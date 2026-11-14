#include "lobby_client.h"
#include "chat_reactions.h"
#include "player_profile.h"

#include <windows.h>
#include <winhttp.h>
#include <json.hpp>

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>

namespace Online::Lobby
{
    namespace
    {
        using Json = nlohmann::json;
        using namespace std::chrono_literals;
        constexpr size_t kMaxResponse = 128 * 1024;
        constexpr INTERNET_PORT kDefaultPort = 8766;

        struct CloseHttp
        {
            void operator()(void* handle) const { WinHttpCloseHandle(handle); }
        };
        using Http = std::unique_ptr<void, CloseHttp>;

        struct CloseWindowsHandle
        {
            void operator()(void* handle) const { CloseHandle(handle); }
        };
        using WindowsHandle = std::unique_ptr<void, CloseWindowsHandle>;

        INTERNET_PORT LobbyPort()
        {
            if (const char* setting = std::getenv("PES_LOBBY_PORT"); setting && *setting)
            {
                const std::string_view value(setting);
                unsigned port = 0;
                const auto number = std::from_chars(value.data(), value.data() + value.size(), port);
                if (number.ec == std::errc{} && number.ptr == value.data() + value.size() && port > 0 && port <= 65535)
                    return INTERNET_PORT(port);
            }
            return kDefaultPort;
        }

        std::wstring Widen(std::string_view text)
        {
            if (text.empty() || text.size() > 1024) return {};
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
            if (count <= 0) return {};
            std::wstring result(size_t(count), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), count);
            return result;
        }

        struct Reply { DWORD status = 0; Json body; };

        Reply Exchange(const wchar_t* path, const std::string& token, const Json* data = nullptr)
        {
            Http session(WinHttpOpen(L"PES13 Lobby/1", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0));
            if (!session || !WinHttpSetTimeouts(session.get(), 1000, 1000, 1200, 1500)) return {};
            Http connection(WinHttpConnect(session.get(), L"127.0.0.1", LobbyPort(), 0));
            if (!connection) return {};
            Http request(WinHttpOpenRequest(connection.get(), data ? L"POST" : L"GET", path,
                nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
            if (!request) return {};
            DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects))) return {};
            std::wstring headers = L"Accept: application/json\r\n";
            if (data) headers += L"Content-Type: application/json\r\n";
            if (!token.empty()) headers += L"Authorization: Bearer " + std::wstring(token.begin(), token.end()) + L"\r\n";
            auto bytes = data ? data->dump() : std::string{};
            if (!WinHttpSendRequest(request.get(), headers.c_str(), DWORD(headers.size()),
                bytes.empty() ? nullptr : bytes.data(), DWORD(bytes.size()), DWORD(bytes.size()), 0) ||
                !WinHttpReceiveResponse(request.get(), nullptr))
                return {};

