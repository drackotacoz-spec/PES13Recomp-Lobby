#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Online::Lobby
{
    enum class Action { Connect, Refresh, OpenRoom, JoinRoom, Invite, Accept, Decline, Leave, Disconnect, Chat };
    struct Player { std::string id; std::wstring name; std::string status; };
    struct PublicRoom { std::string id; std::wstring hostName; };
    struct Invitation { std::string id; std::wstring fromName; };
    struct Match { std::wstring peerName; std::string kind; std::string id; };
    struct Message
    {
        uint64_t id;
        std::wstring name;
        std::wstring text;
        std::string reaction;
        std::string senderId;
    };
    struct State
    {
        bool connected = false;
        bool busy = false;
        std::vector<Player> players;
        std::vector<PublicRoom> rooms;
        std::vector<Invitation> invitations;
        std::optional<Match> match;
        std::string selfId;
        std::vector<Message> messages;
        std::string ownRoomId;
        std::wstring message;
        std::wstring chatError;
    };

    class Client
    {
    public:
        Client();
        State GetState() const;
        void Request(Action action, std::string id = {}, std::wstring name = {});
        bool SendChat(std::string text, std::string reaction, std::wstring& error);

    private:
        struct Command { Action action; std::string id; std::wstring name; std::string text; std::string reaction; };
        void Run(std::stop_token stop);
        mutable std::mutex mutex_;
        std::condition_variable_any wake_;
        State state_;
        std::optional<Command> pending_;
        std::jthread worker_;
    };

    Client& GetClient();
    bool SubmitChat(std::wstring_view text, std::wstring& error);
}
