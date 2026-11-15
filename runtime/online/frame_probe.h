#pragma once

#include <cstdint>

struct PPCContext;

namespace Online
{
    struct PadInput;

    void RunGuestFrame(PPCContext& ctx, uint8_t* base);
    void ClearExhibitionTeams();
    void ConfirmExhibitionTeams();
    bool ReadFramePad(uint32_t user, PadInput& pad, uint32_t& packet);
    // Menus can poll outside the synchronized frame. Both slots are present
    // with neutral input until the current room publishes its first frame.
    bool ReadLatestRoomPad(uint32_t user, PadInput& pad, uint32_t& packet);
    PadInput ReadLocalPad();
}
