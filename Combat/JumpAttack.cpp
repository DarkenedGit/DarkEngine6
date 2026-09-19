#include "Combat/JumpAttack.h"

#include "Core/Log.h"
#include "Math/MathHelper.h"

#include <cmath>

namespace Dark::Combat
{
    using Math::Vector3f;

    namespace
    {
        constexpr float kEps          = 1.0e-4f;
        constexpr float kEnterWater   = 0.25f;
        constexpr float kSwimOffset   = 0.35f;

        Vector3f flatten(const Vector3f& v)
        {
            return Vector3f{ v.x, 0.0f, v.z };
        }

        Vector3f flattenDirOrZero(const Vector3f& v)
        {
            Vector3f d = flatten(v);
            const float mag = d.Magnitude();
            if (mag <= kEps)
                return Vector3f{ 0.0f, 0.0f, 0.0f };
            d *= (1.0f / mag);
            return d;
        }

        Vector3f flattenDirOrForward(const Vector3f& v)
        {
            Vector3f d = flattenDirOrZero(v);
            if (d.MagnitudeSqrd() <= 1.0e-8f)
                return Vector3f{ 0.0f, 0.0f, 1.0f };
            return d;
        }

        Entity targetEntityAt(const WeaponWorldQuery& world, int index)
        {
            if (!world.targetEntityAt)
                return {};
            return world.targetEntityAt(world.targetUser, index);
        }

        void fillHitDir(Vector3f& out, const Vector3f& from, const Vector3f& to)
        {
            out = flatten(to - from);
            if (out.MagnitudeSqrd() > 1.0e-8f)
                out.Normalize();
            else
                out = Vector3f{ 0.0f, 0.0f, 1.0f };
        }
    } // namespace

    JumpAttack::JumpAttack(const JumpAttackDef& def)
        : m_def(def)
    {
    }

    void JumpAttack::setDef(const JumpAttackDef& def)
    {
        m_def = def;
    }

    const JumpAttackDef& JumpAttack::def() const
    {
        return m_def;
    }

    JumpAttackPhase JumpAttack::phase() const
    {
        return m_phase;
    }

    bool JumpAttack::busy() const
    {
        return m_phase != JumpAttackPhase::Idle;
    }

    bool JumpAttack::inAirCommit() const
    {
        return m_phase == JumpAttackPhase::Leap || m_phase == JumpAttackPhase::Connected;
    }

    Entity JumpAttack::connectedTarget() const
    {
        return m_connectedTarget;
    }

    float JumpAttack::connectWindowLeft() const
    {
        return m_connectWindowLeft;
    }

    float JumpAttack::cooldownLeft() const
    {
        return m_cooldown;
    }

    const Vector3f& JumpAttack::velocity() const
    {
        return m_velocity;
    }

    void JumpAttack::enterLeap()
    {
        m_phase              = JumpAttackPhase::Leap;
        m_connectWindowLeft  = m_def.connectWindow;
        m_telegraphLeft      = 0.0f;
    }

    void JumpAttack::enterRecover()
    {
        m_phase        = JumpAttackPhase::Recover;
        m_recoverLeft  = m_didConnect ? m_def.landLagConnected : m_def.landLagWhiff;
        m_needTakeoff  = false;
        m_snapLeft     = 0.0f;
    }

    void JumpAttack::clearLeapState()
    {
        m_hits.clear();
        m_connectedTarget = {};
        m_didConnect      = false;
        m_needTakeoff     = false;
        m_snapLeft        = 0.0f;
        m_connectWindowLeft = 0.0f;
        m_telegraphLeft   = 0.0f;
        m_recoverLeft     = 0.0f;
    }

