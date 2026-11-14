#include "frame_probe.h"

#include "room.h"
#include "room_dialog.h"

#include <guest_memory.h>
#include <ppc_recomp_shared.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace
{
    // The outer update increments this field, which the game may reset before
    // the next callback. It is useful for tracing, not for numbering packets.
    constexpr uint32_t kFrameObject = 0x83507CA8;
    constexpr uint32_t kFrameOffset = 24;
    constexpr uint32_t kMatchTicksRemaining = 0x836F6F74;
    constexpr uint32_t kMatchTicksAtStart = 0x836F6F7C;
    constexpr uint32_t kGameRandom = 0x8350886E;

    struct FrameInputs
    {
        std::weak_ptr<Online::Room> room;
        std::array<Online::PadInput, 2> pads{};
        std::array<uint32_t, 2> packets{};
        uint32_t sequence = 0;
        bool active = false;
    };

    thread_local FrameInputs g_frame;

    uint32_t GuestFrameCount()
    {
        const uint32_t object = Memory::Load32(kFrameObject);
        return object ? Memory::Load32(object + kFrameOffset) : 0;
    }

    void SetPad(size_t user, const Online::PadInput& input)
    {
        if (g_frame.pads[user] != input)
        {
            g_frame.pads[user] = input;
            ++g_frame.packets[user];
        }
    }

    void ExchangeFrameInputs(const std::shared_ptr<Online::Room>& room)
    {
        if (g_frame.room.lock() != room)
        {
            g_frame = {};
            g_frame.room = room;
        }
        const auto local = Online::ReadLocalPad();
        Online::PadInput remote{};
        if (!room->ExchangeInput(g_frame.sequence, local, remote))
        {
            Online::ReportRoomFailure(room, room->Error());
            return;
        }

        const size_t localSlot = room->IsHost() ? 0 : 1;
        SetPad(localSlot, local);
        SetPad(1 - localSlot, remote);
        ++g_frame.sequence;
        g_frame.active = true;
    }
}

namespace Online
{
    void RunGuestFrame(PPCContext& ctx, uint8_t* base)
    {
        static const bool sync = std::getenv("PES_EXPERIMENTAL_SYNC") != nullptr;
        static FILE* trace = []() -> FILE* {
            const char* path = std::getenv("PES_FRAME_TRACE");
            FILE* file = path && *path ? std::fopen(path, "w") : nullptr;
            if (file)
                std::fputs("guest_frame_field_before,guest_frame_field_after,sequence,exchanged,home_buttons,away_buttons,match_start,match_remaining,game_random\n", file);
            return file;
        }();
        const uint32_t before = trace ? GuestFrameCount() : 0;
        if (sync)
        {
            if (const auto room = ConnectedRoom())
                ExchangeFrameInputs(room);
        }
        sub_821B0EA8(ctx, base);
        const bool exchanged = g_frame.active;
        g_frame.active = false;
        if (trace)
        {
            std::fprintf(trace, "%u,%u,%u,%u,%04x,%04x,%u,%u,%u\n",
                before, GuestFrameCount(), g_frame.sequence, unsigned(exchanged),
                unsigned(g_frame.pads[0].buttons), unsigned(g_frame.pads[1].buttons),
                Memory::Load32(kMatchTicksAtStart), Memory::Load32(kMatchTicksRemaining), unsigned(Memory::Load8(kGameRandom)));
            std::fflush(trace);
        }
    }

    bool ReadFramePad(uint32_t user, PadInput& pad, uint32_t& packet)
    {
        if (!g_frame.active || user >= g_frame.pads.size()) return false;
        pad = g_frame.pads[user];
        packet = g_frame.packets[user];
        return true;
    }
}
