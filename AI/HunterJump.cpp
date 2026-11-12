#include "AI/HunterJump.h"

#include "AI/AttackPattern.h"
#include "Core/Log.h"

namespace Dark
{

    namespace
    {

        Combat::JumpAttackDef hunterJumpBase()
        {
            Combat::JumpAttackDef def{};
            def.telegraphSeconds    = 0.40f;
            def.cooldown            = 3.5f;
            def.groundOffset        = 1.0f;
            def.connectDamage       = 22.0f;
            def.poundDamage         = 14.0f;
            def.leapVerticalSpeed   = 10.0f;
            def.leapForwardSpeedMax = 12.0f;
            def.gravity             = 24.0f;
            def.homingRate          = 1.5f;
            def.knockdownDuration   = 0.8f;
            def.knockdownForce      = 1.0f;
            return def;
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
        Combat::JumpAttackDef def = hunterJumpBase();
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

} // namespace Dark
