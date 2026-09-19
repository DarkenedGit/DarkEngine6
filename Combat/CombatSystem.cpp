#include "Combat/CombatSystem.h"

#include "Character/Health.h"
#include "Character/HealthComponent.h"
#include "Character/HitReaction.h"
#include "Combat/ArmorComponent.h"
#include "Combat/ArmorStats.h"
#include "Combat/DefenseComponent.h"
#include "Combat/PoiseComponent.h"
#include "Combat/StatusEffectComponent.h"
#include "ECS/World.h"
#include "Math/MathHelper.h"


namespace Dark::Combat
{
    using Math::Vector3f;

    Vector3f facingFromYaw(float yawRad)
    {
        return Vector3f{ sinf(yawRad), 0.0f, cosf(yawRad) };
    }

    bool isInsideBlockArc(const Vector3f& hitDir, const Vector3f& facingFlat, float blockArcDeg)
    {
        Vector3f incoming = hitDir;
        incoming.y        = 0.0f;
        Vector3f face     = facingFlat;
        face.y            = 0.0f;
        if (incoming.MagnitudeSqrd() < 1.0e-8f || face.MagnitudeSqrd() < 1.0e-8f)
            return false;
        incoming.Normalize();
        face.Normalize();
        // Defender faces the attacker when facing ≈ -incoming.
        const Vector3f fromAttacker = -incoming;
        const float    halfArcRad   = Math::DegreesToRadians(Math::Max(0.0f, blockArcDeg) * 0.5f);
        const float    minDot       = cosf(halfArcRad);
        return face.Dot(fromAttacker) >= minDot;
    }

    ResolveResult CombatSystem::resolve(World& world, const DamageEvent& ev)
    {
        ResolveResult r{};
        if (!ev.target.valid() || !world.alive(ev.target))
        {
            r.filtered = true;
            return r;
        }
        if (ev.source.valid() && ev.source.id() == ev.target.id())
        {
            r.filtered = true;
            return r;
        }

        DefenseComponent* defense = world.get<DefenseComponent>(ev.target);
        if (defense && (ev.flags & DamageFlags::SameTeamOk) == 0 && defense->team != 0 && ev.teamSource != 0
            && defense->team == ev.teamSource)
        {
            r.filtered = true;
            return r;
        }

        HealthComponent*       hpComp  = world.get<HealthComponent>(ev.target);
        HitReactionComponent*  hrComp  = world.get<HitReactionComponent>(ev.target);
        ArmorComponent*        armor   = world.get<ArmorComponent>(ev.target);
        PoiseComponent*        poise   = world.get<PoiseComponent>(ev.target);
        StatusEffectComponent* status  = world.get<StatusEffectComponent>(ev.target);

        Health*      health = hpComp ? &hpComp->health : nullptr;
        HitReaction* hitRx  = hrComp ? &hrComp->hit : nullptr;
        Vector3f     facing = facingFromYaw(defense ? defense->facingYawRad : 0.0f);
        const float  maxHp  = health ? health->maxHp() : 100.0f;

        return resolveDirect(ev, health, hitRx, defense, armor, poise, status, &facing, maxHp);
    }

