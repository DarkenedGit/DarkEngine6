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
        if (m_cooldown > 0.0f || m_pendingLeft >= 0.0f)
            return false;

        m_cooldown = m_desc.cooldown > 0.0f ? m_desc.cooldown : 0.0f;
        if (m_desc.hitDelay > 0.0f)
        {
            m_pending     = req;
            m_pendingLeft = m_desc.hitDelay;
            return true;
        }
        strike(req, world);
        return true;
    }

    namespace
    {
        Vector3f flatLook(const Vector3f& direction)
        {
            Vector3f look = direction;
            look.y        = 0.0f;
            if (look.MagnitudeSqrd() > 1.0e-6f)
                look.Normalize();
            else
                look = Vector3f{ 0.0f, 0.0f, 1.0f };
            return look;
        }
    } // namespace

    int MeleeWeapon::pickTarget(const WeaponFireRequest& req, const WeaponWorldQuery& world) const
    {
        const Vector3f look  = flatLook(req.direction);
        int            best  = -1;
        float          score = -1.0e9f;
        const int      n     = weaponTargetCount(world);
        for (int i = 0; i < n; ++i)
        {
            Vector3f center{};
            if (!weaponTargetAlive(world, i) || !weaponTargetCenter(world, i, center))
                continue;
            Vector3f to = center - req.ownerPos;
            to.y        = 0.0f;
            const float dist = to.Magnitude();
            if (dist > m_desc.range || dist < 1.0e-4f)
                continue;
            const float dot = look.Dot(to * (1.0f / dist));
            if (dot < m_desc.minDot)
                continue;
            const float s = dot - 0.5f * dist / m_desc.range;
            if (s > score)
            {
                score = s;
                best  = i;
            }
        }
        return best;
    }

    void MeleeWeapon::strike(const WeaponFireRequest& req, const WeaponWorldQuery& world)
    {
        const Vector3f look = flatLook(req.direction);
        const int      only = m_desc.singleTarget ? pickTarget(req, world) : -1;
        if (m_desc.singleTarget && only < 0)
            return;

        const int n = weaponTargetCount(world);
        for (int i = 0; i < n; ++i)
        {
            if (m_desc.singleTarget && i != only)
                continue;
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

            const float scale = req.damageScale > 0.0f ? req.damageScale : 1.0f;
            WeaponHit hit{};
            hit.point       = center;
            hit.normal      = -look;
            hit.direction   = look;
            hit.damage      = m_desc.damage * scale;
            hit.targetIndex = i;
            if (world.targetEntityAt)
                hit.targetEntity = world.targetEntityAt(world.targetUser, i);
            hit.hitTarget   = true;
            hit.weapon      = WeaponKind::Melee;
            emitHit(hit);
        }
    }

    void MeleeWeapon::tick(float dt, const WeaponWorldQuery& world)
    {
        if (m_cooldown > 0.0f)
            m_cooldown -= dt;
        if (m_pendingLeft < 0.0f)
            return;
        m_pendingLeft -= dt;
        if (m_pendingLeft <= 0.0f)
        {
            m_pendingLeft = -1.0f;
            strike(m_pending, world);
        }
    }

    void MeleeWeapon::clear()
    {
        m_cooldown    = 0.0f;
        m_pendingLeft = -1.0f;
    }

} // namespace Dark