            Reply reply;
            DWORD length = sizeof(reply.status);
            if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &reply.status, &length, WINHTTP_NO_HEADER_INDEX))
                return {};
            std::string response;
            char block[4096];
            for (;;)
            {
                DWORD got = 0;
                if (!WinHttpReadData(request.get(), block, sizeof(block), &got) || response.size() + got > kMaxResponse)
                    return {};
                if (!got) break;
                response.append(block, got);
            }
            reply.body = Json::parse(response, nullptr, false);
            return reply;
        }

        enum class LobbyService { Ready, Unavailable, Incompatible };

        LobbyService CheckLobbyService()
        {
            const auto reply = Exchange(L"/health", {});
            if (!reply.status)
                return LobbyService::Unavailable;
            if (reply.status == 200 && reply.body.is_object() &&
                reply.body.contains("service") && reply.body["service"] == "pes13-lobby" &&
                reply.body.contains("version") && reply.body["version"] == 2)
                return LobbyService::Ready;
            return LobbyService::Incompatible;
        }

        std::filesystem::path FindLobbyScript()
        {
            std::wstring module(32768, L'\0');
            const DWORD length = GetModuleFileNameW(nullptr, module.data(), DWORD(module.size()));
            if (!length || length >= module.size()) return {};
            module.resize(length);

            std::error_code error;
            const auto directory = std::filesystem::path(module).parent_path();
            const auto installed = directory / L"PES13Recomp\\tools\\online-lobby\\lobby.py";
            if (std::filesystem::is_regular_file(installed, error))
                return installed;
            const auto local = directory / L"tools\\online-lobby\\lobby.py";
            if (std::filesystem::is_regular_file(local, error))
                return local;
            if (directory.filename() == L"bin" && directory.parent_path().parent_path().filename() == L"build")
            {
                const auto debug = directory.parent_path().parent_path().parent_path() / L"tools\\online-lobby\\lobby.py";
                if (std::filesystem::is_regular_file(debug, error))
                    return debug;
            }
            return {};
        }

        std::wstring WindowsEnvironment(const wchar_t* name)
        {
            const DWORD capacity = GetEnvironmentVariableW(name, nullptr, 0);
            if (!capacity) return {};
            std::wstring value(capacity, L'\0');
            const DWORD length = GetEnvironmentVariableW(name, value.data(), capacity);
            if (!length || length >= capacity) return {};
            value.resize(length);
            return value;
        }

        std::filesystem::path FindPython()
        {
            std::error_code error;
            const auto profile = WindowsEnvironment(L"USERPROFILE");
            if (!profile.empty())
            {
                const auto candidate = std::filesystem::path(profile) / L".local\\bin\\python.exe";
                if (std::filesystem::is_regular_file(candidate, error)) return candidate;
            }

            const auto searchPath = WindowsEnvironment(L"PATH");
            std::wstring_view entries(searchPath);
            while (!entries.empty())
            {
                const auto separator = entries.find(L';');
                auto value = entries.substr(0, separator);
                if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
                    value = value.substr(1, value.size() - 2);
                const std::filesystem::path directory(value);
                if (directory.is_absolute() &&
                    CompareStringOrdinal(directory.filename().c_str(), -1, L"WindowsApps", -1, TRUE) != CSTR_EQUAL)
                {
                    const auto candidate = directory / L"python.exe";
                    if (std::filesystem::is_regular_file(candidate, error)) return candidate;
                }
                if (separator == entries.npos) break;
                entries.remove_prefix(separator + 1);
            }
            return {};
        }

        std::wstring PrepareLobbyService(std::stop_token stop)
        {
            const auto initial = CheckLobbyService();
            if (initial == LobbyService::Ready) return {};
            if (initial == LobbyService::Incompatible)
                return L"El puerto del lobby local lo usa otra versi\u00f3n u otro programa.";
            if (std::getenv("PES_LOBBY_PORT"))
                return L"No se pudo conectar al lobby local configurado.";

            WindowsHandle startLock(CreateMutexW(nullptr, FALSE, L"Local\\PES13RecompLobbyStartup"));
            if (!startLock) return L"No se pudo preparar el lobby local.";
            const auto lockDeadline = std::chrono::steady_clock::now() + 6s;
            for (;;)
            {
                if (stop.stop_requested()) return {};
                const DWORD locked = WaitForSingleObject(startLock.get(), 50);
                if (locked == WAIT_OBJECT_0 || locked == WAIT_ABANDONED) break;
                if (locked != WAIT_TIMEOUT || std::chrono::steady_clock::now() >= lockDeadline)
                    return L"No se pudo iniciar el lobby local a tiempo.";
            }
            const auto unlock = [](void* handle) { ReleaseMutex(handle); };
            const std::unique_ptr<void, decltype(unlock)> held(startLock.get(), unlock);
            if (stop.stop_requested()) return {};

            const auto available = CheckLobbyService();
            if (available == LobbyService::Ready) return {};
            if (available == LobbyService::Incompatible)
                return L"El puerto del lobby local lo usa otra versi\u00f3n u otro programa.";
            const auto script = FindLobbyScript();
            if (script.empty()) return L"No se encontr\u00f3 el servicio del lobby en la carpeta de PES.";
            const auto python = FindPython();
            if (python.empty()) return L"El lobby local necesita Python 3. No se encontr\u00f3 python.exe.";

            auto arguments = L"\"" + python.wstring() + L"\" -I -B \"" + script.wstring() +
                L"\" --idle-seconds 60 --port " + std::to_wstring(kDefaultPort);
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(python.c_str(), arguments.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                nullptr, script.parent_path().c_str(), &startup, &process))
                return L"No se pudo abrir el servicio del lobby local.";
            const WindowsHandle child(process.hProcess);
            const WindowsHandle thread(process.hThread);
            const auto readyDeadline = std::chrono::steady_clock::now() + 5s;
            do
            {
                const auto health = CheckLobbyService();
                if (health == LobbyService::Ready) return {};
                if (health == LobbyService::Incompatible) break;
                if (WaitForSingleObject(child.get(), 80) != WAIT_TIMEOUT) break;
            } while (!stop.stop_requested() && std::chrono::steady_clock::now() < readyDeadline);
            return L"No se pudo iniciar el lobby local. Revis\u00e1 que Python 3 funcione y que el puerto 8766 est\u00e9 disponible.";
        }

        bool ReadState(const Json& json, State& state)
        {
            try
            {
                if (!(json.is_object() && json.at("players").is_array() && json.at("rooms").is_array() &&
                    json.at("invitations").is_array() && json.at("messages").is_array()))
                    return false;
                State next;
                next.connected = true;
                next.selfId = json.at("you").at("id").get<std::string>();
                for (const auto& row : json.at("players"))
                {
                    auto id = row.at("id").get<std::string>();
                    if (id != next.selfId)
                        next.players.push_back({ std::move(id), Widen(row.at("name").get<std::string>()),
                            row.at("status").get<std::string>() });
                }
                for (const auto& row : json.at("rooms"))
                {
                    auto id = row.at("id").get<std::string>();
                    if (row.at("host_id").get<std::string>() == next.selfId) next.ownRoomId = std::move(id);
                    else next.rooms.push_back({ std::move(id), Widen(row.at("host_name").get<std::string>()) });
                }
                for (const auto& row : json.at("invitations"))
                    next.invitations.push_back({ row.at("id").get<std::string>(), Widen(row.at("from_name").get<std::string>()) });
                if (const auto& match = json.at("match"); !match.is_null())
                {
                    next.match = Match{ Widen(match.at("peer_name").get<std::string>()),
                        match.at("kind").get<std::string>(), match.at("id").get<std::string>() };
                    if (state.match && state.match->id == next.match->id) next.chatError = state.chatError;
                    for (const auto& row : json.at("messages"))
                        next.messages.push_back({ row.at("id").get<uint64_t>(),
                            Widen(row.at("name").get<std::string>()), Widen(row.at("text").get<std::string>()),
                            row.at("reaction").get<std::string>(), row.at("sender_id").get<std::string>() });
                }
                state = std::move(next);
                return true;
            }
            catch (const Json::exception&) { return false; }
        }

        std::wstring ReplyError(const Reply& reply)
        {
            if (reply.body.is_object() && reply.body.contains("error") && reply.body["error"].is_string())
                return Widen(reply.body["error"].get<std::string>());
            return L"No se pudo conectar al lobby local.";
        }
    }

    Client::Client() : worker_([this](std::stop_token stop) { Run(stop); }) {}

    State Client::GetState() const
    {
        std::lock_guard lock(mutex_);
        return state_;
    }

    void Client::Request(Action action, std::string id, std::wstring name)
    {
        std::lock_guard lock(mutex_);
        if (pending_ || state_.busy) return;
        if (action == Action::Connect ? state_.connected : !state_.connected) return;
        state_.busy = true;
        pending_ = Command{ action, std::move(id), std::move(name), {}, {} };
        wake_.notify_one();
    }

    bool Client::SendChat(std::string text, std::string reaction, std::wstring& error)
    {
        std::lock_guard lock(mutex_);
        if (!state_.match)
        {
            error = L"Entr\u00e1 a una sala con otro jugador para usar el chat.";
            return false;
        }
        if (pending_ || state_.busy)
        {
            error = L"Esper\u00e1 un instante y volv\u00e9 a intentar.";
            return false;
        }
        state_.chatError.clear();
        state_.busy = true;
        pending_ = Command{ Action::Chat, state_.match->id, {}, std::move(text), std::move(reaction) };
        wake_.notify_one();
        return true;
    }

    void Client::Run(std::stop_token stop)
    {
        std::string token;
        std::wstring lastMessage;
        State current;
        while (!stop.stop_requested())
        {
            Command command{ Action::Refresh, {}, {}, {}, {} };
            bool requested = false;
            {
                std::unique_lock lock(mutex_);
                if (token.empty()) wake_.wait(lock, stop, [this] { return pending_.has_value(); });
                else wake_.wait_for(lock, stop, current.match ? 350ms : 1200ms, [this] { return pending_.has_value(); });
                if (stop.stop_requested()) break;
                if (pending_)
                {
                    command = std::move(*pending_);
                    pending_.reset();
                    requested = true;
                }
            }

            Reply reply;
            std::wstring setupError;
            Json payload = Json::object();
            switch (command.action)
            {
            case Action::Connect:
                setupError = PrepareLobbyService(stop);
                if (stop.stop_requested()) return;
                if (!setupError.empty()) break;
                payload["name"] = PlayerNameUtf8(command.name);
                reply = Exchange(L"/session", {}, &payload);
                if (reply.status < 300 && reply.body.is_object() && reply.body.contains("token") && reply.body["token"].is_string())
                {
                    auto candidate = reply.body["token"].get<std::string>();
                    if (!candidate.empty() && candidate.size() <= 128 &&
                        candidate.find_first_not_of("0123456789abcdef") == std::string::npos)
                    {
                        token = std::move(candidate);
                        reply = Exchange(L"/state", token);
                    }
                }
                break;
            case Action::Refresh:
                reply = Exchange(L"/state", token);
                break;
            case Action::OpenRoom:
                reply = Exchange(L"/room", token, &payload);
                break;
            case Action::JoinRoom:
                payload["room_id"] = command.id;
                reply = Exchange(L"/join", token, &payload);
                break;
            case Action::Invite:
                payload["player_id"] = command.id;
                reply = Exchange(L"/invite", token, &payload);
                break;
            case Action::Accept:
            case Action::Decline:
                payload["invite_id"] = command.id;
                payload["accept"] = command.action == Action::Accept;
                reply = Exchange(L"/respond", token, &payload);
                break;
            case Action::Leave:
                reply = Exchange(L"/leave", token, &payload);
                break;
            case Action::Chat:
                current.chatError.clear();
                payload["match_id"] = command.id;
                if (command.reaction.empty()) payload["text"] = command.text;
                else payload["reaction"] = command.reaction;
                reply = Exchange(L"/chat", token, &payload);
                break;
            case Action::Disconnect:
                Exchange(L"/disconnect", token, &payload);
                token.clear();
                current = {};
                current.message = L"Te desconectaste del lobby.";
                break;
            }

            if (command.action != Action::Disconnect)
            {
                const bool success = reply.status >= 200 && reply.status < 300 && ReadState(reply.body, current);
                if (success)
                {
                    if (command.action == Action::Invite) current.message = L"Invitaci\u00f3n enviada.";
                    else if (command.action == Action::Decline) current.message = L"Invitaci\u00f3n rechazada.";
                    if (requested || current.match) lastMessage = current.message;
                    else current.message = lastMessage;
                }
                else
                {
                    if (reply.status == 0 || reply.status == 401 || command.action == Action::Connect ||
                        (reply.status >= 200 && reply.status < 300))
                    {
                        token.clear();
                        current = {};
                    }
                    if (command.action == Action::Chat) current.chatError = ReplyError(reply);
                    else
                    {
                        current.message = setupError.empty() ? ReplyError(reply) : std::move(setupError);
                        lastMessage = current.message;
                    }
                }
            }
            else lastMessage.clear();

            std::lock_guard lock(mutex_);
            current.busy = pending_.has_value();
            state_ = current;
        }
    }

    Client& GetClient()
    {
        static Client client;
        return client;
    }

    bool SubmitChat(std::wstring_view text, std::wstring& error)
    {
        error.clear();
        const auto start = text.find_first_not_of(L' ');
        if (start == text.npos) return false;
        text = text.substr(start, text.find_last_not_of(L' ') - start + 1);
        if (text.front() == L'/')
        {
            std::wstring command(text);
            for (auto& ch : command)
                if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
            for (const auto& reaction : kChatReactions)
                if (reaction.command == command)
                    return GetClient().SendChat({}, std::string(reaction.key), error);
            error = L"Comando desconocido. Eleg\u00ed una reacci\u00f3n de la lista.";
            return false;
        }
        if (text.size() > kChatMaxTextUnits || text.find_first_of(L"\r\n\t") != text.npos)
        {
            error = L"Escrib\u00ed hasta 180 caracteres en una sola l\u00ednea.";
            return false;
        }
        char bytes[kChatMaxTextUnits * 3];
        const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), bytes, sizeof(bytes), nullptr, nullptr);
        if (!length)
        {
            error = L"El mensaje contiene un car\u00e1cter inv\u00e1lido.";
            return false;
        }
        return GetClient().SendChat(std::string(bytes, length), {}, error);
    }
}
