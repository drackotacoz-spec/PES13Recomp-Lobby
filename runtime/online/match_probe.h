#pragma once

#include <ppc_context.h>

namespace Online
{
    PPCFunc* MatchProbeHook(uint32_t guestAddress, PPCFunc* original);
}
