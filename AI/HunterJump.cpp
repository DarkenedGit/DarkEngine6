#include "AI/HunterJump.h"

#include "AI/AttackPattern.h"
#include "Character/Health.h"
#include "Character/HealthComponent.h"
#include "Combat/DefenseComponent.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/PoiseComponent.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Scene/EntityCombat.h"

namespace Dark
{

    namespace
    {

        Combat::JumpAttackDef jumpDefFromCombat(const EntityCombat& combat)
        {
            Combat::JumpAttackDef def{};
            def.telegraphSeconds    = 0.40f;
            def.cooldown            = combat.jumpCooldown;
            def.groundOffset        = combat.jumpGroundOffset;
            def.connectDamage       = combat.jumpConnectDamage;
            def.connectPoise        = combat.jumpConnectPoise;
            def.poundDamage         = combat.jumpPoundDamage;
            def.poundPoise          = combat.jumpPoundPoise;
            def.leapVerticalSpeed   = combat.jumpLeapVertical;
            def.leapForwardSpeedMax = combat.jumpLeapForwardMax;
            def.gravity             = combat.jumpGravity;
            def.homingRate          = combat.jumpHoming;
            def.knockdownDuration   = combat.jumpKnockdown;
            def.knockdownForce      = combat.jumpKnockdownForce;
            def.teamFilter          = static_cast<uint8_t>(combat.team);
            return def;
        }

        Combat::JumpAttackDef hunterJumpBase()
        {
            return jumpDefFromCombat(entityCombatFor("hunter"));
        }

        const AI::AttackMove* jumpMove(const AI::AttackPattern* pattern)
        {
            if (!pattern || !pattern->loaded)
                return nullptr;
            for (const AI::AttackMove& move : pattern->attacks)
            {
                if (move.kind == AI::AttackKind::Jump)
                    return &move;
            }
            return nullptr;
        }

        void logMissingJumpOnce()
        {
            static bool logged = false;
            if (logged)
                return;
            logged = true;
            DE_LOG_WARN(LogCategory::AI, "Hunter jump: ai/hunter.attacks.json has no jump move; using built-in pounce");
        }

    } // namespace

    Combat::JumpAttackDef hunterJumpDefFromPattern(const AI::AttackPattern* pattern)
    {
        const bool            wolf = pattern && pattern->id == "wolf";
        Combat::JumpAttackDef def  = jumpDefFromCombat(entityCombatFor(wolf ? "wolf" : "hunter"));
        const AI::AttackMove* jump = jumpMove(pattern);
        if (!jump)
        {
            logMissingJumpOnce();
            return def;
        }
        def.telegraphSeconds = jump->windup;
        def.connectDamage    = jump->damage;
        return def;
    }

    void hunterJumpRanges(const AI::AttackPattern* pattern, float& minRange, float& maxRange)
    {
        minRange = 4.0f;
        maxRange = 9.0f;
        if (const AI::AttackMove* jump = jumpMove(pattern))
        {
            minRange = jump->minRange;
            maxRange = jump->maxRange;
        }
    }

    void applySpeciesCombat(World& world, Entity e)
    {
        const TagComponent* tag = world.get<TagComponent>(e);
        if (!tag || tag->name != "Wolf")
            return;

        static AI::AttackPattern wolfAttacks = AI::loadAttackPattern("ai/wolf.attacks.json");
        if (JumpAttackComponent* jac = world.get<JumpAttackComponent>(e))
            jac->jump.setDef(hunterJumpDefFromPattern(wolfAttacks.loaded ? &wolfAttacks : nullptr));

        const EntityCombat& combat = entityCombatFor("wolf");
        if (HealthComponent* hc = world.get<HealthComponent>(e))
        {
            if (hc->health.alive() && hc->health.hp() + 1.0e-3f >= hc->health.maxHp())
            {
                HealthSettings hs = hc->health.settings();
                hs.maxHp          = combat.healthMax;
                hs.regenPerSec    = combat.healthRegen;
                hs.regenDelay     = combat.healthRegenDelay;
                hc->health        = Health{ hs };
            }
        }
        if (Combat::PoiseComponent* poise = world.get<Combat::PoiseComponent>(e))
        {
            if (poise->poise + 1.0e-3f >= poise->maxPoise)
            {
                poise->maxPoise    = combat.poiseMax;
                poise->regenPerSec = combat.poiseRegen;
                poise->regenDelay  = combat.poiseRegenDelay;
                poise->reset();
            }
        }
        if (Combat::DefenseComponent* defense = world.get<Combat::DefenseComponent>(e))
            defense->team = static_cast<uint8_t>(combat.team);
    }

} // namespace Dark
