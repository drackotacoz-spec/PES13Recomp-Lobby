#pragma once

#include <memory>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <windows.h>

namespace Online
{
    class Room;
    namespace Lobby { enum class PublicInput; }

    struct PublicEndpoint { uint16_t port; std::string token; };

    void ToggleRoomDialog(HWND gameWindow, bool experimental = false);
    bool HandleRoomDialogMessage(MSG& message);

    std::shared_ptr<Room> ConnectedRoom();
    std::shared_ptr<Room> ConnectedInputRoom();
    std::optional<PublicEndpoint> OpenPublicInputRoom(std::wstring name, std::wstring& error);
    void JoinPublicInputRoom(PublicEndpoint endpoint, std::wstring name);
    void StopPublicInputRoom();
    bool ManualRoomActive();
    Lobby::PublicInput PublicInputStatus(std::wstring& error);
    std::wstring RoomPlayerName(bool local);
    void ReportRoomFailure(const std::shared_ptr<Room>& room, std::string_view reason);
    std::wstring TakeRoomFailureNotice();
}
