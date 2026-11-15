#pragma once

#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>

namespace Online
{
    class RoomTunnel
    {
    public:
        ~RoomTunnel();

        static std::unique_ptr<RoomTunnel> Host(uint16_t port, std::stop_token stop, std::wstring& error);
        static std::unique_ptr<RoomTunnel> Join(std::string_view hostname, std::stop_token stop, std::wstring& error);
        static bool IsHostname(std::string_view hostname);

        const std::string& Hostname() const { return hostname_; }
        uint16_t Port() const { return port_; }

    private:
        struct Process;
        RoomTunnel(std::unique_ptr<Process> process, std::string hostname, uint16_t port);

        std::unique_ptr<Process> process_;
        std::string hostname_;
        uint16_t port_;
    };
}
