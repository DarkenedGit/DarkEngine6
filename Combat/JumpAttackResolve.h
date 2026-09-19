#pragma once

#include "Combat/DamageEvent.h"

namespace Dark
{
    class World;
}

namespace Dark::Combat
{

    class CombatSystem;

    // CombatSystem::resolve loop only. Returns how many results had applied.
    int resolveJumpAttackEvents(World& world, CombatSystem& combat, const DamageEvent* events, int count);

} // namespace Dark::Combat
