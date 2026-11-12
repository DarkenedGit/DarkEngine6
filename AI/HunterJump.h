#pragma once

#include "Combat/JumpAttackDef.h"

namespace Dark::AI
{
    struct AttackPattern;
}

namespace Dark
{

    // Built-in pounce. JSON overlays windup, connect damage, and the min/max range when a jump move is present.
    // Cooldown, pound, leap, gravity, homing, and knockdown stay on this def. A missing file keeps these numbers.
    Combat::JumpAttackDef hunterJumpDefFromPattern(const AI::AttackPattern* pattern);

    // Fallback is 4 m to 9 m. A loaded jump move replaces both ends. Does not log.
    void hunterJumpRanges(const AI::AttackPattern* pattern, float& minRange, float& maxRange);

} // namespace Dark
