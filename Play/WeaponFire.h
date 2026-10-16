#pragma once

#include "Character/SkillCatalog.h"
#include "Character/SkillComponent.h"
#include "Math/Vector3f.h"
#include "Weapons/Weapon.h"

namespace Dark
{

    // Camera shot: look is normalized, origin sits 2.2 m along it, and a zero look becomes +Z.
    inline WeaponFireRequest makeCameraFireRequest(const Math::Vector3f& cameraPos, const Math::Vector3f& cameraLook, const Math::Vector3f* ownerPos, float damageScale)
    {
        WeaponFireRequest req{};
        req.direction = cameraLook;
        if (req.direction.MagnitudeSqrd() > 1.0e-6f)
            req.direction.Normalize();
        else
            req.direction = Math::Vector3f{ 0.0f, 0.0f, 1.0f };
        req.origin      = cameraPos + req.direction * 2.2f;
        req.ownerPos    = ownerPos ? *ownerPos : cameraPos;
        req.damageScale = damageScale;
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

} // namespace Dark
