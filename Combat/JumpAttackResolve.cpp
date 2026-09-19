#include "Combat/JumpAttackResolve.h"

#include "Combat/CombatSystem.h"
#include "Core/Log.h"
#include "ECS/World.h"

namespace Dark::Combat
{

    int resolveJumpAttackEvents(World& world, CombatSystem& combat, const DamageEvent* events, int count)
    {
        if (count <= 0)
            return 0;
        DE_ASSERT(events != nullptr);
        if (!events)
            return 0;

        int applied = 0;
        for (int i = 0; i < count; ++i)
        {
            if (combat.resolve(world, events[i]).applied)
                ++applied;
        }
        return applied;
    }

} // namespace Dark::Combat
