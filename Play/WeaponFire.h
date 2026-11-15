#pragma once

#include "Character/SkillCatalog.h"
#include "Character/SkillComponent.h"
#include "Math/Vector3f.h"
#include "Scene/EntityCombat.h"
#include "Weapons/Weapon.h"

namespace Dark
{

    // Camera shot: look is normalized, origin sits along it, and a zero look becomes +Z.
    // A negative muzzleOffset reads the player combat profile.
    inline WeaponFireRequest makeCameraFireRequest(const Math::Vector3f& cameraPos, const Math::Vector3f& cameraLook, const Math::Vector3f* ownerPos, float damageScale, float muzzleOffset = -1.0f)
    {
        WeaponFireRequest req{};
        req.direction = cameraLook;
        if (req.direction.MagnitudeSqrd() > 1.0e-6f)
            req.direction.Normalize();
        else
            req.direction = Math::Vector3f{ 0.0f, 0.0f, 1.0f };
        const float muzzle = muzzleOffset >= 0.0f ? muzzleOffset : combatProfileForId("core:player").muzzleOffset;
        req.origin         = cameraPos + req.direction * muzzle;
        req.ownerPos       = ownerPos ? *ownerPos : cameraPos;
        req.damageScale    = damageScale;
        return req;
    }

    inline void applyShootSkill(WeaponFireRequest& req, const SkillComponent* skill)
    {
        if (!skill)
            return;
        const int shootLevel = skill->level(SkillId::Shoot);
        req.recoilScale      = skillScalar(SkillId::Shoot, SkillScalar::RecoilScale, shootLevel);
        req.cooldownScale    = skillScalar(SkillId::Shoot, SkillScalar::CooldownScale, shootLevel);
    }

    // Charge reads the weapon before this frame's swap. The fire site reads it after the swap.
    struct WeaponAttackInput
    {
        bool airborne      = false;
        bool swimming      = false;
        bool throwable     = false;
        bool swordOrRifle  = false;
        bool attackPressed = false;
        bool jumpBuffered  = false;
        bool fireQuick     = false;
        bool fireCharged   = false;
        bool ccLocked      = false;
        bool jumpBusy      = false;
    };

    struct WeaponAttackDecision
    {
        bool allowCharge          = false;
        bool beginJumpAttack      = false;
        bool throwOnRelease       = false;
        bool fireLoadoutOnRelease = false;
        bool suppressRelease      = false;
    };

    inline void classifyWeaponAttack(WeaponAttackInput& in, WeaponKind kind)
    {
        in.throwable    = kind == WeaponKind::Throwable;
        in.swordOrRifle = kind == WeaponKind::Melee || kind == WeaponKind::Projectile;
    }

    inline WeaponAttackDecision decideWeaponAttack(const WeaponAttackInput& in)
    {
        WeaponAttackDecision out{};
        const bool blocked  = in.ccLocked || in.jumpBusy;
        const bool released = in.fireQuick || in.fireCharged;
        out.allowCharge          = !blocked && (!in.airborne || in.throwable);
        out.beginJumpAttack      = in.airborne && (in.attackPressed || in.jumpBuffered) && !in.throwable && !blocked && !in.swimming;
        out.throwOnRelease       = in.throwable && released && !blocked;
        out.fireLoadoutOnRelease = in.swordOrRifle && !in.airborne && released && !blocked;
        out.suppressRelease      = in.airborne && !in.throwable;
        return out;
    }

} // namespace Dark
