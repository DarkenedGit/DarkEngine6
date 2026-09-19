#pragma once

#include "Character/HitReaction.h"
#include "Combat/DamageTypes.h"

#include <cstdint>

namespace Dark::Combat
{

    enum class Severity : uint8_t
    {
        Tick = 0,
        Light,
        Medium,
        Heavy,
        Critical,
        Fatal
    };

    // Map finalDamage / maxHp into a severity band (RFC defaults).
    inline Severity severityFromNormalized(float normDmg, bool poiseBroke, bool killed)
    {
        if (killed)
            return Severity::Fatal;
        if (poiseBroke)
            return Severity::Critical;
        if (normDmg < 0.03f)
            return Severity::Tick;
        if (normDmg < 0.08f)
            return Severity::Light;
        if (normDmg < 0.18f)
            return Severity::Medium;
        if (normDmg < 0.35f)
            return Severity::Heavy;
        return Severity::Critical;
    }

    // Table-driven HitReaction settings keyed by severity.
    inline HitReactionSettings hitReactionForSeverity(Severity sev)
    {
        HitReactionSettings s{};
        switch (sev)
        {
        case Severity::Tick:
            s.stunSeconds       = 0.0f;
            s.knockbackDistance = 0.0f;
            s.knockbackSeconds  = 0.0f;
            break;
        case Severity::Light:
            s.stunSeconds       = 0.08f;
            s.knockbackDistance = 0.35f;
            s.knockbackSeconds  = 0.08f;
            break;
        case Severity::Medium:
            s.stunSeconds       = 0.20f;
            s.knockbackDistance = 0.90f;
            s.knockbackSeconds  = 0.12f;
            break;
        case Severity::Heavy:
            s.stunSeconds       = 0.35f;
            s.knockbackDistance = 1.40f;
            s.knockbackSeconds  = 0.16f;
            break;
        case Severity::Critical:
            s.stunSeconds       = 0.55f;
            s.knockbackDistance = 2.20f;
            s.knockbackSeconds  = 0.22f;
            break;
        case Severity::Fatal:
            s.stunSeconds       = 0.0f;
            s.knockbackDistance = 1.0f;
            s.knockbackSeconds  = 0.10f;
            break;
        }
        s.horizontalOnly = true;
        return s;
    }

    // Knockdown lock: stun matches effective CC duration. force <= 0 uses the 2.4 m default.
    inline HitReactionSettings hitReactionForKnockdown(float duration, float force)
    {
        HitReactionSettings s{};
        s.stunSeconds       = duration > 0.0f ? duration : 0.0f;
        s.knockbackDistance = force > 0.0f ? force : 2.4f;
        s.knockbackSeconds  = 0.22f;
        s.horizontalOnly    = true;
        return s;
    }

} // namespace Dark::Combat
