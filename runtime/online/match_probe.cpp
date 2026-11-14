#include "match_probe.h"

#include <gpu/gpu.h>
#include <guest_memory.h>
#include <kernel/thread.h>

#include <cstdio>
#include <cstdlib>

namespace
{
    // Addresses from the European ES/PT XEX (Media ID 183C6A2F, 0.0.0.8).
    // The manager initializes gameWork and GameStep in sub_82883C50. The
    // elapsed-time getter sub_82881770 reads the two clock values below.
    constexpr uint32_t kManagerPointer = 0x8350AED0;
    constexpr uint32_t kGameStepCounter = 0x836F6504;
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
        };
    }

    FILE* MatchTrace()
    {
        static FILE* trace = []() -> FILE*
        {
            const char* path = std::getenv("PES_MATCH_TRACE");
            if (!path || !*path) return nullptr;
            FILE* file = std::fopen(path, "w");
            if (!file)
            {
                std::perror("PES_MATCH_TRACE");
                return nullptr;
            }
            std::fputs("callback,guest_thread,caller,gpu_frame,manager_before,step_before,clock_before,period_start_before,rng_before,period_before,"
                       "manager_after,step_after,clock_after,period_start_after,rng_after,period_after,result\n", file);
            std::fflush(file);
            return file;
        }();
        return trace;
    }

    struct Callback
    {
        uint32_t address;
        PPCFunc* original = nullptr;
    };

    Callback g_matchInit{0x82883C50};
    Callback g_matchUpdate{0x8286B718};
    Callback g_gameStep{0x8286C030};
    Callback g_gameStepTail{0x8286C188};

    void TraceCallback(const Callback& callback, PPCContext& ctx, uint8_t* base)
    {
        const auto caller = uint32_t(ctx.lr);
        const auto* thread = Kernel::CurrentGuestThread();
        const auto gpuFrame = static_cast<unsigned long long>(Gpu::FrameCount());
        const MatchData before = SampleMatch();
        callback.original(ctx, base);
        const MatchData after = SampleMatch();
        FILE* trace = MatchTrace();
        std::fprintf(trace, "%08x,%u,%08x,%llu,%08x,%u,%u,%u,%u,%u,%08x,%u,%u,%u,%u,%u,%08x\n",
            callback.address, thread ? thread->threadId : 0, caller, gpuFrame,
            before.manager, before.step, before.remaining, before.start, before.random, before.period,
            after.manager, after.step, after.remaining, after.start, after.random, after.period, ctx.r3.u32);
        std::fflush(trace);
    }

    void ProbeInit(PPCContext& ctx, uint8_t* base) { TraceCallback(g_matchInit, ctx, base); }
    void ProbeUpdate(PPCContext& ctx, uint8_t* base) { TraceCallback(g_matchUpdate, ctx, base); }
    void ProbeStep(PPCContext& ctx, uint8_t* base) { TraceCallback(g_gameStep, ctx, base); }
    void ProbeTail(PPCContext& ctx, uint8_t* base) { TraceCallback(g_gameStepTail, ctx, base); }
}

namespace Online
{
    PPCFunc* MatchProbeHook(uint32_t guestAddress, PPCFunc* original)
    {
        if (!MatchTrace()) return original;
        if (guestAddress == g_matchInit.address)
        {
            g_matchInit.original = original;
            return ProbeInit;
        }
        if (guestAddress == g_matchUpdate.address)
        {
            g_matchUpdate.original = original;
            return ProbeUpdate;
        }
        if (guestAddress == g_gameStep.address)
        {
            g_gameStep.original = original;
            return ProbeStep;
        }
        if (guestAddress == g_gameStepTail.address)
        {
            g_gameStepTail.original = original;
            return ProbeTail;
        }
        return original;
    }
}
