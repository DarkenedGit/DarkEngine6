#include <gtest/gtest.h>

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Particles/ParticleComponents.h"
#include "Particles/ParticleTick.h"

using Dark::Entity;
using Dark::ParticleEmitterComponent;
using Dark::TransformComponent;
using Dark::World;
using Dark::ensureParticleRuntime;
using Dark::tickParticleEmitters;

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

TEST(ParticleEmitterComponent, TickSetsTransformFromComponent)
{
    World world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = Dark::Math::Vector3f{ 3.0f, 1.5f, -2.0f };
    world.emplace<TransformComponent>(e, xf);
    ParticleEmitterComponent pe{};
    pe.desc.name         = "Authored";
    pe.desc.emissionRate = 50.0f;
    pe.playing           = true;
    world.emplace<ParticleEmitterComponent>(e, std::move(pe));

    tickParticleEmitters(world, 0.25f);
    ParticleEmitterComponent* c = world.get<ParticleEmitterComponent>(e);
    ASSERT_NE(c, nullptr);
    ASSERT_NE(c->runtime, nullptr);
    EXPECT_NEAR(c->runtime->position().x, 3.0f, 1.0e-4f);
    EXPECT_NEAR(c->runtime->position().y, 1.5f, 1.0e-4f);
    EXPECT_NEAR(c->runtime->position().z, -2.0f, 1.0e-4f);
}
