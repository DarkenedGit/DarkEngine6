#pragma once

#include "Combat/DamageTypes.h"
#include "Combat/StatusId.h"

#include <cstdint>

namespace Dark::Combat
{

    enum class StatusStackRule : uint8_t
    {
        RefreshLonger = 0,
        RefreshDurationMaxMag,
        StackCount,
        ExclusiveRefresh
    };

    enum class StatusParticlePreset : uint8_t
    {
        None = 0,
        StunStars,
        PoisonCloud,
        BleedDrip,
        IgniteFlames,
        ChillMist,
        ShockSparks
    };

    struct StatusDef
    {
        StatusId        id                   = StatusId::None;
        const char*     name                 = "";
        DamageType      damageType           = DamageType::True;
        StatusStackRule stack                = StatusStackRule::RefreshLonger;
        uint8_t         maxStacks            = 1;
        bool            hardCc               = false;
        CcCategory      ccCategory           = CcCategory::Count; // Count = not CC
        float           defaultDuration      = 0.f;
        float           defaultMagnitude     = 1.f;
        float           tickDamage           = 0.f;
        float           tickInterval         = 1.f;
        bool            tickScalesWithStacks = false;

        StatusParticlePreset particle      = StatusParticlePreset::None;
        bool                 particleLoop  = true;
        const char*          applyCue      = "";
        const char*          loopCue       = "";
        const char*          expireCue     = "";
        uint8_t              audioPriority = 0;
        const char*          animBool      = "";
        const char*          animTrigger   = "";
    };

    const StatusDef* statusDef(StatusId id);

} // namespace Dark::Combat
