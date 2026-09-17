#include "Combat/SpellCaster.h"

#include "Combat/DamageEvent.h"
#include "Math/MathHelper.h"

namespace Dark::Combat
{
    using Math::Vector3f;

    SpellCaster::SpellCaster(const SpellDef& def)
        : m_def(def)
    {
    }

    void SpellCaster::setDef(const SpellDef& def)
    {
        m_def = def;
    }

    void SpellCaster::setMana(float mana, float manaMax)
    {
        m_manaMax = manaMax > 1.0f ? manaMax : 1.0f;
        m_mana    = Math::Clamp(mana, 0.0f, m_manaMax);
    }

    void SpellCaster::enter(SpellPhase phase)
    {
        m_phase = phase;
        switch (phase)
        {
        case SpellPhase::Windup:
            m_phaseLeft = m_def.windupSeconds;
            break;
        case SpellPhase::Channel:
            m_phaseLeft = m_def.channelSeconds;
            break;
        case SpellPhase::Release:
            m_phaseLeft = m_def.releaseSeconds;
            break;
        case SpellPhase::Recovery:
            m_phaseLeft = m_def.recoverySeconds;
            break;
        case SpellPhase::Idle:
        default:
            m_phaseLeft = 0.0f;
            break;
        }
    }

    bool SpellCaster::trySpendCost(bool atRelease)
    {
        if (m_costSpent)
            return true;
        if (m_def.commitCostOnRelease != atRelease)
            return true;
        if (m_mana < m_def.manaCost)
            return false;
        m_mana -= m_def.manaCost;
        m_costSpent = true;
        return true;
    }

    bool SpellCaster::beginCast(const SpellCastRequest& req)
    {
        if (m_phase != SpellPhase::Idle || m_cooldown > 0.0f)
            return false;
        if (!m_def.commitCostOnRelease && m_mana < m_def.manaCost)
            return false;
        m_req       = req;
        m_costSpent = false;
        if (!m_def.commitCostOnRelease && !trySpendCost(false))
            return false;
        enter(SpellPhase::Windup);
        return true;
    }

    bool SpellCaster::interrupt(bool hardCc)
    {
        if (m_phase != SpellPhase::Windup && m_phase != SpellPhase::Channel)
            return false;
        switch (m_def.interruptPolicy)
        {
        case SpellInterruptPolicy::Never:
            return false;
        case SpellInterruptPolicy::SoftCc:
            break;
        case SpellInterruptPolicy::HardCc:
            if (!hardCc)
                return false;
            break;
        case SpellInterruptPolicy::DamageOver:
            break;
        }
        enter(SpellPhase::Idle);
        m_cooldown = 0.0f;
        return true;
    }

    void SpellCaster::tick(float dt)
    {
        if (dt < 0.0f)
            dt = 0.0f;
        if (m_cooldown > 0.0f)
            m_cooldown = Math::Max(0.0f, m_cooldown - dt);

        if (m_proj.alive)
        {
            m_proj.pos += m_proj.vel * dt;
            m_proj.lifetime -= dt;
            if (m_proj.lifetime <= 0.0f)
                m_proj.alive = false;
        }

        if (m_phase == SpellPhase::Idle)
            return;

        m_phaseLeft -= dt;
        if (m_phaseLeft > 0.0f)
            return;

        switch (m_phase)
        {
        case SpellPhase::Windup:
            if (m_def.channelSeconds > 0.0f)
                enter(SpellPhase::Channel);
            else
                enter(SpellPhase::Release);
            break;
        case SpellPhase::Channel:
            enter(SpellPhase::Release);
            break;
        case SpellPhase::Release:
            if (!trySpendCost(true))
            {
                enter(SpellPhase::Idle);
                break;
            }
            if (m_def.targeting == SpellTargeting::Projectile)
            {
                Vector3f dir = m_req.direction;
                if (dir.MagnitudeSqrd() < 1.0e-8f)
                    dir = Vector3f{ 0.0f, 0.0f, 1.0f };
                else
                    dir.Normalize();
                m_proj.alive          = true;
                m_proj.pos            = m_req.origin;
                m_proj.vel            = dir * m_def.projectileSpeed;
                m_proj.lifetime       = m_def.range / Math::Max(m_def.projectileSpeed, 1.0f);
                m_proj.damage         = m_def.damage;
                m_proj.type           = m_def.damageType;
                m_proj.poiseDamage    = m_def.poiseDamage;
                m_proj.armorPen       = m_def.armorPen;
                m_proj.flags          = m_def.damageFlags;
                m_proj.source         = m_req.caster;
                m_proj.intendedTarget = m_req.target;
            }
            m_cooldown = m_def.cooldown;
            enter(SpellPhase::Recovery);
            break;
        case SpellPhase::Recovery:
            enter(SpellPhase::Idle);
            break;
        default:
            enter(SpellPhase::Idle);
            break;
        }
    }

    bool SpellCaster::tryResolveProjectileHit(Entity target, const Vector3f& targetPos, float radius, DamageEvent& outEvent)
    {
        if (!m_proj.alive || !target.valid())
            return false;
        const Vector3f d = targetPos - m_proj.pos;
        const float    r = radius > 0.0f ? radius : 0.5f;
        if (d.MagnitudeSqrd() > r * r)
            return false;

        outEvent              = DamageEvent{};
        outEvent.source       = m_proj.source;
        outEvent.target       = target;
        outEvent.type         = m_proj.type;
        outEvent.amount       = m_proj.damage;
        outEvent.poiseDamage  = m_proj.poiseDamage;
        outEvent.armorPen     = m_proj.armorPen;
        outEvent.hitPoint     = targetPos;
        outEvent.hitDir       = m_proj.vel.MagnitudeSqrd() > 1.0e-8f ? m_proj.vel : Vector3f{ 0.0f, 0.0f, 1.0f };
        if (outEvent.hitDir.MagnitudeSqrd() > 1.0e-8f)
            outEvent.hitDir.Normalize();
        outEvent.flags        = m_proj.flags != 0 ? m_proj.flags : (DamageFlags::CanBlock | DamageFlags::CanParry);
        m_proj.alive          = false;
        return true;
    }

} // namespace Dark::Combat
