#pragma once

#include "Combat/DamageEvent.h"
#include "Combat/DamageTypes.h"

#include <cstdint>

namespace Dark::Combat
{

    struct JumpAttackDef
    {
        const char* name = "JumpAttack";

        float gravity             = 24.0f;
        float minHeight           = 0.45f;
        float minAirTime          = 0.10f;
        float leapForwardSpeed    = 10.0f;
        float leapForwardSpeedMax = 12.0f;
        float leapVerticalSpeed   = 10.0f;
        float airControlScale     = 0.35f;
        float homingRate          = 6.0f;
        float connectSnapSeconds  = 0.08f;
        float telegraphSeconds    = 0.0f;
        float groundOffset        = 0.5f;

        float connectWindow       = 0.55f;
        float connectRange        = 2.8f;
        float connectVerticalSlop = 1.5f;
        float connectMinDot       = 0.25f;

        float      connectDamage     = 32.0f;
        float      connectPoise      = 28.0f;
        float      knockdownDuration = 1.4f;
        float      knockdownForce    = 2.4f;
        DamageType connectType       = DamageType::Blunt;
        uint32_t   connectFlags      = DamageFlags::CanBlock | DamageFlags::HardCc | (1u << 7); // Knockdown bit; named DamageFlags::Knockdown in PR2

        float      poundRadius       = 3.5f;
        float      poundVerticalSlop = 2.0f;
        float      poundDamage       = 20.0f;
        float      poundPoise        = 16.0f;
        float      poundStunDuration = 0.85f;
        int        poundMaxTargets   = 8;
        DamageType poundType         = DamageType::Blunt;
        uint32_t   poundFlags        = DamageFlags::CanBlock | DamageFlags::CanParry | DamageFlags::HardCc;

        float   landLagConnected = 0.22f;
        float   landLagWhiff     = 0.40f;
        float   cooldown         = 1.25f;
        float   staminaCost      = 0.0f;
        uint8_t teamFilter       = 0;
        bool    sameTeamOk       = false;
    };

} // namespace Dark::Combat
