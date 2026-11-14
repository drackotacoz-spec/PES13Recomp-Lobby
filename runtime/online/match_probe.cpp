#include "match_probe.h"
#include "network_trial.h"

#include <gpu/gpu.h>
#include <guest_memory.h>
#include <kernel/thread.h>

#include <array>
#include <atomic>
#include <cstdio>

namespace
{
    // Addresses from the European ES/PT XEX (Media ID 183C6A2F, 0.0.0.8).
    // The manager initializes gameWork and GameStep in sub_82883C50. The
    // elapsed-time getter sub_82881770 reads the two clock values below.
    constexpr uint32_t kManagerPointer = 0x8350AED0;
    constexpr uint32_t kGameStepCounter = 0x836F6504;
    // sub_8286C030 skips the player and ball updates when this flag is clear.
    constexpr uint32_t kGameplayStepEnabled = 0x836F6539;
    constexpr uint32_t kClockRemaining = 0x836F6F74;
    constexpr uint32_t kClockStart = 0x836F6F7C;
    constexpr uint32_t kGameRandom = 0x8350886E;
    constexpr uint32_t kPeriod = 0x836F6F70;

    struct MatchData
    {
        uint32_t manager;
        uint32_t step;
        uint32_t remaining;
        uint32_t start;
        unsigned random;
        unsigned period;
        unsigned gameplay;

        uint64_t Fingerprint() const
        {
            // Manager pointers, guest thread IDs and rendered frame numbers can differ between processes.
            const std::array<uint32_t, 6> words{step, remaining, start, random, period, gameplay};
            uint64_t hash = 14695981039346656037ull;
            for (const auto word : words)
                for (unsigned shift = 0; shift < 32; shift += 8)
                    hash = (hash ^ ((word >> shift) & 0xff)) * 1099511628211ull;
            return hash;
        }
    };

    MatchData SampleMatch()
    {
        return {
            Memory::Load32(kManagerPointer),
            Memory::Load32(kGameStepCounter),
            Memory::Load32(kClockRemaining),
            Memory::Load32(kClockStart),
            Memory::Load8(kGameRandom),
            Memory::Load8(kPeriod),
            Memory::Load8(kGameplayStepEnabled),
        };
    }

    std::atomic<uint64_t> g_nextCall{1};
    thread_local uint64_t g_activeCall = 0;
}

namespace Online
{
    void TraceMatchCall(uint32_t guestAddress, PPCFunc* original, PPCContext& ctx, uint8_t* base)
    {
        FILE* trace = NetworkTrialLog(TrialLog::Match);
        if (!trace)
        {
            original(ctx, base);
            return;
        }

        const uint64_t call = g_nextCall.fetch_add(1, std::memory_order_relaxed);
        const uint64_t parent = g_activeCall;
        g_activeCall = call;
        const auto caller = uint32_t(ctx.lr);
        const auto* thread = Kernel::CurrentGuestThread();
        const auto threadId = thread ? thread->threadId : 0;
        const auto gpuFrame = static_cast<unsigned long long>(Gpu::FrameCount());
        const MatchData before = SampleMatch();
        original(ctx, base);
        const MatchData after = SampleMatch();
        g_activeCall = parent;
        std::fprintf(trace, "%08x,%u,%08x,%llu,%08x,%u,%u,%u,%u,%u,%08x,%u,%u,%u,%u,%u,%08x,"
            "%llu,%llu,%u,%u,%016llx,%016llx\n",
            guestAddress, threadId, caller, gpuFrame,
            before.manager, before.step, before.remaining, before.start, before.random, before.period,
            after.manager, after.step, after.remaining, after.start, after.random, after.period, ctx.r3.u32,
            static_cast<unsigned long long>(call), static_cast<unsigned long long>(parent),
            before.gameplay, after.gameplay, static_cast<unsigned long long>(before.Fingerprint()),
            static_cast<unsigned long long>(after.Fingerprint()));
        std::fflush(trace);
    }
}