    void JumpAttack::fillConnectEvent(DamageEvent& out, Entity target, const Vector3f& attackerPos, const Vector3f& targetPos) const
    {
        out              = DamageEvent{};
        out.source       = m_attacker;
        out.target       = target;
        out.type         = m_def.connectType;
        out.amount       = m_def.connectDamage;
        out.poiseDamage  = m_def.connectPoise;
        out.hitPoint     = targetPos;
        fillHitDir(out.hitDir, attackerPos, targetPos);
        out.flags            = m_def.connectFlags;
        out.teamSource       = m_def.teamFilter;
        out.statusDuration   = m_def.knockdownDuration;
        out.statusMagnitude  = m_def.knockdownForce;
    }

    void JumpAttack::fillPoundEvent(DamageEvent& out, Entity target, const Vector3f& landPos, const Vector3f& targetPos) const
    {
        out              = DamageEvent{};
        out.source       = m_attacker;
        out.target       = target;
        out.type         = m_def.poundType;
        out.amount       = m_def.poundDamage;
        out.poiseDamage  = m_def.poundPoise;
        out.hitPoint     = targetPos;
        fillHitDir(out.hitDir, landPos, targetPos);
        out.flags          = m_def.poundFlags;
        out.teamSource     = m_def.teamFilter;
        out.statusDuration = m_def.poundStunDuration;
    }

    bool JumpAttack::begin(const JumpAttackBegin& req)
    {
        if (m_phase != JumpAttackPhase::Idle || m_cooldown > 0.0f)
            return false;

        const bool groundedGate = req.heightAboveGround < m_def.minHeight && req.airTime < m_def.minAirTime;
        if (m_def.telegraphSeconds <= 0.0f && groundedGate)
            return false;

        clearLeapState();
        m_attacker        = req.attacker;
        m_intendedTarget  = req.intendedTarget;
        m_lookFlat        = flatten(req.lookFlat);
        m_velocity        = req.velocity;
        m_cooldown        = m_def.cooldown;

        if (m_def.telegraphSeconds > 0.0f)
        {
            m_phase         = JumpAttackPhase::Telegraph;
            m_telegraphLeft = m_def.telegraphSeconds;
        }
        else
        {
            enterLeap();
        }
        return true;
    }

    void JumpAttack::onLanded(const Vector3f&)
    {
        if (m_phase != JumpAttackPhase::Leap && m_phase != JumpAttackPhase::Connected)
            return;
        m_phase       = JumpAttackPhase::Pound;
        m_needTakeoff = false;
        m_snapLeft    = 0.0f;
    }

    void JumpAttack::onSplashed()
    {
        cancel(JumpAttackCancel::NoPound);
    }

    void JumpAttack::cancel(JumpAttackCancel reason)
    {
        (void)reason;
        clearLeapState();
        m_phase = JumpAttackPhase::Idle;
    }

    void JumpAttack::tick(float dt)
    {
        if (dt < 0.0f)
            dt = 0.0f;
        if (m_cooldown > 0.0f)
            m_cooldown = Math::Max(0.0f, m_cooldown - dt);

        switch (m_phase)
        {
        case JumpAttackPhase::Idle:
            break;
        case JumpAttackPhase::Telegraph:
            m_telegraphLeft -= dt;
            if (m_telegraphLeft <= 0.0f)
            {
                enterLeap();
                m_needTakeoff = true;
            }
            break;
        case JumpAttackPhase::Leap:
            m_connectWindowLeft = Math::Max(0.0f, m_connectWindowLeft - dt);
            break;
        case JumpAttackPhase::Connected:
            break;
        case JumpAttackPhase::Pound:
            DE_LOG_WARN("JumpAttack: tick observed Pound without tryPound");
            enterRecover();
            break;
        case JumpAttackPhase::Recover:
            m_recoverLeft -= dt;
            if (m_recoverLeft <= 0.0f)
                m_phase = JumpAttackPhase::Idle;
            break;
        }
    }

