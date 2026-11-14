#pragma once

#include <cstdio>

namespace Online
{
    enum class TrialLog { Frames, Gamepads, Match };

    bool PrepareNetworkTrial();
    bool NetworkTrialInputEnabled();
    FILE* NetworkTrialLog(TrialLog kind);
}
