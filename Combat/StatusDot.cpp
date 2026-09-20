#include "Combat/StatusDot.h"

#include "Combat/CombatSystem.h"
#include "Combat/StatusEffectComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"

namespace Dark::Combat
{

    int harvestAndResolveDots(World& world, CombatSystem& combat)
    {
        int applied = 0;
        world.each<StatusEffectComponent>([&](Entity e, StatusEffectComponent& st) {
            DamageEvent local[StatusEffectComponent::kMaxStatus]{};
            const int   got = st.harvestDot(local, StatusEffectComponent::kMaxStatus, e);
            const TransformComponent* xf = world.get<TransformComponent>(e);
            for (int i = 0; i < got; ++i)
            {
                local[i].target = e;
                if (xf)
                    local[i].hitPoint = xf->position;
                if (combat.resolve(world, local[i]).applied)
                    ++applied;
            }
        });
        return applied;
    }

} // namespace Dark::Combat
