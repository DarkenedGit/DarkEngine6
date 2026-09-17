#pragma once

#include "Combat/DamageEvent.h"
#include "Combat/SpellDef.h"
#include "ECS/Entity.h"
#include "Math/Vector3f.h"

namespace Dark
{
    class World;
}

namespace Dark::Combat
{

    struct SpellCastRequest
    {
        Entity         caster{};
        Math::Vector3f origin{};
        Math::Vector3f direction{};
        Entity         target{};
    };

    struct SpellProjectile
    {
        bool           alive = false;
        Math::Vector3f pos{};
        Math::Vector3f vel{};
        float          lifetime = 0.f;
        float          damage   = 0.f;
        DamageType     type     = DamageType::Fire;
        float          poiseDamage = 0.f;
        float          armorPen = 0.f;
        uint32_t       flags    = 0;
        Entity         source{};
        Entity         intendedTarget{};
    };

    // Minimal caster: windup → optional channel → release (spawn projectile) → recovery.
    class SpellCaster
    {
    public:
        SpellCaster() = default;
        explicit SpellCaster(const SpellDef& def);

        void                   setDef(const SpellDef& def);
        const SpellDef&        def() const { return m_def; }
        SpellPhase             phase() const { return m_phase; }
        float                  mana() const { return m_mana; }
        float                  manaMax() const { return m_manaMax; }
        void                   setMana(float mana, float manaMax);
        const SpellProjectile& projectile() const { return m_proj; }
        bool                   busy() const { return m_phase != SpellPhase::Idle; }

        bool beginCast(const SpellCastRequest& req);
        // Hard CC (or SoftCc when policy allows) cancels windup/channel.
        bool interrupt(bool hardCc);
        // Advance phases; on release may spawn a simple projectile-like payload.
        void tick(float dt);
        // If projectile overlaps target (sphere radius), build DamageEvent and clear proj.
        bool tryResolveProjectileHit(Entity target, const Math::Vector3f& targetPos, float radius, DamageEvent& outEvent);

    private:
        void enter(SpellPhase phase);
        bool trySpendCost(bool atRelease);

        SpellDef         m_def{};
        SpellPhase       m_phase = SpellPhase::Idle;
        float            m_phaseLeft = 0.f;
        float            m_cooldown  = 0.f;
        float            m_mana      = 100.f;
        float            m_manaMax   = 100.f;
        SpellCastRequest m_req{};
        SpellProjectile  m_proj{};
        bool             m_costSpent = false;
    };

} // namespace Dark::Combat
