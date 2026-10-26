#pragma once

#include "AI/Awareness.h"

namespace Dark
{

    // Alert, Searching, or a visual notice (Suspicious and not heard-only) abort the walk.
    // Investigating is the power-check task and must not abort it.
    inline bool powerCheckerShouldClear(AI::Alertness alertness, bool heardOnly)
    {
        if (alertness == AI::Alertness::Alert || alertness == AI::Alertness::Searching)
            return true;
        return alertness == AI::Alertness::Suspicious && !heardOnly;
    }

} // namespace Dark
