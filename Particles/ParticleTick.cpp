#include "Particles/ParticleTick.h"

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Particles/ParticleComponents.h"

namespace Dark
{

    void tickParticleEmitters(World& world, float dt)
    {
        world.each<ParticleEmitterComponent>([&](Entity e, ParticleEmitterComponent& pe) {
            ensureParticleRuntime(pe);
            if (const TransformComponent* xf = world.get<TransformComponent>(e))
                pe.runtime->setTransform(xf->position, xf->rotation);
            pe.runtime->update(dt);
        });
    }

} // namespace Dark
