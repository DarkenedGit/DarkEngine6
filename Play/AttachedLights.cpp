#include "Play/AttachedLights.h"

#include "AI/AiComponents.h"
#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"

#include <vector>

namespace Dark
{
    using namespace Math;

    void tickAttachedLights(World& world, float sunElevation)
    {
        const bool night = sunElevation < 0.15f;
        world.each<AttachedLightComponent>([&](Entity torch, AttachedLightComponent& att) {
            LocalLightComponent*      beam = world.get<LocalLightComponent>(torch);
            TransformComponent*       lxf  = world.get<TransformComponent>(torch);
            const TransformComponent* gxf  = world.alive(att.owner) ? world.get<TransformComponent>(att.owner) : nullptr;
            const AiAgentComponent*   ai   = gxf ? world.get<AiAgentComponent>(att.owner) : nullptr;
            const HealthComponent*    hp   = gxf ? world.get<HealthComponent>(att.owner) : nullptr;
            if (!beam || !lxf)
                return;
            beam->enabled = (!att.nightOnly || night) && ai && hp && hp->health.alive();
            if (!beam->enabled)
                return;
            Vector3f fwd{ ai->forward.x, 0.0f, ai->forward.z };
            if (fwd.MagnitudeSqrd() < 1.0e-6f)
                fwd = Vector3f::Z_AXIS;
            fwd.Normalize();
            lxf->position = gxf->position + Vector3f{ 0.0f, att.height, 0.0f } + fwd * att.forward;
            Vector3f aim  = fwd + Vector3f{ 0.0f, -att.aimDrop, 0.0f };
            aim.Normalize();
            lxf->rotation = Quaternion::FromLookRotation(aim, Vector3f::Y_AXIS);
        });
    }

    Entity spawnGuardTorch(World& world, Entity owner, const Vector3f& at)
    {
        const Entity torch = world.createEntity();
        world.emplace<TransformComponent>(torch, at, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
        AttachedLightComponent att{};
        att.owner         = owner;
        auto& beam        = world.emplace<LocalLightComponent>(torch);
        beam.type         = LocalLightType::Spot;
        beam.color        = Vector3f{ 1.0f, 0.95f, 0.85f };
        beam.intensity    = att.intensity;
        beam.range        = att.range;
        beam.innerConeDeg = att.innerConeDeg;
        beam.outerConeDeg = att.outerConeDeg;
        beam.enabled      = false;
        beam.castShadow   = true;
        world.emplace<AttachedLightComponent>(torch, att);
        return torch;
    }

    void destroyGuardTorches(World& world)
    {
        std::vector<Entity> torches;
        world.each<AttachedLightComponent>([&](Entity torch, AttachedLightComponent&) { torches.push_back(torch); });
        for (Entity torch : torches)
            world.destroyEntity(torch);
    }

} // namespace Dark
