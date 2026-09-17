#pragma once

#include "Combat/DamageTypes.h"
#include "Math/MathHelper.h"

#include <cstddef>

namespace Dark::Combat
{

    // Hyperbolic armor constant K: armor == K -> 50% DR before the soft cap.
    // Tune in play; do not copy foreign MMO level tables.
    inline constexpr float kArmorCurveK     = 100.0f;
    inline constexpr float kDamageReductionCap = 0.75f;
    inline constexpr float kResistMin       = -0.25f;
    inline constexpr float kResistMax       = 0.90f;

    struct ArmorStats
    {
        float armor           = 0.f;
        float resist[kDamageTypeCount]{}; // True slot unused
        float blockMitigation = 0.7f;     // HP multiplier while blocking (0.7 = 30% chip)
    };

    inline float clampArmorPen(float p)
    {
        return Math::Clamp(p, 0.0f, 1.0f);
    }

    inline float effectiveArmor(float armor, float armorPen)
    {
        const float a = armor < 0.0f ? 0.0f : armor;
        return a * (1.0f - clampArmorPen(armorPen));
    }

    // DR = A_eff / (A_eff + K), clamped to kDamageReductionCap.
    inline float hyperbolicDamageReduction(float armorEff, float k = kArmorCurveK)
    {
        if (armorEff <= 0.0f || k <= 0.0f)
            return 0.0f;
        const float dr = armorEff / (armorEff + k);
        return Math::Min(dr, kDamageReductionCap);
    }

    // d' = d * K / (A_eff + K) with DR soft-cap.
    inline float mitigatePhysical(float damage, float armor, float armorPen, float k = kArmorCurveK)
    {
        if (damage <= 0.0f)
            return 0.0f;
        const float aEff = effectiveArmor(armor, armorPen);
        const float dr   = hyperbolicDamageReduction(aEff, k);
        return damage * (1.0f - dr);
    }

    inline float clampResist(float r)
    {
        return Math::Clamp(r, kResistMin, kResistMax);
    }

    inline float mitigateElemental(float damage, float resist)
    {
        if (damage <= 0.0f)
            return 0.0f;
        return damage * (1.0f - clampResist(resist));
    }

    // Full typed mitigation helper used by CombatSystem.
    inline float mitigateTyped(float damage, DamageType type, const ArmorStats& stats, float armorPen, bool ignoreArmor)
    {
        if (damage <= 0.0f)
            return 0.0f;
        if (isTrue(type) || ignoreArmor)
            return damage;
        if (isPhysical(type))
            return mitigatePhysical(damage, stats.armor, armorPen);
        if (isElemental(type))
        {
            const size_t idx = static_cast<size_t>(type);
            const float  r   = idx < kDamageTypeCount ? stats.resist[idx] : 0.0f;
            return mitigateElemental(damage, r);
        }
        return damage;
    }

} // namespace Dark::Combat
