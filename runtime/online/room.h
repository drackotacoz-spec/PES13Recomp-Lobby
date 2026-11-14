#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>

namespace Online
{
    struct PadInput
    {
        uint16_t buttons = 0;
        uint8_t lt = 0, rt = 0;
        int16_t lx = 0, ly = 0, rx = 0, ry = 0;

        bool operator==(const PadInput&) const = default;
    };

    // The caller serializes access. Use a worker so that a wait does not block
    // the game window, and request its stop token before joining that worker.
    class Room
    {
    public:
        enum class Mode : uint8_t { Input, Latency };

        explicit Room(Mode mode = Mode::Input) : mode_(mode) {}
        Room(const Room&) = delete;
        Room& operator=(const Room&) = delete;
        ~Room();

        // Set before Host or Join. An empty nickname omits it from the room.
        bool SetLocalName(std::string_view utf8);
        // An empty address binds to 127.0.0.1. Port 0 selects an available port.
        bool Host(std::string_view bindAddress, uint16_t port);
        bool WaitForPeer(std::stop_token stop = {});
        bool Join(std::string_view address, uint16_t port, std::string_view token, std::stop_token stop = {});
        bool ExchangeInput(uint32_t frame, const PadInput& local, PadInput& remote, std::stop_token stop = {});
        // Repeat on the latency worker. The host schedules a probe in each direction
        // every second; a call waits cancellably until its full round finishes.
        bool MeasureLatency(std::chrono::microseconds& roundTrip, std::stop_token stop = {});
        void Close();

        bool Connected() const { return connected_; }
        bool PeerClosed() const;
        bool Hosting() const { return listener_ != kInvalidSocket; }
        bool IsHost() const { return host_; }
        uint16_t Port() const { return port_; }
        const std::string& Token() const { return token_; }
        const std::string& PeerAddress() const { return peerAddress_; }
        const std::string& PeerName() const { return peerName_; }
        const std::string& Error() const { return error_; }

    private:
        using Clock = std::chrono::steady_clock;
        static constexpr uintptr_t kInvalidSocket = ~uintptr_t{ 0 };

        bool StartWinsock();
        bool SocketError(std::string_view reason, int code);
        bool Transfer(std::span<uint8_t> bytes, bool sending, Clock::time_point deadline, std::stop_token stop);
        bool SendName(Clock::time_point deadline, std::stop_token stop);
        bool ReceiveName(std::string& name, Clock::time_point deadline, std::stop_token stop);
        bool Ping(Clock::time_point deadline, std::stop_token stop, std::chrono::microseconds& roundTrip);
        bool AnswerPing(Clock::time_point deadline, std::stop_token stop);
        void ClosePeer();

        const Mode mode_;
        uintptr_t listener_ = kInvalidSocket;
        uintptr_t peer_ = kInvalidSocket;
        bool wsaStarted_ = false;
        bool connected_ = false;
        bool host_ = false;
        uint16_t port_ = 0;
        std::string token_;
        std::string peerAddress_;
        std::string localName_;
        std::string peerName_;
        std::string error_;
        uint32_t latencySequence_ = 0;
        Clock::time_point nextProbe_{};
    };
}