    ResolveResult CombatSystem::resolveDirect(const DamageEvent& ev,
                                              Health* health,
                                              HitReaction* hitReaction,
                                              DefenseComponent* defense,
                                              ArmorComponent* armor,
                                              PoiseComponent* poise,
                                              StatusEffectComponent* status,
                                              const Vector3f* targetFacingFlat,
                                              float maxHpForSeverity)
    {
        ResolveResult r{};

        if (ev.amount < 0.0f)
        {
            r.filtered = true;
            return r;
        }
        if (health && !health->alive())
        {
            r.filtered = true;
            return r;
        }

        if (defense && defense->inIFrame())
        {
            r.iframe = true;
            return r;
        }

        Vector3f facing{ 0.0f, 0.0f, 1.0f };
        if (targetFacingFlat)
            facing = *targetFacingFlat;

        const bool downed   = status && status->knockedDown();
        const bool canParry = (ev.flags & DamageFlags::CanParry) != 0;
        const bool canBlock = (ev.flags & DamageFlags::CanBlock) != 0;

        if (canParry && !downed && defense && defense->inParryWindow()
            && isInsideBlockArc(ev.hitDir, facing, defense->blockArcDeg))
        {
            r.parried  = true;
            r.applied  = true;
            r.severity = Severity::Tick;
            defense->parrying        = false;
            defense->parryWindowLeft = 0.0f;
            return r;
        }

        float dmg = ev.amount;

        if (canBlock && !downed && defense && defense->blocking
            && isInsideBlockArc(ev.hitDir, facing, defense->blockArcDeg))
        {
            if (defense->stamina >= defense->blockStaminaCost)
            {
                defense->stamina -= defense->blockStaminaCost;
                const ArmorStats& stats = armor ? armor->stats : ArmorStats{};
                const float       mit   = Math::Clamp(stats.blockMitigation, 0.0f, 1.0f);
                dmg *= mit;
                r.blocked = true;
            }
        }

        const bool ignoreArmor = (ev.flags & DamageFlags::IgnoresArmor) != 0 || isTrue(ev.type);
        ArmorStats stats{};
        if (armor)
            stats = armor->stats;
        dmg = mitigateTyped(dmg, ev.type, stats, ev.armorPen, ignoreArmor);
        if (dmg < 0.0f)
            dmg = 0.0f;

        bool poiseBroke = false;
        if (poise && ev.poiseDamage > 0.0f)
            poiseBroke = poise->applyPoiseDamage(ev.poiseDamage);
        r.poiseBroke = poiseBroke;

        bool killed = false;
        if (health && dmg > 0.0f)
            killed = health->applyDamage(dmg);
        r.killed      = killed;
        r.finalDamage = dmg;
        r.applied     = true;

        const bool mappedKnockdown = (ev.flags & DamageFlags::Knockdown) != 0;
        if (status && !r.blocked
            && (ev.flags & (DamageFlags::SoftCc | DamageFlags::HardCc | DamageFlags::Knockdown)) != 0)
        {
            CcCategory cat    = CcCategory::Root;
            bool       hard   = false;
            float      defDur = 0.8f;
            if (mappedKnockdown)
            {
                cat    = CcCategory::Knockdown;
                hard   = true;
                defDur = 1.4f;
            }
            else if ((ev.flags & DamageFlags::HardCc) != 0)
            {
                cat    = CcCategory::Stun;
                hard   = true;
                defDur = 1.2f;
            }
            const float dur = ev.statusDuration > 0.0f ? ev.statusDuration : defDur;
            r.ccDuration    = status->applyCc(cat, dur, hard, ev.statusId, ev.statusMagnitude);
        }

        const float maxHp = maxHpForSeverity > 1.0e-6f ? maxHpForSeverity : 100.0f;
        const float norm  = dmg / maxHp;
        r.severity        = severityFromNormalized(norm, poiseBroke, killed);

        if (hitReaction && !r.blocked)
        {
            const bool hyper = poise && poise->hyperArmor;
            if (mappedKnockdown && r.ccDuration > 0.0f)
            {
                if (!hyper)
                {
                    HitReactionSettings kd{};
                    kd.stunSeconds       = r.ccDuration;
                    kd.knockbackDistance = ev.statusMagnitude > 0.0f ? ev.statusMagnitude : 2.4f;
                    kd.knockbackSeconds  = 0.22f;
                    kd.horizontalOnly    = true;
                    hitReaction->setSettings(kd);
                    hitReaction->apply(ev.hitDir);
                }
            }
            else if (r.severity != Severity::Tick && !hyper)
            {
                hitReaction->setSettings(hitReactionForSeverity(r.severity));
                hitReaction->apply(ev.hitDir);
            }
        }

        return r;
    }

} // namespace Dark::Combat
