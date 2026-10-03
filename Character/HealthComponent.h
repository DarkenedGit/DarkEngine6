#pragma once

#include "Character/Health.h"
#include "Character/HitReaction.h"
#include "ECS/Persist.h"

namespace Dark
{

    struct HealthComponent
    {
        static constexpr const char* kTypeName    = "Health";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        Health health;
    };

    struct HitReactionComponent
    {
        static constexpr const char* kTypeName    = "HitReaction";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        HitReaction hit;
    };

} // namespace Dark
