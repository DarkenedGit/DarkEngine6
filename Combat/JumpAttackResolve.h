#pragma once

#include "Combat/DamageEvent.h"

namespace Dark
{
    class World;
}

namespace Dark::Combat
{

    class CombatSystem;
    struct ResolveResult;

    // results (optional) receives one entry per event.
    int resolveJumpAttackEvents(World& world, CombatSystem& combat, const DamageEvent* events, int count, ResolveResult* results = nullptr);

} // namespace Dark::Combat
