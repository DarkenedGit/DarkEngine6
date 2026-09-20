#pragma once

#include "Combat/DamageTypes.h"

#include <cstdint>

namespace Dark::Combat
{

    enum class SpellTargeting : uint8_t
    {
        Self = 0,
        Ally,
        Enemy,
        GroundAoE,
        Projectile,
        Cone
    };

    enum class SpellInterruptPolicy : uint8_t
    {
        Never = 0,
        SoftCc,
        HardCc,
        DamageOver
    };

    enum class SpellPhase : uint8_t
    {
        Idle = 0,
        Windup,
        Channel,
        Release,
        Recovery
    };

    struct SpellDef
    {
        const char*          id               = "Spell";
        SpellTargeting       targeting        = SpellTargeting::Projectile;
        float                windupSeconds    = 0.35f;
        float                channelSeconds   = 0.0f;
        float                releaseSeconds   = 0.05f;
        float                recoverySeconds  = 0.25f;
        float                manaCost         = 20.0f;
        float                staminaCost      = 0.0f;
        float                cooldown         = 1.0f;
        float                range            = 40.0f;
        float                projectileSpeed  = 28.0f;
        float                damage           = 22.0f;
        DamageType           damageType       = DamageType::Fire;
        float                poiseDamage      = 8.0f;
        float                armorPen         = 0.0f;
        uint32_t             damageFlags      = 0;
        uint8_t              statusId         = 0;
        float                statusDuration   = 0.f;
        float                statusMagnitude  = 0.f;
        SpellInterruptPolicy interruptPolicy = SpellInterruptPolicy::HardCc;
        float                damageInterruptThreshold = 0.0f; // used if DamageOver
        bool                 commitCostOnRelease      = true;
    };

} // namespace Dark::Combat
