#include "Weapons/MeleeWeapon.h"

#include "Combat/HitPart.h"
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

        struct SwingContact
        {
            bool            found = false;
            Combat::HitPart part  = Combat::HitPart::None;
            Vector3f        point{};
            Vector3f        normal{};
            float           dist = 0.0f;
            float           dot  = 0.0f;
        };

        SwingContact swingContact(const MeleeWeaponDesc& desc, const WeaponFireRequest& req, const WeaponWorldQuery& world, int index, const Vector3f& look)
        {
            SwingContact contact;
            int          segmentCount = 0;
            const Combat::HitSegment* segments = weaponTargetCapsules(world, index, segmentCount);
            if (weaponCapsulesActive(segments, segmentCount))
            {
                WeaponPartContact picked{};
                if (!weaponPickCapsuleReach(segments, segmentCount, req.ownerPos, look, desc.range, desc.minDot, picked))
                    return contact;
                contact.found  = true;
                contact.part   = picked.part;
                contact.point  = picked.point;
                contact.normal = picked.normal;
                contact.dist   = picked.distance;
                contact.dot    = picked.facing;
                return contact;
            }

            Vector3f center{};
            if (!weaponTargetCenter(world, index, center))
                return contact;
            Vector3f to = center - req.ownerPos;
            to.y        = 0.0f;
            const float dist = to.Magnitude();
            if (dist > desc.range || dist < 1.0e-4f)
                return contact;
            to *= (1.0f / dist);
            const float dot = look.Dot(to);
            if (dot < desc.minDot)
                return contact;
            contact.found  = true;
            contact.point  = center;
            contact.normal = -look;
            contact.dist   = dist;
            contact.dot    = dot;
            return contact;
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
            if (!weaponTargetAlive(world, i))
                continue;
            const SwingContact contact = swingContact(m_desc, req, world, i, look);
            if (!contact.found)
                continue;
            const float s = contact.dot - 0.5f * contact.dist / m_desc.range;
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
            const SwingContact contact = swingContact(m_desc, req, world, i, look);
            if (!contact.found)
                continue;

            const float scale = req.damageScale > 0.0f ? req.damageScale : 1.0f;
            WeaponHit hit{};
            hit.point       = contact.point;
            hit.normal      = contact.normal;
            hit.direction   = look;
            hit.damage      = m_desc.damage * scale;
            hit.part        = contact.part;
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
