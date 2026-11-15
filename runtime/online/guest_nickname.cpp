#include "guest_nickname.h"
#include "player_profile.h"

#include <guest_memory.h>
#include <kernel/thread.h>

#include "room_dialog.h"
#include "room.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" PPC_FUNC(__imp__sub_824476B8);

namespace
{
    // sub_823484C0 sends the text to the controller row's persistent label.
    // Its nonvolatile r31 holds that row; +240 is the controller index.
    constexpr uint32_t kControllerLabel = 0x82348640;
    constexpr uint32_t kSelectSideScene = 0x82416DF4;
    constexpr uint32_t kControllerIndexOffset = 240;
    constexpr size_t kGuestLabelBytes = 160;

    bool IsSelectSideScene(const PPCContext& ctx)
    {
        const auto* thread = Kernel::CurrentGuestThread();
        if (!thread) return false;
        uint32_t frame = ctx.r1.u32;
        for (int depth = 0; depth < 12; ++depth)
        {
            if (frame < thread->stackLimit || frame >= thread->stackBase) break;
            const uint32_t previous = Memory::Load32(frame);
            if (previous < frame + 8 || previous >= thread->stackBase) break;
            if (Memory::Load32(previous - 8) == kSelectSideScene) return true;
            frame = previous;
        }
        return false;
    }

    uint32_t NicknameForController(uint32_t controller)
    {
        const auto room = Online::ConnectedInputRoom();
        const bool local = controller == (room && !room->IsHost() ? 1u : 0u);
        auto name = Online::RoomPlayerName(local);
        if (local && name.empty()) name = Online::LoadPlayerName();
        const auto nickname = Online::PlayerNameUtf8(name);
        if (nickname.empty() || nickname.size() >= kGuestLabelBytes) return 0;

        // The guest duplicates the string in its own layout before returning.
        // Per-thread buffers remain valid for successive rows of the same menu.
        thread_local uint32_t labels[2]{};
        auto& address = labels[controller];
        if (!address)
            address = Memory::AllocKernel(kGuestLabelBytes);
        if (address)
            std::memcpy(Memory::Translate(address), nickname.c_str(), nickname.size() + 1);
        return address;
    }
}

namespace Online
{
    void SetGuestText(PPCContext& ctx, uint8_t* base)
    {
        if (ctx.lr == kControllerLabel && ctx.r4.u32 && ctx.r31.u32 && IsSelectSideScene(ctx))
        {
            const auto controller = Memory::Load8(ctx.r31.u32 + kControllerIndexOffset);
            if (controller < 2)
            {
                const auto nickname = NicknameForController(controller);
                static const bool trace = std::getenv("PES_NICKNAME_TRACE") != nullptr;
                if (trace)
                    std::fprintf(stderr, "[nickname] controller=%u substituted=%d\n", unsigned(controller), nickname != 0);
                if (nickname)
                    ctx.r4.u64 = nickname;
            }
        }
        __imp__sub_824476B8(ctx, base);
    }
}
