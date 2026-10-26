#pragma once

#include "ECS/Entity.h"
#include "ECS/Persist.h"

namespace Dark
{

    class World;

    // Torch on a guard. The numbers are the Sandbox spawn defaults.
    struct AttachedLightComponent
    {
        static constexpr const char* kTypeName    = "AttachedLight";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        Entity owner{};
        bool   nightOnly     = true;
        float  intensity     = 1300.0f;
        float  range         = 18.0f;
        float  innerConeDeg  = 12.0f;
        float  outerConeDeg  = 26.0f;
        float  height        = 0.75f;
        float  forward       = 0.35f;
        float  aimDrop       = 0.28f;
    };

    // nightOnly lights turn on when the sun elevation is below 0.15.
    void tickAttachedLights(World& world, float sunElevation);

} // namespace Dark
