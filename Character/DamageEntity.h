#pragma once

#include "Character/HealthComponent.h"
#include "Combat/PlayerMode.h"
#include "ECS/Entity.h"
#include "ECS/World.h"

namespace Dark
{

    // True when hit points fall. God mode refuses the write. Reaper stays on the combat resolve path.
    inline bool damageEntity(World& world, Entity e, float amount, bool* killed = nullptr)
    {
        if (killed)
            *killed = false;
        if (!e.valid() || !world.alive(e) || !(amount > 0.0f))
            return false;
        if (const Combat::PlayerModeComponent* mode = world.get<Combat::PlayerModeComponent>(e))
        {
            if (mode->godMode)
                return false;
        }
        HealthComponent* hp = world.get<HealthComponent>(e);
        if (!hp || !hp->health.alive())
            return false;
        const float before = hp->health.hp();
        const bool  dead   = hp->health.applyDamage(amount);
        if (killed)
            *killed = dead;
        return hp->health.hp() < before;
    }

} // namespace Dark
