#pragma once

#include <ppc_context.h>

namespace Online
{
    void TraceMatchCall(uint32_t guestAddress, PPCFunc* original, PPCContext& ctx, uint8_t* base);
}
