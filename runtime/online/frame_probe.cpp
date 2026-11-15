#include "frame_probe.h"
#include "network_trial.h"

#include "room.h"
#include "room_dialog.h"

#include <guest_memory.h>
#include <ppc_recomp_shared.h>
#include <gpu/gpu.h>

#include <array>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>

namespace
{
    // The outer update increments this field, which the game may reset before
    // the next callback. It is useful for tracing, not for numbering packets.
    constexpr uint32_t kFrameObject = 0x83507CA8;
    constexpr uint32_t kFrameOffset = 24;
    constexpr uint32_t kMatchTicksRemaining = 0x836F6F74;
    constexpr uint32_t kMatchTicksAtStart = 0x836F6F7C;
    constexpr uint32_t kGameRandom = 0x8350886E;

    struct RoomPads
    {
        std::array<Online::PadInput, 2> pads{};
        std::array<uint32_t, 2> packets{};
    };

    struct FrameInputs
    {
        std::weak_ptr<Online::Room> room;
        RoomPads input;
        uint32_t sequence = 0;
        bool active = false;
    };

    thread_local FrameInputs g_frame;
    std::mutex g_latestLock;
    std::map<std::weak_ptr<Online::Room>, RoomPads, std::owner_less<>> g_latestPads;

    uint32_t GuestFrameCount()
    {
        const uint32_t object = Memory::Load32(kFrameObject);
        return object ? Memory::Load32(object + kFrameOffset) : 0;
    }

    void SetPad(size_t user, const Online::PadInput& input)
    {
        auto& state = g_frame.input;
        if (state.pads[user] != input)
        {
            state.pads[user] = input;
            ++state.packets[user];
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
        {
            std::lock_guard lock(g_latestLock);
            // An old in-flight frame cannot replace a new room's inputs.
            std::erase_if(g_latestPads, [](const auto& entry) { return entry.first.expired(); });
            g_latestPads.insert_or_assign(room, g_frame.input);
        }
    }
}

namespace Online
{
    void RunGuestFrame(PPCContext& ctx, uint8_t* base)
    {
        FILE* trace = NetworkTrialLog(TrialLog::Frames);
        const uint32_t before = trace ? GuestFrameCount() : 0;
        if (NetworkTrialInputEnabled())
        {
            if (const auto room = ConnectedInputRoom())
                ExchangeFrameInputs(room);
        }
        sub_821B0EA8(ctx, base);
        const bool exchanged = g_frame.active;
        g_frame.active = false;
        if (trace)
        {
            std::fprintf(trace, "%u,%u,%u,%u,%04x,%04x,%u,%u,%u,%llu\n",
                before, GuestFrameCount(), g_frame.sequence, unsigned(exchanged),
                unsigned(g_frame.input.pads[0].buttons), unsigned(g_frame.input.pads[1].buttons),
                Memory::Load32(kMatchTicksAtStart), Memory::Load32(kMatchTicksRemaining), unsigned(Memory::Load8(kGameRandom)),
                static_cast<unsigned long long>(Gpu::FrameCount()));
            std::fflush(trace);
        }
    }

    bool ReadFramePad(uint32_t user, PadInput& pad, uint32_t& packet)
    {
        if (!g_frame.active || user >= g_frame.input.pads.size()) return false;
        pad = g_frame.input.pads[user];
        packet = g_frame.input.packets[user];
        return true;
    }

    bool ReadLatestRoomPad(uint32_t user, PadInput& pad, uint32_t& packet)
    {
        if (user >= 2 || !NetworkTrialInputEnabled()) return false;
        const auto room = ConnectedInputRoom();
        if (!room) return false;

        std::lock_guard lock(g_latestLock);
        const auto latest = g_latestPads.find(room);
        pad = latest == g_latestPads.end() ? PadInput{} : latest->second.pads[user];
        packet = latest == g_latestPads.end() ? 0 : latest->second.packets[user];
        return true;
    }
}
