#pragma once

#include "ECS/Entity.h"
#include "ECS/Persist.h"
#include "Math/Vector3f.h"

#include <cstdint>
#include <string>
#include <vector>

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

    // Facility session state on the session entity: owned upgrades, the alarm, and the hide spot the player is in.
    struct FacilityProgressComponent
    {
        static constexpr const char* kTypeName    = "FacilityProgress";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        std::vector<std::string> upgrades;
        float                    alarmTime = 0.0f;
        Entity                   hiddenIn{};
        float                    hideFound = 0.0f;
    };

    struct QuestObjectiveRecord
    {
        int  progress = 0;
        bool done     = false;
    };

    // Session-entity overlay for QuestRuntime. The quest file is reloaded by id on apply.
    struct QuestProgressComponent
    {
        static constexpr const char* kTypeName    = "QuestProgress";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        std::string                     id;
        bool                            rewardsGranted = false;
        std::vector<QuestObjectiveRecord> objectives;
    };

} // namespace Dark
