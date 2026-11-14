#include "room.h"
#include "player_profile.h"

#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#   define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <bit>
#include <condition_variable>
#include <cstring>
#include <mutex>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Bcrypt.lib")

namespace Online
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        using namespace std::chrono_literals;

        constexpr auto kInviteTimeout = 2min;
        constexpr auto kConnectTimeout = 8s;
        constexpr auto kHandshakeTimeout = 3s;
        constexpr auto kInputTimeout = 3s;
        constexpr auto kPingInterval = 1s;
        constexpr auto kPingTimeout = 3s;
        constexpr std::string_view kConnectTimeoutMessage =
            "La sala no respondi\u00f3. Revisen la IPv4, el puerto TCP, el firewall y el router.";
        constexpr std::array<uint8_t, 4> kRoomTag{ 'P', '1', '3', 'R' };
        constexpr std::array<uint8_t, 4> kInputTag{ 'P', '1', '3', 'I' };
        constexpr std::array<uint8_t, 4> kPingTag{ 'P', '1', '3', 'P' };
        constexpr std::array<uint8_t, 4> kPongTag{ 'P', '1', '3', 'A' };
        constexpr uint16_t kProtocolVersion = 3;
        constexpr uint8_t kHostRole = 1;
        constexpr uint8_t kGuestRole = 2;
        constexpr size_t kSecretSize = 16;
        constexpr size_t kMaxNameBytes = 3 * kMaxPlayerNameLength;
        using Secret = std::array<uint8_t, kSecretSize>;
        using Handshake = std::array<uint8_t, 8 + kSecretSize>;
        using InputMessage = std::array<uint8_t, 20>;
        using PingMessage = std::array<uint8_t, 8>;

        void Store16(std::span<uint8_t> bytes, uint16_t value)
        {
            bytes[0] = uint8_t(value >> 8);
            bytes[1] = uint8_t(value);
        }

        uint16_t Load16(std::span<const uint8_t> bytes)
        {
            return uint16_t((uint16_t(bytes[0]) << 8) | bytes[1]);
        }

        void Store32(std::span<uint8_t> bytes, uint32_t value)
        {
            Store16(bytes, uint16_t(value >> 16));
            Store16(bytes.subspan(2), uint16_t(value));
        }

        uint32_t Load32(std::span<const uint8_t> bytes)
        {
            return (uint32_t(Load16(bytes)) << 16) | Load16(bytes.subspan(2));
        }

        int HexValue(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        bool DecodeToken(std::string_view token, Secret& secret)
        {
            if (token.size() != 2 * secret.size()) return false;
            for (size_t index = 0; index < secret.size(); ++index)
            {
                const int high = HexValue(token[2 * index]);
                const int low = HexValue(token[2 * index + 1]);
                if (high < 0 || low < 0) return false;
                secret[index] = uint8_t((high << 4) | low);
            }
            return true;
        }

        std::string EncodeToken(const Secret& secret)
        {
            constexpr char kHex[] = "0123456789abcdef";
            std::string token;
            token.reserve(secret.size() * 2);
            for (const uint8_t byte : secret)
            {
                token.push_back(kHex[byte >> 4]);
                token.push_back(kHex[byte & 0x0F]);
            }
            return token;
        }

        bool SameSecret(std::span<const uint8_t> received, const Secret& expected)
        {
            uint8_t difference = 0;
            for (size_t index = 0; index < expected.size(); ++index)
                difference |= received[index] ^ expected[index];
            return difference == 0;
        }

        Handshake MakeHandshake(uint8_t role, Room::Mode mode)
        {
            Handshake message{};
            std::ranges::copy(kRoomTag, message.begin());
            Store16(std::span(message).subspan(4), kProtocolVersion);
            message[6] = role;
            message[7] = static_cast<uint8_t>(mode);
            return message;
        }

        bool ValidHandshake(const Handshake& message, uint8_t role, Room::Mode mode)
        {
            return std::ranges::equal(kRoomTag, std::span(message).first<4>())
                && Load16(std::span(message).subspan(4)) == kProtocolVersion
                && message[6] == role && message[7] == static_cast<uint8_t>(mode);
        }

        bool Address(std::string_view text, uint16_t port, sockaddr_in& result)
        {
            if (text.empty() || text == "localhost") text = "127.0.0.1";
            if (text.size() > 15 || text.find('\0') != text.npos) return false;
            result = {};
            result.sin_family = AF_INET;
            result.sin_port = htons(port);
            const std::string address(text);
            if (InetPtonA(AF_INET, address.c_str(), &result.sin_addr) != 1)
                return false;
            const auto firstOctet = ntohl(result.sin_addr.s_addr) >> 24;
            return firstOctet > 0 && firstOctet < 224;
        }

        std::string AddressText(const sockaddr_in& endpoint)
        {
            char address[INET_ADDRSTRLEN]{};
            return InetNtopA(AF_INET, &endpoint.sin_addr, address, sizeof(address)) ? address : "";
        }

        PingMessage MakePing(const std::array<uint8_t, 4>& tag, uint32_t sequence)
        {
            PingMessage message{};
            std::ranges::copy(tag, message.begin());
            Store32(std::span(message).subspan(4), sequence);
            return message;
        }

        bool SetNonBlocking(SOCKET socket)
        {
            u_long enabled = 1;
            return ioctlsocket(socket, FIONBIO, &enabled) != SOCKET_ERROR;
        }

        bool ConfigurePeer(SOCKET socket)
        {
            BOOL enabled = TRUE;
            return SetNonBlocking(socket) &&
                setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled)) != SOCKET_ERROR;
        }

        enum class Wait { Ready, Timeout, Cancelled, Failed };

        Wait Ready(SOCKET socket, bool writing, Clock::time_point deadline, std::stop_token stop)
        {
            while (!stop.stop_requested())
            {
                const auto remaining = deadline - Clock::now();
                if (remaining <= Clock::duration::zero()) return Wait::Timeout;
                const auto interval = std::min(std::chrono::ceil<std::chrono::microseconds>(remaining), 50'000us);
                timeval timeout{ 0, long(interval.count()) };
                fd_set sockets{}, failures{};
                FD_SET(socket, &sockets);
                FD_SET(socket, &failures);
                const int result = select(0, writing ? nullptr : &sockets, writing ? &sockets : nullptr, &failures, &timeout);
                if (result > 0) return Wait::Ready;
                if (result == SOCKET_ERROR) return Wait::Failed;
            }
            return Wait::Cancelled;
        }

        InputMessage MakeInput(uint32_t frame, const PadInput& pad)
        {
            InputMessage message{};
            std::ranges::copy(kInputTag, message.begin());
            auto bytes = std::span(message);
            Store32(bytes.subspan(4), frame);
            Store16(bytes.subspan(8), pad.buttons);
            message[10] = pad.lt;
            message[11] = pad.rt;
            Store16(bytes.subspan(12), std::bit_cast<uint16_t>(pad.lx));
            Store16(bytes.subspan(14), std::bit_cast<uint16_t>(pad.ly));
            Store16(bytes.subspan(16), std::bit_cast<uint16_t>(pad.rx));
            Store16(bytes.subspan(18), std::bit_cast<uint16_t>(pad.ry));
            return message;
        }

        bool ParseInput(const InputMessage& message, uint32_t frame, PadInput& pad)
        {
            auto bytes = std::span(message);
            if (!std::ranges::equal(kInputTag, bytes.first<4>()) || Load32(bytes.subspan(4)) != frame) return false;
            pad = {
                Load16(bytes.subspan(8)), message[10], message[11],
                std::bit_cast<int16_t>(Load16(bytes.subspan(12))),
                std::bit_cast<int16_t>(Load16(bytes.subspan(14))),
                std::bit_cast<int16_t>(Load16(bytes.subspan(16))),
                std::bit_cast<int16_t>(Load16(bytes.subspan(18)))
            };
            return true;
        }
    }

    Room::~Room()
    {
        Close();
        if (wsaStarted_) WSACleanup();
    }

    bool Room::StartWinsock()
    {
        if (wsaStarted_) return true;
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result) return SocketError("No se pudo iniciar la red de Windows.", result);
        wsaStarted_ = true;
        return true;
    }

    bool Room::SocketError(std::string_view reason, int code)
    {
        switch (code)
        {
        case WSAEADDRINUSE:
            reason = "El puerto TCP est\u00e1 en uso. Cerr\u00e1 la otra sala y volv\u00e9 a intentar.";
            break;
        case WSAEACCES:
            reason = "Windows bloque\u00f3 el puerto TCP. Revis\u00e1 si otra sala lo usa o si est\u00e1 reservado.";
            break;
        case WSAECONNREFUSED:
            reason = "Conexi\u00f3n rechazada. Confirm\u00e1 la IPv4 y que la sala siga abierta en ese puerto TCP.";
            break;
        case WSAETIMEDOUT:
            reason = kConnectTimeoutMessage;
            break;
        case WSAENETUNREACH:
        case WSAEHOSTUNREACH:
            reason = "Windows no encuentra una ruta hasta esa IPv4. Revis\u00e1 la red y el c\u00f3digo.";
            break;
        case WSAENETDOWN:
            reason = "Windows no tiene acceso a la red. Revis\u00e1 tu conexi\u00f3n.";
            break;
        case WSAECONNABORTED:
        case WSAECONNRESET:
            reason = "Se cort\u00f3 la conexi\u00f3n TCP con el otro equipo.";
            break;
        }
        error_ = std::string(reason) + " (Winsock " + std::to_string(code) + ')';
        return false;
    }

    bool Room::SetLocalName(std::string_view utf8)
    {
        error_.clear();
        if (Connected() || Hosting())
        {
            error_ = "Sali de la sala para cambiar de apodo.";
            return false;
        }
        if (utf8.empty())
        {
            localName_.clear();
            return true;
        }
        std::string name;
        if (utf8.size() <= kMaxNameBytes)
            name = PlayerNameUtf8(PlayerNameFromUtf8(utf8));
        if (name.empty())
        {
            error_ = "El apodo no es valido.";
            return false;
        }
        localName_ = std::move(name);
        return true;
    }

    bool Room::Host(std::string_view bindAddress, uint16_t port)
    {
        Close();
        error_.clear();
        if (!StartWinsock()) return false;

        sockaddr_in endpoint{};
        if (!Address(bindAddress, port, endpoint))
        {
            error_ = "Us\u00e1 la IPv4 local de tu PC. Para probar en la misma PC, us\u00e1 127.0.0.1.";
            return false;
        }

        const SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == INVALID_SOCKET) return SocketError("No se pudo abrir el puerto de la sala.", WSAGetLastError());
        listener_ = socket;
        BOOL exclusive = TRUE;
        int size = sizeof(endpoint);
        if (!SetNonBlocking(socket) ||
            setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR ||
            bind(socket, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) == SOCKET_ERROR ||
            listen(socket, SOMAXCONN) == SOCKET_ERROR ||
            getsockname(socket, reinterpret_cast<sockaddr*>(&endpoint), &size) == SOCKET_ERROR)
        {
            const int code = WSAGetLastError();
            SocketError(code == WSAEADDRNOTAVAIL
                ? "Esa IPv4 no pertenece a tu PC. En Crear sala, us\u00e1 la IPv4 local de tu PC."
                : "No se pudo abrir el puerto de la sala.", code);
            Close();
            return false;
        }

        Secret secret{};
        if (BCryptGenRandom(nullptr, secret.data(), ULONG(secret.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        {
            error_ = "No se pudo crear el codigo de sala.";
            Close();
            return false;
        }
        token_ = EncodeToken(secret);
        port_ = ntohs(endpoint.sin_port);
        host_ = true;
        return true;
    }

    bool Room::WaitForPeer(std::stop_token stop, bool expireInvitation)
    {
        error_.clear();
        if (!Hosting())
        {
            error_ = "Primero crea una sala.";
            return false;
        }

        Secret secret{};
        if (!DecodeToken(token_, secret))
        {
            error_ = "El codigo de sala no es valido.";
            return false;
        }

        const auto deadline = expireInvitation ? Clock::now() + kInviteTimeout : Clock::time_point::max();
        for (;;)
        {
            const Wait result = Ready(listener_, false, deadline, stop);
            if (result != Wait::Ready)
            {
                if (result == Wait::Failed) return SocketError("No se pudo recibir la conexion.", WSAGetLastError());
                error_ = result == Wait::Cancelled ? "Conexion cancelada." : "Se agoto el tiempo de espera de la sala.";
                return false;
            }
            sockaddr_in endpoint{};
            int endpointSize = sizeof(endpoint);
            const SOCKET socket = accept(listener_, reinterpret_cast<sockaddr*>(&endpoint), &endpointSize);
            if (socket == INVALID_SOCKET)
            {
                const int code = WSAGetLastError();
                if (code == WSAEWOULDBLOCK || code == WSAECONNRESET) continue;
                return SocketError("No se pudo recibir la conexion.", code);
            }
            peer_ = socket;
            if (!ConfigurePeer(socket))
            {
                ClosePeer();
                continue;
            }

            const auto handshakeDeadline = std::min(deadline, Clock::now() + kHandshakeTimeout);
            Handshake request{};
            Handshake response = MakeHandshake(kHostRole, mode_);
            if (Transfer(request, false, handshakeDeadline, stop) && ValidHandshake(request, kGuestRole, mode_) &&
                SameSecret(std::span(request).subspan(8), secret) && Transfer(response, true, handshakeDeadline, stop) &&
                ReceiveName(peerName_, handshakeDeadline, stop) && SendName(handshakeDeadline, stop))
            {
                closesocket(listener_);
                listener_ = kInvalidSocket;
                peerAddress_ = AddressText(endpoint);
                connected_ = true;
                error_.clear();
                return true;
            }
            ClosePeer();
        }
    }

    bool Room::Join(std::string_view address, uint16_t port, std::string_view token, std::stop_token stop)
    {
        Close();
        error_.clear();
        Secret secret{};
        if (!DecodeToken(token, secret))
        {
            error_ = "El codigo de sala debe tener 32 caracteres hexadecimales.";
            return false;
        }
        if (!StartWinsock()) return false;

        sockaddr_in endpoint{};
        if (!port || !Address(address, port, endpoint))
        {
            error_ = "El c\u00f3digo debe incluir una IPv4 v\u00e1lida del anfitri\u00f3n y su puerto TCP.";
            return false;
        }

        const SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == INVALID_SOCKET) return SocketError("No se pudo abrir la conexion.", WSAGetLastError());
        peer_ = socket;
        if (!ConfigurePeer(socket))
        {
            SocketError("No se pudo configurar la conexion.", WSAGetLastError());
            ClosePeer();
            return false;
        }

        if (connect(socket, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) == SOCKET_ERROR)
        {
            const int code = WSAGetLastError();
            if (code != WSAEWOULDBLOCK)
            {
                SocketError("No se pudo conectar con la sala.", code);
                ClosePeer();
                return false;
            }
            const Wait result = Ready(socket, true, Clock::now() + kConnectTimeout, stop);
            int connectionError = 0;
            int size = sizeof(connectionError);
            if (result != Wait::Ready)
            {
                if (result == Wait::Failed) SocketError("No se pudo conectar con la sala.", WSAGetLastError());
                else if (result == Wait::Cancelled) error_ = "Conexi\u00f3n cancelada.";
                else error_ = kConnectTimeoutMessage;
            }
            else if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&connectionError), &size) == SOCKET_ERROR)
                SocketError("No se pudo comprobar la conexion.", WSAGetLastError());
            else if (connectionError)
                SocketError("No se pudo conectar con la sala.", connectionError);
            if (!error_.empty())
            {
                ClosePeer();
                return false;
            }
        }

        auto request = MakeHandshake(kGuestRole, mode_);
        std::ranges::copy(secret, request.begin() + 8);
        Handshake response{};
        const auto deadline = Clock::now() + kHandshakeTimeout;
        if (!Transfer(request, true, deadline, stop) || !Transfer(response, false, deadline, stop))
        {
            if (!stop.stop_requested()) error_ += " Revisen el c\u00f3digo y que ambos usen el mismo modo y versi\u00f3n.";
            ClosePeer();
            return false;
        }
        if (!ValidHandshake(response, kHostRole, mode_))
        {
            error_ = "La sala respondi\u00f3 con otro protocolo. Ambos deben usar el mismo modo y versi\u00f3n de PES.";
            ClosePeer();
            return false;
        }
        if (!SendName(deadline, stop) || !ReceiveName(peerName_, deadline, stop))
        {
            ClosePeer();
            return false;
        }
        port_ = port;
        token_ = EncodeToken(secret);
        peerAddress_ = AddressText(endpoint);
        connected_ = true;
        return true;
    }

    bool Room::SendName(Clock::time_point deadline, std::stop_token stop)
    {
        std::array<uint8_t, 1 + kMaxNameBytes> message{};
        message[0] = static_cast<uint8_t>(localName_.size());
        std::memcpy(message.data() + 1, localName_.data(), localName_.size());
        return Transfer(std::span(message).first(1 + localName_.size()), true, deadline, stop);
    }

    bool Room::ReceiveName(std::string& name, Clock::time_point deadline, std::stop_token stop)
    {
        std::array<uint8_t, 1> length{};
        if (!Transfer(length, false, deadline, stop))
            return false;

        std::array<uint8_t, kMaxNameBytes> message{};
        if (length[0] > message.size())
        {
            error_ = "El apodo del otro jugador es demasiado largo.";
            return false;
        }
        if (!Transfer(std::span(message).first(length[0]), false, deadline, stop))
            return false;

        const std::string_view utf8(reinterpret_cast<const char*>(message.data()), length[0]);
        name = PlayerNameUtf8(PlayerNameFromUtf8(utf8));
        if (!utf8.empty() && name.empty())
        {
            error_ = "El apodo del otro jugador no es valido.";
            return false;
        }
        return true;
    }

    bool Room::ExchangeInput(uint32_t frame, const PadInput& local, PadInput& remote, std::stop_token stop)
    {
        error_.clear();
        if (!Connected() || mode_ != Mode::Input)
        {
            error_ = "La sala no esta conectada.";
            return false;
        }
        auto outgoing = MakeInput(frame, local);
        InputMessage incoming{};
        const auto deadline = Clock::now() + kInputTimeout;
        if (!Transfer(outgoing, true, deadline, stop) || !Transfer(incoming, false, deadline, stop))
        {
            ClosePeer();
            return false;
        }
        if (!ParseInput(incoming, frame, remote))
        {
            error_ = "Los mandos recibieron cuadros de entrada diferentes.";
            ClosePeer();
            return false;
        }
        return true;
    }

    bool Room::Ping(Clock::time_point deadline, std::stop_token stop, std::chrono::microseconds& roundTrip)
    {
        auto request = MakePing(kPingTag, latencySequence_);
        PingMessage response{};
        const auto sent = Clock::now();
        if (!Transfer(request, true, deadline, stop) || !Transfer(response, false, deadline, stop))
            return false;
        const auto elapsed = Clock::now() - sent;
        if (response != MakePing(kPongTag, latencySequence_))
        {
            error_ = "La otra computadora envio una respuesta de ping invalida.";
            return false;
        }
        roundTrip = std::chrono::ceil<std::chrono::microseconds>(elapsed);
        return true;
    }

    bool Room::AnswerPing(Clock::time_point deadline, std::stop_token stop)
    {
        PingMessage request{};
        if (!Transfer(request, false, deadline, stop))
            return false;
        if (request != MakePing(kPingTag, latencySequence_))
        {
            error_ = "La otra computadora envio una medicion de ping invalida.";
            return false;
        }
        auto response = MakePing(kPongTag, latencySequence_);
        return Transfer(response, true, deadline, stop);
    }

    bool Room::MeasureLatency(std::chrono::microseconds& roundTrip, std::stop_token stop)
    {
        error_.clear();
        if (!Connected() || mode_ != Mode::Latency)
        {
            error_ = "La medicion de ping necesita una sala conectada.";
            return false;
        }

        if (IsHost() && Clock::now() < nextProbe_)
        {
            std::mutex mutex;
            std::unique_lock lock(mutex);
            std::condition_variable_any wake;
            wake.wait_until(lock, stop, nextProbe_, [] { return false; });
        }
        const auto timeout = Clock::now() + kPingTimeout;
        const auto initialWait = IsHost() ? timeout : timeout + kPingInterval;
        const bool measured = IsHost()
            ? Ping(initialWait, stop, roundTrip) && AnswerPing(Clock::now() + kPingTimeout, stop)
            : AnswerPing(initialWait, stop) && Ping(Clock::now() + kPingTimeout, stop, roundTrip);
        if (!measured)
        {
            ClosePeer();
            return false;
        }
        ++latencySequence_;
        nextProbe_ = Clock::now() + kPingInterval;
        return true;
    }

    bool Room::PeerClosed() const
    {
        if (!Connected()) return true;
        char next = 0;
        const int count = recv(peer_, &next, 1, MSG_PEEK);
        if (count == SOCKET_ERROR) return WSAGetLastError() != WSAEWOULDBLOCK;
        return count == 0;
    }

    bool Room::Transfer(std::span<uint8_t> bytes, bool sending, Clock::time_point deadline, std::stop_token stop)
    {
        while (!bytes.empty())
        {
            const Wait result = Ready(peer_, sending, deadline, stop);
            if (result != Wait::Ready)
            {
                if (result == Wait::Failed) return SocketError("La conexion fallo.", WSAGetLastError());
                error_ = result == Wait::Cancelled ? "Conexion cancelada." : "La otra computadora no respondio.";
                return false;
            }
            const int count = sending
                ? send(peer_, reinterpret_cast<const char*>(bytes.data()), int(bytes.size()), 0)
                : recv(peer_, reinterpret_cast<char*>(bytes.data()), int(bytes.size()), 0);
            if (count == SOCKET_ERROR)
            {
                const int code = WSAGetLastError();
                if (code == WSAEWOULDBLOCK) continue;
                return SocketError("La conexion fallo.", code);
            }
            if (!count)
            {
                error_ = "La otra computadora se desconecto.";
                return false;
            }
            bytes = bytes.subspan(size_t(count));
        }
        return true;
    }

    void Room::ClosePeer()
    {
        if (peer_ != kInvalidSocket)
        {
            closesocket(peer_);
            peer_ = kInvalidSocket;
        }
        connected_ = false;
        peerName_.clear();
        latencySequence_ = 0;
        nextProbe_ = {};
    }

    void Room::Close()
    {
        ClosePeer();
        if (listener_ != kInvalidSocket)
        {
            closesocket(listener_);
            listener_ = kInvalidSocket;
        }
        host_ = false;
        port_ = 0;
        token_.clear();
        peerAddress_.clear();
    }
}
