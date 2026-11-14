#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <windows.h>

namespace Online
{
    class Room;

    void ToggleRoomDialog(HWND gameWindow);
    bool HandleRoomDialogMessage(MSG& message);

    std::shared_ptr<Room> ConnectedRoom();
    std::wstring RoomPlayerName(bool local);
    void ReportRoomFailure(const std::shared_ptr<Room>& room, std::string_view reason);
}
