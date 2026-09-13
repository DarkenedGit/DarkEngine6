#pragma once

#include "Character/Health.h"
#include "Character/HitReaction.h"

namespace Dark
{

    struct HealthComponent
    {
        static constexpr const char* kTypeName = "Health";
        Health                       health;
    };

    struct HitReactionComponent
    {
        static constexpr const char* kTypeName = "HitReaction";
        HitReaction                  hit;
    };

} // namespace Dark
