#include "Gameplay/HealthPack.h"

#include "Core/Log.h"
#include "Math/MathDefines.h"
#include "Math/Quaternion.h"

#include <cmath>

namespace Dark
{
    using namespace Math;

    Matrix4f healthPackWorldMatrix(const Vector3f& pos, float spin, float bob)
    {
        const Quaternion rot = Quaternion::FromAxisAngle(Vector3f::Y_AXIS, spin);
        const float      y   = pos.y + 0.08f * std::sinf(bob * 2.6f);
        return Matrix4f::ScaleMatrixXYZ(0.9f, 0.9f, 0.9f) * rot.ToMatrix4() * Matrix4f::TranslationMatrix(pos.x, y, pos.z);
    }

    void HealthPackSet::clear()
    {
        m_count = 0;
        m_spin  = 0.0f;
        m_bob   = 0.0f;
        for (int i = 0; i < kMax; ++i)
            m_packs[i] = {};
    }

    bool HealthPackSet::tryAdd(const Vector3f& pos)
    {
        if (m_count >= kMax)
            return false;
        HealthPack& p = m_packs[m_count++];
        p               = {};
        p.pos           = pos;
        p.active        = true;
        p.respawnIn     = 0.0f;
        return true;
    }

    void HealthPackSet::tick(float dt)
    {
        m_spin += 1.85f * dt;
        if (m_spin > TwoPi)
            m_spin -= TwoPi;
        m_bob += dt;

        for (int i = 0; i < m_count; ++i)
        {
            HealthPack& p = m_packs[i];
            if (p.active)
                continue;
            p.havePrevWorld = false;
            p.respawnIn -= dt;
            if (p.respawnIn <= 0.0f)
                p.active = true;
        }
    }

    int HealthPackSet::tryPickup(const Vector3f& playerPos, Health& health)
    {
        if (!health.alive() || health.hp() >= health.maxHp())
            return 0;

        const float r2    = kPickupR * kPickupR;
        int         taken = 0;
        for (int i = 0; i < m_count; ++i)
        {
            HealthPack& p = m_packs[i];
            if (!p.active)
                continue;
            const float dx = playerPos.x - p.pos.x;
            const float dy = playerPos.y - p.pos.y;
            const float dz = playerPos.z - p.pos.z;
            if (dx * dx + dy * dy + dz * dz > r2)
                continue;
            health.heal(kHeal);
            p.active    = false;
            p.respawnIn = kRespawn;
            ++taken;
            DE_LOG_INFO("Player: health pack +{:.0f} ({:.0f}/{:.0f})", kHeal, health.hp(), health.maxHp());
        }
        return taken;
    }

} // namespace Dark
