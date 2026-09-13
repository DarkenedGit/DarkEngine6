#include <gtest/gtest.h>

#include "ECS/World.h"
#include "Particles/ParticleComponents.h"

using Dark::Entity;
using Dark::ParticleEmitterComponent;
using Dark::World;
using Dark::ensureParticleRuntime;

TEST(ParticleEmitterComponent, EmplaceCreatesRuntimeAndDestroyFrees)
{
    World world;
    Entity e = world.createEntity();
    ParticleEmitterComponent pe{};
    pe.desc.name = "Test";
    world.emplace<ParticleEmitterComponent>(e, std::move(pe));
    ParticleEmitterComponent* c = world.get<ParticleEmitterComponent>(e);
    ASSERT_NE(c, nullptr);
    ensureParticleRuntime(*c);
    ASSERT_NE(c->runtime, nullptr);
    EXPECT_TRUE(c->runtime->isPlaying());
    world.destroyEntity(e);
    EXPECT_FALSE(world.has<ParticleEmitterComponent>(e));
}
