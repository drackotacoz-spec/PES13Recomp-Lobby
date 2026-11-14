#pragma once

#include <windows.h>

namespace Online
{
    void ToggleChatComposer(HWND gameWindow);
    void TickChatOverlay(HWND gameWindow);
    bool HandleChatComposerMessage(MSG& message);
}
