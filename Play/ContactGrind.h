#pragma once

#include "AI/AiComponents.h"
#include "Character/HealthComponent.h"
#include "Combat/CombatSystem.h"
#include "ECS/Components.h"
#include "ECS/World.h"

namespace Dark
{

    inline constexpr float kContactStandoff = 2.25f;
    inline constexpr float kContactDps      = 12.0f;

    struct ContactGrindResult
    {
        bool           killed  = false;
        bool           damaged = false;
        Math::Vector3f away{ 0.0f, 0.0f, 0.0f };
    };

    // Standing in reach is not a swing. CanBlock only, so a parry window is not spent on proximity.
    inline ContactGrindResult applyContactGrind(World& world, Combat::CombatSystem& combat, Entity player, const Math::Vector3f& playerPos, float dt)
    {
        ContactGrindResult result;
        HealthComponent*   playerHp = player.valid() ? world.get<HealthComponent>(player) : nullptr;
        const float        before   = playerHp ? playerHp->health.hp() : 0.0f;
        const float        reach2   = kContactStandoff * kContactStandoff;
        float              best     = reach2;

        world.each<AiAgentComponent>([&](Entity e, AiAgentComponent& ai) {
            const HealthComponent*    hp  = world.get<HealthComponent>(e);
            const TransformComponent* hxf = world.get<TransformComponent>(e);
            if (!hp || !hp->health.alive() || !hxf || ai.passive)
                return;
            const float dx = hxf->position.x - playerPos.x;
            const float dz = hxf->position.z - playerPos.z;
            const float d2 = dx * dx + dz * dz;
            if (d2 > reach2)
                return;

            Math::Vector3f hitDir{ playerPos.x - hxf->position.x, 0.0f, playerPos.z - hxf->position.z };
            if (hitDir.MagnitudeSqrd() > 1.0e-8f)
                hitDir.Normalize();
            else
                hitDir = Math::Vector3f{ 0.0f, 0.0f, 1.0f };

            Combat::DamageEvent ev{};
            ev.source   = e;
            ev.target   = player;
            ev.amount   = kContactDps * dt;
            ev.type     = Combat::DamageType::Slash;
            ev.hitDir   = hitDir;
            ev.hitPoint = playerPos;
            ev.flags    = Combat::DamageFlags::CanBlock;
            if (combat.resolve(world, ev).killed)
                result.killed = true;
            if (d2 <= best)
            {
                best        = d2;
                result.away = Math::Vector3f{ -dx, 0.0f, -dz };
            }
        });

        if (playerHp && playerHp->health.hp() < before)
            result.damaged = true;
        return result;
    }

} // namespace Dark
