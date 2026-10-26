#pragma once

#include "ECS/Entity.h"
#include "ECS/Persist.h"
#include "Math/Vector3f.h"

#include <string>

namespace Dark
{

    // Distances and times that used to be literals in the Sandbox power loop.
    struct PowerTuning
    {
        float alertRadius  = 30.0f;
        float fixSeconds   = 1.5f;
        float fixRange     = 2.5f;
        float serviceRange = 1.5f;
        float checkDwell   = 2.5f;
        float checkLimit   = 15.0f;
        int   checkCount   = 3;
    };

    inline const PowerTuning& powerTuning()
    {
        static const PowerTuning tuning{};
        return tuning;
    }

    struct PowerBoxComponent
    {
        static constexpr const char* kTypeName    = "PowerBox";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;
        static constexpr int         kCheckCount  = 3;

        std::string    circuit;
        bool           on = true;
        float          fixFor = 0.0f;
        Entity         fixer{};
        // Vector3f() does not write its floats. A default box must still save.
        Math::Vector3f service{ 0.0f, 0.0f, 0.0f };
        Entity         checker{};
        Math::Vector3f checks[kCheckCount]{
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f },
        };
        int            checkIdx  = 0;
        float          checkFor  = 0.0f;
        float          checkTime = 0.0f;
        Math::Vector3f pos{ 0.0f, 0.0f, 0.0f };
    };

    struct CircuitMemberComponent
    {
        static constexpr const char* kTypeName    = "CircuitMember";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        std::string circuit;
    };

} // namespace Dark
