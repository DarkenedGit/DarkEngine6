#pragma once

#include "Character/HealthComponent.h"
#include "ECS/World.h"
#include "Render/DecalLifeSource.h"

namespace Dark
{

    // Decal life from HealthComponent; entities without one have no life state.
    class HealthDecalLife final : public IDecalLifeSource
    {
    public:
        bool life(World& world, Entity entity, bool& alive) const override
        {
            const HealthComponent* hp = world.get<HealthComponent>(entity);
            if (!hp)
                return false;
            alive = hp->health.alive();
            return true;
        }
    };

    inline const HealthDecalLife& healthDecalLife()
    {
        static const HealthDecalLife source;
        return source;
    }

} // namespace Dark