    bool JumpAttack::tryConnect(const WeaponWorldQuery& world, const Vector3f& attackerPos, const Vector3f& lookFlat, DamageEvent& out)
    {
        if (m_phase != JumpAttackPhase::Leap || m_connectWindowLeft <= 0.0f)
            return false;

        const bool     exclusive = m_intendedTarget.valid();
        const Vector3f lookDir   = flattenDirOrZero(lookFlat);
        const bool     useCone   = !exclusive && lookDir.MagnitudeSqrd() > 1.0e-8f;

        Entity   best{};
        Vector3f bestPos{};
        float    bestDist = 1.0e30f;

        const int n = weaponTargetCount(world);
        for (int i = 0; i < n; ++i)
        {
            if (!weaponTargetAlive(world, i))
                continue;
            Vector3f center{};
            if (!weaponTargetCenter(world, i, center))
                continue;
            const Entity e = targetEntityAt(world, i);
            if (!e.valid())
                continue;
            if (m_attacker.valid() && e.id() == m_attacker.id())
                continue;
            if (m_hits.contains(e))
                continue;
            if (exclusive && e.id() != m_intendedTarget.id())
                continue;

            const Vector3f to   = flatten(center - attackerPos);
            const float    dist = to.Magnitude();
            if (dist > m_def.connectRange)
                continue;
            if (fabsf(center.y - attackerPos.y) > m_def.connectVerticalSlop)
                continue;
            if (useCone && dist > kEps)
            {
                Vector3f dir = to;
                dir *= (1.0f / dist);
                if (lookDir.Dot(dir) < m_def.connectMinDot)
                    continue;
            }

            if (exclusive)
            {
                best    = e;
                bestPos = center;
                break;
            }
            if (dist < bestDist)
            {
                bestDist = dist;
                best     = e;
                bestPos  = center;
            }
        }

        if (!best.valid())
            return false;
        if (!m_hits.tryAdd(best))
            return false;

        fillConnectEvent(out, best, attackerPos, bestPos);
        m_connectedTarget = best;
        m_didConnect      = true;
        m_snapLeft        = m_def.connectSnapSeconds;
        m_phase           = JumpAttackPhase::Connected;
        return true;
    }

    int JumpAttack::tryPound(const WeaponWorldQuery& world, const Vector3f& landPos, DamageEvent* out, int outCap)
    {
        if (m_phase != JumpAttackPhase::Pound)
            return 0;
        if (outCap > 0 && !out)
        {
            DE_ASSERT(out != nullptr);
            outCap = 0;
        }
        if (outCap < 0)
            outCap = 0;

        const int maxHits = m_def.poundMaxTargets > 0 ? m_def.poundMaxTargets : 0;
        const int cap     = maxHits < outCap ? maxHits : outCap;
        int       written = 0;

        const int n = weaponTargetCount(world);
        while (written < cap)
        {
            Entity   best{};
            Vector3f bestPos{};
            float    bestDist = 1.0e30f;

            for (int i = 0; i < n; ++i)
            {
                if (!weaponTargetAlive(world, i))
                    continue;
                Vector3f center{};
                if (!weaponTargetCenter(world, i, center))
                    continue;
                const Entity e = targetEntityAt(world, i);
                if (!e.valid())
                    continue;
                if (m_attacker.valid() && e.id() == m_attacker.id())
                    continue;
                if (m_hits.contains(e))
                    continue;

                const float dist = flatten(center - landPos).Magnitude();
                if (dist > m_def.poundRadius)
                    continue;
                if (fabsf(center.y - landPos.y) > m_def.poundVerticalSlop)
                    continue;
                if (dist < bestDist)
                {
                    bestDist = dist;
                    best     = e;
                    bestPos  = center;
                }
            }

            if (!best.valid())
                break;
            if (!m_hits.tryAdd(best))
                break;
            fillPoundEvent(out[written], best, landPos, bestPos);
            ++written;
        }

        enterRecover();
        return written;
    }

