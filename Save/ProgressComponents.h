#pragma once

#include "ECS/Entity.h"
#include "ECS/Persist.h"
#include "Math/Vector3f.h"

#include <cstdint>

namespace Dark
{
    struct LookComponent
    {
        static constexpr const char* kTypeName    = "Look";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        float yaw     = 0.0f;
        float pitch   = 0.0f;
        bool  lightOn = false;
    };

    struct WorldClockComponent
    {
        static constexpr const char* kTypeName    = "WorldClock";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        double playTimeSec  = 0.0;
        float  simTimeSec   = 0.0f;
        float  envTimeOfDay = 0.0f;
        float  cloudTime    = 0.0f;
        float  waterTime    = 0.0f;
    };

    struct AiClockComponent
    {
        static constexpr const char* kTypeName    = "AiClock";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        float  time          = 0.0f;
        float  packAttackGap = 0.0f;
        Entity token{};
    };

    struct RunProgressComponent
    {
        static constexpr const char* kTypeName    = "RunProgress";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        float          deadTimer = 0.0f;
        float          spawnAge  = 0.0f;
        bool           hasSpawn  = false;
        Math::Vector3f spawn{};
    };

} // namespace Dark
