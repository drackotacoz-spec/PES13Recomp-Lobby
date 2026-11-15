#include "frame_probe.h"
#include "network_trial.h"

#include "room.h"
#include "room_dialog.h"

#include <guest_memory.h>
#include <ppc_recomp_shared.h>
#include <gpu/gpu.h>

#include <windows.h>

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
    // ES/PT exhibition uses the +2 halfword of each descriptor when resolving the confirmed teams.
    constexpr uint32_t kExhibitionTeams = 0x83507ED0;
    constexpr uint32_t kSelectedTeam = 2;

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
        uint32_t matchTeams = 0;
        bool active = false;
    };

    thread_local FrameInputs g_frame;
    std::mutex g_latestLock;
    std::map<std::weak_ptr<Online::Room>, RoomPads, std::owner_less<>> g_latestPads;
    std::weak_ptr<Online::Room> g_confirmedRoom;
    uint32_t g_confirmedTeams = 0;

    uint32_t GuestFrameCount()
    {
        const uint32_t object = Memory::Load32(kFrameObject);
        return object ? Memory::Load32(object + kFrameOffset) : 0;
    }

    bool ReadableGuestRange(uint32_t address, size_t length)
    {
        if (!address || uint64_t(address) + length > (uint64_t{1} << 32)) return false;
        const auto* pointer = Memory::Translate(address);
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(pointer, &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;
        const auto offset = reinterpret_cast<uintptr_t>(pointer) - reinterpret_cast<uintptr_t>(info.BaseAddress);
        return offset < info.RegionSize && length <= info.RegionSize - offset;
    }

    uint32_t ReadMatchTeams()
    {
        const auto teams = Memory::Load32(kExhibitionTeams);
        if (!ReadableGuestRange(teams, 2 * sizeof(uint32_t))) return 0;
        const auto local = Memory::Load32(teams);
        const auto visitor = Memory::Load32(teams + sizeof(uint32_t));
        constexpr auto descriptorBytes = kSelectedTeam + sizeof(uint16_t);
        if (!ReadableGuestRange(local, descriptorBytes) || !ReadableGuestRange(visitor, descriptorBytes)) return 0;
        const auto home = Memory::Load16(local + kSelectedTeam);
        const auto away = Memory::Load16(visitor + kSelectedTeam);
        const auto concrete = [](uint16_t id) { return id && id != 0xffff && id != 4096 && id != 4097; };
        return concrete(home) && concrete(away) ? (uint32_t(home) << 16) | away : 0;
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
        const auto sample = Online::ReadLocalPad();
        Online::PadInput local{}, remote{};
        {
            std::lock_guard lock(g_latestLock);
            if (g_confirmedRoom.lock() == room) g_frame.matchTeams = g_confirmedTeams;
        }
        if (!room->ExchangeInput(g_frame.sequence, sample, local, remote, {}, g_frame.matchTeams))
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
    void ClearExhibitionTeams()
    {
        std::lock_guard lock(g_latestLock);
        g_confirmedRoom.reset();
        g_confirmedTeams = 0;
    }

    void ConfirmExhibitionTeams()
    {
        const auto room = ConnectedInputRoom();
        if (!room) return;
        const auto teams = ReadMatchTeams();
        std::lock_guard lock(g_latestLock);
        g_confirmedRoom = room;
        g_confirmedTeams = teams;
    }

    void RunGuestFrame(PPCContext& ctx, uint8_t* base)
    {
        FILE* trace = NetworkTrialLog(TrialLog::Frames);
        const uint32_t before = trace ? GuestFrameCount() : 0;
        g_frame.matchTeams = 0;
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
            std::fprintf(trace, "%u,%u,%u,%u,%04x,%04x,%u,%u,%u,%llu,%u,%u\n",
                before, GuestFrameCount(), g_frame.sequence, unsigned(exchanged),
                unsigned(g_frame.input.pads[0].buttons), unsigned(g_frame.input.pads[1].buttons),
                Memory::Load32(kMatchTicksAtStart), Memory::Load32(kMatchTicksRemaining), unsigned(Memory::Load8(kGameRandom)),
                static_cast<unsigned long long>(Gpu::FrameCount()),
                g_frame.matchTeams >> 16, g_frame.matchTeams & 0xffff);
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
