#include "room_tunnel.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "Ws2_32.lib")

namespace Online
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        using namespace std::chrono_literals;

        struct CloseHandleOnExit
        {
            void operator()(void* value) const { CloseHandle(value); }
        };
        using Handle = std::unique_ptr<void, CloseHandleOnExit>;

        struct DeleteAttributeList
        {
            void operator()(LPPROC_THREAD_ATTRIBUTE_LIST value) const { DeleteProcThreadAttributeList(value); }
        };

        struct FreeEnvironment
        {
            void operator()(wchar_t* value) const { FreeEnvironmentStringsW(value); }
        };

        std::wstring TunnelEnvironment()
        {
            const std::unique_ptr<wchar_t, FreeEnvironment> current(GetEnvironmentStringsW());
            if (!current) return {};
            std::wstring environment;
            for (const wchar_t* entry = current.get(); *entry;)
            {
                const std::wstring_view text(entry);
                const auto hasPrefix = [text](std::wstring_view prefix) {
                    return text.size() >= prefix.size() && CompareStringOrdinal(text.data(), int(prefix.size()),
                        prefix.data(), int(prefix.size()), TRUE) == CSTR_EQUAL;
                };
                // Cloudflared has environment defaults for Access service credentials and private tunnels.
                if (!hasPrefix(L"TUNNEL_") && !hasPrefix(L"CLOUDFLARE_") && !hasPrefix(L"CF_ACCESS_"))
                {
                    environment.append(text);
                    environment.push_back(L'\0');
                }
                entry += text.size() + 1;
            }
            if (environment.empty()) environment.push_back(L'\0');
            environment.push_back(L'\0');
            return environment;
        }

        bool IsLabelCharacter(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
        }

        std::string QuickHostname(std::string_view logs)
        {
            // The published address is printed in the startup table. An API error can also contain a trycloudflare URL.
            constexpr std::string_view addressRow = "|  https://";
            for (size_t start = logs.find(addressRow); start != logs.npos; start = logs.find(addressRow, start + addressRow.size()))
            {
                const auto candidate = logs.substr(start + addressRow.size());
                const auto end = std::ranges::find_if(candidate, [](char c) { return !IsLabelCharacter(c) && c != '.'; });
                // A final partial line can arrive on a following pipe read.
                if (end != candidate.end())
                {
                    const auto hostname = candidate.substr(0, size_t(end - candidate.begin()));
                    if (RoomTunnel::IsHostname(hostname)) return std::string(hostname);
                }
            }
            return {};
        }

        std::filesystem::path CloudflaredPath()
        {
            std::wstring module(32768, L'\0');
            const DWORD length = GetModuleFileNameW(nullptr, module.data(), DWORD(module.size()));
            if (!length || length >= module.size()) return {};
            module.resize(length);
            const auto candidate = std::filesystem::path(module).parent_path() / L"Soporte" / L"cloudflared.exe";
            std::error_code error;
            return std::filesystem::is_regular_file(candidate, error) ? candidate : std::filesystem::path{};
        }

        bool CanResolve(std::string_view hostname, Clock::time_point deadline, std::stop_token stop)
        {
            WSADATA data{};
            if (WSAStartup(MAKEWORD(2, 2), &data)) return false;
            Handle complete(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            bool resolved = false;
            if (complete)
            {
                const std::wstring name(hostname.begin(), hostname.end());
                ADDRINFOEXW hints{};
                hints.ai_socktype = SOCK_STREAM;
                ADDRINFOEXW* addresses = nullptr;
                OVERLAPPED query{};
                query.hEvent = complete.get();
                HANDLE cancellation = nullptr;
                int status = GetAddrInfoExW(name.c_str(), nullptr, NS_DNS, nullptr, &hints, &addresses,
                    nullptr, &query, nullptr, &cancellation);
                if (status == WSA_IO_PENDING)
                {
                    DWORD wait = WAIT_TIMEOUT;
                    while (wait == WAIT_TIMEOUT && !stop.stop_requested() && Clock::now() < deadline)
                        wait = WaitForSingleObject(complete.get(), 25);
                    if (wait != WAIT_OBJECT_0)
                    {
                        GetAddrInfoExCancel(&cancellation);
                        // Winsock owns the query until its completion event is signaled.
                        WaitForSingleObject(complete.get(), INFINITE);
                    }
                    status = GetAddrInfoExOverlappedResult(&query);
                }
                resolved = status == 0 && addresses != nullptr;
                if (addresses) FreeAddrInfoExW(addresses);
            }
            WSACleanup();
            return resolved && !stop.stop_requested();
        }

        uint16_t LoopbackListener(DWORD pid, std::vector<uint8_t>& buffer)
        {
            DWORD size = DWORD(buffer.size());
            DWORD result = GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
            if (result == ERROR_INSUFFICIENT_BUFFER && size <= 1024 * 1024)
            {
                buffer.resize(size);
                result = GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
            }
            if (result != NO_ERROR) return 0;
            const auto& table = *reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
            uint16_t port = 0;
            for (DWORD index = 0; index < table.dwNumEntries; ++index)
            {
                const auto& entry = table.table[index];
                if (entry.dwOwningPid != pid || entry.dwState != MIB_TCP_STATE_LISTEN ||
                    entry.dwLocalAddr != htonl(INADDR_LOOPBACK))
                    continue;
                if (port) return 0;
                port = ntohs(uint16_t(entry.dwLocalPort));
            }
            return port;
        }
    }

    struct RoomTunnel::Process
    {
        Handle process;
        Handle output;
        Handle job;
        DWORD pid = 0;
        std::jthread drain;

        ~Process()
        {
            drain.request_stop();
            job.reset();
            if (drain.joinable()) drain.join();
        }

        static std::unique_ptr<Process> Start(std::wstring_view arguments, std::wstring& error)
        {
            const auto exe = CloudflaredPath();
            if (exe.empty())
            {
                error = L"Falta Soporte\\cloudflared.exe. Extra\u00e9 toda la carpeta del juego.";
                return {};
            }

            error = L"No se pudo iniciar el t\u00fanel de Internet.";
            auto result = std::make_unique<Process>();
            result->job.reset(CreateJobObjectW(nullptr, nullptr));
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION settings{};
            settings.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!result->job || !SetInformationJobObject(result->job.get(), JobObjectExtendedLimitInformation,
                &settings, sizeof(settings)))
                return {};

            SECURITY_ATTRIBUTES inherited{sizeof(inherited), nullptr, TRUE};
            HANDLE readHandle = nullptr;
            HANDLE writeHandle = nullptr;
            if (!CreatePipe(&readHandle, &writeHandle, &inherited, 0)) return {};
            result->output.reset(readHandle);
            Handle write(writeHandle);
            if (!SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0)) return {};
            const HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                &inherited, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (nullInput == INVALID_HANDLE_VALUE) return {};
            Handle input(nullInput);

            SIZE_T bytes = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
            if (!bytes) return {};
            std::vector<uint8_t> attributes(bytes);
            const auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
            if (!InitializeProcThreadAttributeList(list, 1, 0, &bytes)) return {};
            const std::unique_ptr<std::remove_pointer_t<LPPROC_THREAD_ATTRIBUTE_LIST>, DeleteAttributeList> cleanup(list);
            std::array handles{write.get(), input.get()};
            if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.data(),
                sizeof(handles), nullptr, nullptr))
                return {};

            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof(startup);
            startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            startup.StartupInfo.hStdInput = input.get();
            startup.StartupInfo.hStdOutput = write.get();
            startup.StartupInfo.hStdError = write.get();
            startup.lpAttributeList = list;
            std::wstring command = L"\"" + exe.wstring() + L"\" ";
            command += arguments;
            auto environment = TunnelEnvironment();
            if (environment.empty()) return {};
            PROCESS_INFORMATION child{};
            if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                environment.data(),
                exe.parent_path().c_str(), &startup.StartupInfo, &child))
            {
                const DWORD code = GetLastError();
                error = L"Windows no pudo abrir cloudflared.exe (c\u00f3digo " + std::to_wstring(code) + L").";
                return {};
            }
            result->process.reset(child.hProcess);
            result->pid = child.dwProcessId;
            Handle thread(child.hThread);
            if (!AssignProcessToJobObject(result->job.get(), result->process.get()) ||
                ResumeThread(thread.get()) == DWORD(-1))
            {
                const DWORD code = GetLastError();
                TerminateProcess(result->process.get(), 1);
                error = L"Windows no pudo iniciar el proceso del t\u00fanel (c\u00f3digo " + std::to_wstring(code) + L").";
                return {};
            }
            error.clear();
            return result;
        }

        bool Alive() const { return WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT; }

        std::string_view Read(std::array<char, 4096>& chunk)
        {
            DWORD available = 0;
            DWORD copied = 0;
            if (!PeekNamedPipe(output.get(), nullptr, 0, nullptr, &available, nullptr) || !available ||
                !ReadFile(output.get(), chunk.data(), std::min(available, DWORD(chunk.size())), &copied, nullptr))
                return {};
            return {chunk.data(), copied};
        }

        void DiscardOutput()
        {
            drain = std::jthread([this](std::stop_token stop) {
                std::array<char, 4096> chunk{};
                while (!stop.stop_requested() && Alive())
                {
                    if (Read(chunk).empty()) WaitForSingleObject(process.get(), 50);
                }
            });
        }
    };

    RoomTunnel::RoomTunnel(std::unique_ptr<Process> process, std::string hostname, uint16_t port)
        : process_(std::move(process)), hostname_(std::move(hostname)), port_(port)
    {
        process_->DiscardOutput();
    }

    RoomTunnel::~RoomTunnel() = default;

    bool RoomTunnel::IsHostname(std::string_view hostname)
    {
        constexpr std::string_view suffix = ".trycloudflare.com";
        if (!hostname.ends_with(suffix)) return false;
        const auto label = hostname.substr(0, hostname.size() - suffix.size());
        return !label.empty() && label.size() <= 63 && label.front() != '-' && label.back() != '-' &&
            std::ranges::all_of(label, IsLabelCharacter);
    }

    std::unique_ptr<RoomTunnel> RoomTunnel::Host(uint16_t port, std::stop_token stop, std::wstring& error)
    {
        if (!port || stop.stop_requested())
        {
            error = port ? L"Se cancel\u00f3 el t\u00fanel." : L"Primero abr\u00ed la sala para iniciar el t\u00fanel.";
            return {};
        }
        auto process = Process::Start(L"tunnel --config= --no-autoupdate --protocol auto --retries 3 --url tcp://127.0.0.1:" +
            std::to_wstring(port), error);
        if (!process) return {};

        constexpr size_t maxLog = 8192;
        const auto deadline = Clock::now() + 60s;
        std::array<char, 4096> chunk{};
        std::string logs;
        std::string hostname;
        bool registered = false;
        while (!stop.stop_requested() && Clock::now() < deadline)
        {
            const auto text = process->Read(chunk);
            logs.append(text);
            if (logs.size() > maxLog) logs.erase(0, logs.size() - maxLog);
            if (!registered) registered = logs.find("Registered tunnel connection") != logs.npos;
            if (hostname.empty()) hostname = QuickHostname(logs);
            if (registered && !hostname.empty() && process->Alive())
                return std::unique_ptr<RoomTunnel>(new RoomTunnel(std::move(process), std::move(hostname), port));
            if (text.empty())
            {
                if (!process->Alive()) break;
                WaitForSingleObject(process->process.get(), 75);
            }
        }
        if (stop.stop_requested())
            error = L"Se cancel\u00f3 el t\u00fanel.";
        else if (logs.find("quick tunnel provisioning failed with status 429") != logs.npos)
            error = L"Cloudflare limit\u00f3 las solicitudes (429). Esper\u00e1 un momento y reintent\u00e1.";
        else if (logs.find("x509:") != logs.npos)
            error = L"El t\u00fanel no pudo validar el certificado de Cloudflare.";
        else if (hostname.empty())
            error = L"Cloudflare no entreg\u00f3 una direcci\u00f3n para la sala. Volv\u00e9 a intentar.";
        else if (!registered)
            error = L"El t\u00fanel no lleg\u00f3 a Cloudflare (salida 7844). Prob\u00e1 que la sala la cree tu amigo.";
        else
            error = L"El t\u00fanel de Cloudflare se cerr\u00f3 antes de publicar la sala. Volv\u00e9 a intentar.";
        return {};
    }

    std::unique_ptr<RoomTunnel> RoomTunnel::Join(std::string_view hostname, std::stop_token stop, std::wstring& error)
    {
        if (!IsHostname(hostname) || stop.stop_requested())
        {
            error = stop.stop_requested() ? L"Se cancel\u00f3 el t\u00fanel." : L"El nombre del t\u00fanel no es v\u00e1lido.";
            return {};
        }
        // Test publication on the guest's own network. The host can have a different DNS cache.
        const auto dnsDeadline = Clock::now() + 30s;
        bool resolved = false;
        while (!stop.stop_requested() && Clock::now() < dnsDeadline)
        {
            resolved = CanResolve(hostname, std::min(dnsDeadline, Clock::now() + 1s), stop);
            if (resolved) break;
            std::this_thread::sleep_for(250ms);
        }
        if (!resolved)
        {
            error = stop.stop_requested() ? L"Se cancel\u00f3 el t\u00fanel."
                : L"Tu PC no encuentra la direcci\u00f3n de la sala (DNS). Ped\u00ed un c\u00f3digo nuevo.";
            return {};
        }
        const std::wstring name(hostname.begin(), hostname.end());
        auto process = Process::Start(L"access tcp --hostname " + name + L" --url 127.0.0.1:0", error);
        if (!process) return {};

        const auto deadline = Clock::now() + 5s;
        std::vector<uint8_t> table(4096);
        std::array<char, 4096> chunk{};
        while (!stop.stop_requested() && Clock::now() < deadline && process->Alive())
        {
            process->Read(chunk);
            if (const uint16_t port = LoopbackListener(process->pid, table); port && process->Alive())
                return std::unique_ptr<RoomTunnel>(new RoomTunnel(std::move(process), std::string(hostname), port));
            WaitForSingleObject(process->process.get(), 50);
        }
        error = stop.stop_requested() ? L"Se cancel\u00f3 el t\u00fanel."
            : L"No se pudo abrir la conexi\u00f3n local del t\u00fanel. Volv\u00e9 a intentar.";
        return {};
    }
}
