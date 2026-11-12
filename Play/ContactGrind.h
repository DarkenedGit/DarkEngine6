#pragma once

#include "AI/AiComponents.h"
#include "Character/HealthComponent.h"
#include "Character/HitReaction.h"
#include "Combat/CombatSystem.h"
#include "Combat/DefenseComponent.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/StatusEffectComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/AABox3f.h"
#include "Math/Sphere3f.h"
#include "Scene/EntityCombat.h"
#include "Weapons/Weapon.h"

#include <cmath>

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
    // Solids default empty so older callers still compile. A stunned, hard-CC'd, or recovering agent does not grind.
    inline ContactGrindResult applyContactGrind(World& world, Combat::CombatSystem& combat, Entity player, const Math::Vector3f& playerPos, float dt,
                                                const Math::AABox3f* solids = nullptr, int solidCount = 0, const Math::Sphere3f* spheres = nullptr, int sphereCount = 0)
    {
        ContactGrindResult result;
        HealthComponent*   playerHp = player.valid() ? world.get<HealthComponent>(player) : nullptr;
        const float        before   = playerHp ? playerHp->health.hp() : 0.0f;
        float              best     = 1.0e12f;

        world.each<AiAgentComponent>([&](Entity e, AiAgentComponent& ai) {
            const HealthComponent*    hp  = world.get<HealthComponent>(e);
            const TransformComponent* hxf = world.get<TransformComponent>(e);
            if (!hp || !hp->health.alive() || !hxf || ai.passive)
                return;
            const TagComponent* tag = world.get<TagComponent>(e);
            const bool          wolf = tag && tag->name == "Wolf";
            const EntityCombat& agent = entityCombatFor(wolf ? "wolf" : "hunter");
            const float         reach = agent.contactStandoff;
            const float         reach2 = reach * reach;
            const float         dx = hxf->position.x - playerPos.x;
            const float         dy = hxf->position.y - playerPos.y;
            const float         dz = hxf->position.z - playerPos.z;
            const float         d2 = dx * dx + dz * dz;
            if (d2 > reach2)
                return;
            if (agent.meleeVertical >= 0.0f && std::fabs(dy) > agent.meleeVertical)
                return;
            if (const HitReactionComponent* hr = world.get<HitReactionComponent>(e); hr && hr->hit.stunned())
                return;
            if (const Combat::StatusEffectComponent* st = world.get<Combat::StatusEffectComponent>(e); st && st->hasHardCc())
                return;
            if (const JumpAttackComponent* jac = world.get<JumpAttackComponent>(e); jac && jac->jump.phase() == Combat::JumpAttackPhase::Recover)
                return;
            if (weaponSegmentBlocked(solids, solidCount, spheres, sphereCount, hxf->position, playerPos))
                return;

            Math::Vector3f hitDir{ playerPos.x - hxf->position.x, 0.0f, playerPos.z - hxf->position.z };
            if (hitDir.MagnitudeSqrd() > 1.0e-8f)
                hitDir.Normalize();
            else
                hitDir = Math::Vector3f{ 0.0f, 0.0f, 1.0f };

            Combat::DamageEvent ev{};
            ev.source   = e;
            ev.target   = player;
            ev.amount   = agent.contactDps * dt;
            ev.type     = Combat::DamageType::Slash;
            ev.hitDir   = hitDir;
            ev.hitPoint = playerPos;
            ev.flags    = Combat::DamageFlags::CanBlock;
            if (const Combat::DefenseComponent* defense = world.get<Combat::DefenseComponent>(e))
                ev.teamSource = defense->team;
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
