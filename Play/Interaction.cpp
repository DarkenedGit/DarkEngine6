#include "Play/Interaction.h"

#include "AI/AiComponents.h"
#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/PowerComponents.h"
#include "Play/Carry.h"

#include <cmath>

namespace Dark
{

    void setInteractable(World& world, Entity e, InteractKind kind, float range)
    {
        if (!e.valid() || !world.alive(e))
            return;
        if (InteractableComponent* have = world.get<InteractableComponent>(e))
        {
            have->kind  = kind;
            have->range = range;
            return;
        }
        InteractableComponent mark;
        mark.kind  = kind;
        mark.range = range;
        world.emplace<InteractableComponent>(e, mark);
    }

    void clearInteractable(World& world, Entity e)
    {
        if (e.valid() && world.alive(e) && world.has<InteractableComponent>(e))
            world.remove<InteractableComponent>(e);
    }

    bool isInteractGuard(World& world, Entity e)
    {
        const InteractableComponent* mark = e.valid() ? world.get<InteractableComponent>(e) : nullptr;
        return mark && mark->kind == InteractKind::Guard;
    }

    PlayInteractPick pickInteractable(World& world, Entity player, const Math::Vector3f& eye, const Math::Vector3f& lookIn, float minAim)
    {
        PlayInteractPick best{};
        if (carriedBody(world).valid())
            return best;
        const TransformComponent* pxf = player.valid() ? world.get<TransformComponent>(player) : nullptr;
        if (!pxf)
            return best;
        Math::Vector3f look = lookIn;
        if (look.MagnitudeSqrd() < 1.0e-6f)
            return best;
        look.Normalize();
        float bestAim = minAim;
        auto  consider = [&](PlayInteract kind, Entity e, const Math::Vector3f& at, float range) {
            if (Math::Vector3f{ at.x - pxf->position.x, 0.0f, at.z - pxf->position.z }.MagnitudeSqrd() > range * range)
                return;
            Math::Vector3f to = at - eye;
            if (to.MagnitudeSqrd() < 1.0e-6f)
                return;
            to.Normalize();
            const float aim = look.Dot(to);
            if (aim <= bestAim)
                return;
            bestAim = aim;
            best    = PlayInteractPick{ kind, e };
        };
        world.each<InteractableComponent>([&](Entity e, InteractableComponent& mark) {
            const TransformComponent* xf = world.get<TransformComponent>(e);
            if (mark.kind == InteractKind::Guard)
            {
                const AiAgentComponent* ai = world.get<AiAgentComponent>(e);
                const HealthComponent*  hp = world.get<HealthComponent>(e);
                if (!ai || !hp || !xf || hp->health.alive() || ai->carried)
                    return;
                consider(PlayInteract::Body, e, xf->position, mark.range);
                return;
            }
            if (mark.kind == InteractKind::Power)
            {
                Math::Vector3f at = xf ? xf->position : Math::Vector3f{};
                if (!xf)
                {
                    const PowerBoxComponent* box = world.get<PowerBoxComponent>(e);
                    if (!box)
                        return;
                    at = box->pos;
                }
                consider(PlayInteract::Power, e, at, mark.range);
                return;
            }
            if (!xf)
                return;
            if (mark.kind == InteractKind::Item)
            {
                const ItemPickupComponent* pickup = world.get<ItemPickupComponent>(e);
                if (pickup && !pickup->item.empty())
                    consider(PlayInteract::Item, e, xf->position, mark.range);
                return;
            }
            if (mark.kind == InteractKind::Loot)
            {
                const LootComponent* loot = world.get<LootComponent>(e);
                if (loot && itemCount(loot->contents) > 0)
                    consider(PlayInteract::Item, e, xf->position, mark.range);
            }
        });
        return best;
    }

    Entity findTakedownTarget(World& world, Entity player, bool enabled)
    {
        if (!enabled || carriedBody(world).valid())
            return {};
        const TransformComponent* pxf = player.valid() ? world.get<TransformComponent>(player) : nullptr;
        if (!pxf)
            return {};
        Entity best{};
        float  bestD = kGuardTakedownRange * kGuardTakedownRange;
        world.each<InteractableComponent>([&](Entity e, InteractableComponent& mark) {
            if (mark.kind != InteractKind::Guard)
                return;
            const AiAgentComponent*   ai  = world.get<AiAgentComponent>(e);
            const TransformComponent* gxf = world.get<TransformComponent>(e);
            const HealthComponent*    hp  = world.get<HealthComponent>(e);
            if (!ai || !gxf || !hp || !hp->health.alive() || ai->aware.alertness == AI::Alertness::Alert)
                return;
            const Math::Vector3f to{ pxf->position.x - gxf->position.x, 0.0f, pxf->position.z - gxf->position.z };
            const float          d = to.MagnitudeSqrd();
            Math::Vector3f       fwd{ ai->forward.x, 0.0f, ai->forward.z };
            if (d > bestD || d < 1.0e-6f || std::fabs(pxf->position.y - gxf->position.y) > 1.2f || fwd.MagnitudeSqrd() < 1.0e-6f)
                return;
            fwd.Normalize();
            if (fwd.Dot(to * (1.0f / std::sqrt(d))) > -0.2f)
                return;
            best  = e;
            bestD = d;
        });
        return best;
    }

} // namespace Dark
