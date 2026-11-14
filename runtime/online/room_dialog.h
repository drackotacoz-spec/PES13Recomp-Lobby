#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <windows.h>

namespace Online
{
    class Room;

    void ToggleRoomDialog(HWND gameWindow, bool experimental = false);
    bool HandleRoomDialogMessage(MSG& message);

    std::shared_ptr<Room> ConnectedRoom();
    std::shared_ptr<Room> ConnectedInputRoom();
    std::wstring RoomPlayerName(bool local);
    void ReportRoomFailure(const std::shared_ptr<Room>& room, std::string_view reason);
}
