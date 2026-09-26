#include "Combat/Shield.h"

#include "Combat/ArmorComponent.h"
#include "Combat/DefenseComponent.h"
#include "ECS/World.h"
#include "Math/MathHelper.h"

#include <cmath>

namespace Dark::Combat
{

    void Shield::tick(float dt, bool wantUp)
    {
        if (dt < 0.0f)
            dt = 0.0f;

        const float dur = m_settings.raiseSeconds;
        if (dur <= 1.0e-4f)
        {
            m_alpha = wantUp ? 1.0f : 0.0f;
            m_phase = wantUp ? ShieldPhase::Up : ShieldPhase::Down;
            return;
        }

        if (wantUp)
        {
            if (m_phase != ShieldPhase::Up)
                m_phase = ShieldPhase::Raising;
            m_alpha += dt / dur;
            if (m_alpha >= 1.0f)
            {
                m_alpha = 1.0f;
                m_phase = ShieldPhase::Up;
            }
        }
        else
        {
            if (m_phase != ShieldPhase::Down)
                m_phase = ShieldPhase::Lowering;
            m_alpha -= dt / dur;
            if (m_alpha <= 0.0f)
            {
                m_alpha = 0.0f;
                m_phase = ShieldPhase::Down;
            }
        }
    }

    float Shield::speedScale() const
    {
        const float slow = Math::Clamp(m_settings.moveSpeedScale, 0.0f, 1.0f);
        return Math::Lerp(1.0f, slow, Math::Clamp(m_alpha, 0.0f, 1.0f));
    }

    void Shield::reset()
    {
        m_phase = ShieldPhase::Down;
        m_alpha = 0.0f;
    }

    void OffhandState::tick(float dt, bool wantShield, bool toggleLight)
    {
        if (wantShield)
        {
            lightOn      = false;
            lightPending = false;
        }
        else if (toggleLight)
        {
            if (shield.phase() == ShieldPhase::Down)
                lightOn = !lightOn;
            else
                lightPending = !lightPending;
        }

        shield.tick(dt, wantShield);

        // The light only comes on once the shield is fully down.
        if (shield.phase() != ShieldPhase::Down)
            lightOn = false;
        else if (lightPending && !wantShield)
        {
            lightOn      = true;
            lightPending = false;
        }
    }

    void OffhandState::reset()
    {
        lightOn      = true;
        lightPending = false;
        shield.reset();
    }

    ShieldLocalPose shieldLocalPose(float alpha)
    {
        const float t = Math::Clamp(alpha, 0.0f, 1.0f);
        // Left of the chest. A behind-the-shoulder camera looks past this, not through it.
        const Math::Vector3f lowered{ -0.48f, 0.70f, 0.04f };
        const Math::Vector3f raised{ -0.50f, 1.12f, 0.26f };
        const Math::Quaternion rotDown = Math::Quaternion::FromAxisAngle(Math::Vector3f::Z_AXIS, 1.15f);
        const Math::Quaternion faceForward = Math::Quaternion::FromAxisAngle(Math::Vector3f::X_AXIS, 1.5707963f);
        const Math::Quaternion turnIn      = Math::Quaternion::FromAxisAngle(Math::Vector3f::Y_AXIS, 0.40f);
        const Math::Quaternion rotUp       = turnIn * faceForward;

        ShieldLocalPose pose;
        pose.position = Math::Lerp(lowered, raised, t);
        pose.rotation = Math::Quaternion::Slerp(rotDown, rotUp, t);
        pose.rotation.Normalize();
        return pose;
    }

    void syncShieldDefense(DefenseComponent& defense, const Shield& shield, float facingYawRad)
    {
        defense.blocking         = shield.blocking();
        defense.blockArcDeg      = shield.settings().blockArcDeg;
        defense.blockStaminaCost = 0.0f;
        defense.facingYawRad     = facingYawRad;
    }

    void equipPlayerShield(World& world, Entity player)
    {
        if (!player.valid() || !world.alive(player))
            return;
        if (!world.has<DefenseComponent>(player))
            world.emplace<DefenseComponent>(player);
        ArmorComponent* armor = world.get<ArmorComponent>(player);
        if (!armor)
            armor = &world.emplace<ArmorComponent>(player);
        // Default blockMitigation leaves most of the hit. A raised shield stops it.
        armor->stats.blockMitigation = 0.0f;
    }

} // namespace Dark::Combat
