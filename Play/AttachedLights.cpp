#include "Play/AttachedLights.h"

#include "AI/AiComponents.h"
#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"

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

} // namespace Dark
