#pragma once

#include <string>
#include <windows.h>

namespace Online
{
    void ToggleLobbyDialog(HWND gameWindow, std::wstring name = {});
    bool HandleLobbyDialogMessage(MSG& message);
}
