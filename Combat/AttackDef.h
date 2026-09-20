#pragma once

#include "Combat/DamageTypes.h"

#include <cstdint>

namespace Dark::Combat
{

    struct AttackDef
    {
        const char* name         = "Attack";
        float       damage       = 16.0f;
        DamageType  type         = DamageType::Slash;
        float       poiseDamage  = 12.0f;
        float       armorPen     = 0.0f;
        float       staminaCost  = 0.0f;
        float       knockback    = 1.0f; // scale hint for severity table
        float       hitstop      = 0.05f;
        int         maxTargets   = 8;
        float       multiHitInterval = 0.0f; // 0 = once per Active
        uint8_t     armorLevelWhileAttacking = 0;
        uint32_t    flags        = 0; // DamageFlags::*
        uint8_t     teamFilter   = 0;
        uint8_t     statusId        = 0;
        float       statusDuration  = 0.f;
        float       statusMagnitude = 0.f;
    };

} // namespace Dark::Combat
