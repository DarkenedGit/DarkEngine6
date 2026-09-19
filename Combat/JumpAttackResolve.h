#pragma once

#include "Combat/DamageEvent.h"

namespace Dark
{
    class World;
}

namespace Dark::Combat
{

    class CombatSystem;

    int resolveJumpAttackEvents(World& world, CombatSystem& combat, const DamageEvent* events, int count);

} // namespace Dark::Combat