    void JumpAttack::applyAirSteering(Vector3f& velocity, const Vector3f& pos, const Vector3f& lookFlat, const Vector3f* targetPos, bool hasTarget, float dt)
    {
        if (m_def.homingRate <= 0.0f || dt <= 0.0f)
            return;

        Vector3f dir;
        float    speed = 0.0f;
        if (hasTarget && targetPos)
        {
            dir = flattenDirOrZero(*targetPos - pos);
            if (dir.MagnitudeSqrd() <= 1.0e-8f)
                return;
            const float cur = sqrtf(velocity.x * velocity.x + velocity.z * velocity.z);
            speed           = Math::Max(cur, m_def.leapForwardSpeed);
        }
        else
        {
            dir = flattenDirOrZero(lookFlat);
            if (dir.MagnitudeSqrd() <= 1.0e-8f)
                return;
            speed = m_def.leapForwardSpeed;
        }

        const float    a       = 1.0f - expf(-m_def.homingRate * dt);
        const Vector3f desired = dir * speed;
        velocity.x             = Math::Lerp(velocity.x, desired.x, a);
        velocity.z             = Math::Lerp(velocity.z, desired.z, a);
    }

    void JumpAttack::applyConnectSnap(Vector3f& position, const Vector3f& targetPos, float dt)
    {
        if (m_phase != JumpAttackPhase::Connected || m_snapLeft <= 0.0f || dt <= 0.0f)
            return;
        const float t = Math::Clamp(dt / m_snapLeft, 0.0f, 1.0f);
        position.x    = Math::Lerp(position.x, targetPos.x, t);
        position.z    = Math::Lerp(position.z, targetPos.z, t);
        m_snapLeft    = Math::Max(0.0f, m_snapLeft - dt);
    }

    void JumpAttack::tickAutonomous(Vector3f& position, float dt, float (*heightAt)(void*, float, float), void* heightUser, float waterY, const Vector3f* targetPos, bool hasTarget, bool& landed,
                                   bool& splashed)
    {
        landed   = false;
        splashed = false;
        if (m_phase != JumpAttackPhase::Leap && m_phase != JumpAttackPhase::Connected)
            return;
        if (dt < 0.0f)
            dt = 0.0f;

        if (m_needTakeoff)
        {
            const float hang = 2.0f * m_def.leapVerticalSpeed / Math::Max(m_def.gravity, kEps);
            Vector3f    look = flattenDirOrForward(m_lookFlat);
            if (hasTarget && targetPos)
            {
                const Vector3f to   = flatten(*targetPos - position);
                const float    dist = to.Magnitude();
                float          spd  = hang > kEps ? dist / hang : 0.0f;
                spd                 = Math::Clamp(spd, 0.0f, m_def.leapForwardSpeedMax);
                const Vector3f dir  = dist > kEps ? to * (1.0f / dist) : look;
                m_velocity.x        = dir.x * spd;
                m_velocity.z        = dir.z * spd;
            }
            else
            {
                m_velocity.x = look.x * m_def.leapForwardSpeed;
                m_velocity.z = look.z * m_def.leapForwardSpeed;
            }
            m_velocity.y  = m_def.leapVerticalSpeed;
            m_needTakeoff = false;
        }

        m_velocity.y -= m_def.gravity * dt;
        applyAirSteering(m_velocity, position, m_lookFlat, targetPos, hasTarget, dt);
        position.x += m_velocity.x * dt;
        position.y += m_velocity.y * dt;
        position.z += m_velocity.z * dt;

        const float groundY = heightAt ? heightAt(heightUser, position.x, position.z) : 0.0f;
        if (groundY < waterY - kEnterWater && position.y <= waterY + kSwimOffset)
        {
            splashed = true;
            landed   = false;
            return;
        }
        if (m_velocity.y <= 0.0f && position.y <= groundY + m_def.groundOffset)
        {
            landed     = true;
            position.y = groundY + m_def.groundOffset;
        }
    }

} // namespace Dark::Combat
