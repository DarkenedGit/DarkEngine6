#include "Weapons/MeleeWeapon.h"

#include "Math/MathDefines.h"

namespace Dark
{
    using Math::Vector3f;

    MeleeWeapon::MeleeWeapon()
        : MeleeWeapon(MeleeWeaponDesc{})
    {
    }

    MeleeWeapon::MeleeWeapon(const MeleeWeaponDesc& desc)
        : m_desc(desc)
    {
    }

    void MeleeWeapon::setDesc(const MeleeWeaponDesc& desc)
    {
        m_desc = desc;
    }

    bool MeleeWeapon::fire(const WeaponFireRequest& req, const WeaponWorldQuery& world)
    {
        if (m_cooldown > 0.0f)
            return false;

        Vector3f look = req.direction;
        look.y        = 0.0f;
        if (look.MagnitudeSqrd() > 1.0e-6f)
            look.Normalize();
        else
            look = Vector3f{ 0.0f, 0.0f, 1.0f };

        m_cooldown = m_desc.cooldown > 0.0f ? m_desc.cooldown : 0.0f;

        const int n = weaponTargetCount(world);
        for (int i = 0; i < n; ++i)
        {
            if (!weaponTargetAlive(world, i))
                continue;
            Vector3f center{};
            if (!weaponTargetCenter(world, i, center))
                continue;
            Vector3f to = center - req.ownerPos;
            to.y        = 0.0f;
            const float dist = to.Magnitude();
            if (dist > m_desc.range || dist < 1.0e-4f)
                continue;
            to *= (1.0f / dist);
            if (look.Dot(to) < m_desc.minDot)
                continue;

            WeaponHit hit{};
            hit.point       = center;
            hit.normal      = -look;
            hit.direction   = look;
            hit.damage      = m_desc.damage;
            hit.targetIndex = i;
            hit.hitTarget   = true;
            hit.weapon      = WeaponKind::Melee;
            emitHit(hit);
        }
        return true;
    }

    void MeleeWeapon::tick(float dt, const WeaponWorldQuery&)
    {
        if (m_cooldown > 0.0f)
            m_cooldown -= dt;
    }

    void MeleeWeapon::clear()
    {
        m_cooldown = 0.0f;
    }

} // namespace Dark
