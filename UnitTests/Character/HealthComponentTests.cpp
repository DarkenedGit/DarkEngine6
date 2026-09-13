#include <gtest/gtest.h>

#include "Character/HealthComponent.h"
#include "ECS/World.h"

using Dark::Entity;
using Dark::HealthComponent;
using Dark::World;

TEST(HealthComponent, EmplaceAndSwapRemoveLeavesOtherValid)
{
    World world;
    Entity a = world.createEntity();
    Entity b = world.createEntity();
    world.emplace<HealthComponent>(a);
    world.emplace<HealthComponent>(b);
    ASSERT_NE(world.get<HealthComponent>(a), nullptr);
    world.get<HealthComponent>(a)->health.applyDamage(10.0f);
    EXPECT_NEAR(world.get<HealthComponent>(a)->health.hp(), 90.0f, 1.0e-3f);

    world.remove<HealthComponent>(b);
    EXPECT_FALSE(world.has<HealthComponent>(b));
    ASSERT_NE(world.get<HealthComponent>(a), nullptr);
    EXPECT_TRUE(world.get<HealthComponent>(a)->health.alive());
    EXPECT_NEAR(world.get<HealthComponent>(a)->health.hp(), 90.0f, 1.0e-3f);
}

TEST(HealthComponent, DestroyEntityDropsHealth)
{
    World world;
    Entity e = world.createEntity();
    world.emplace<HealthComponent>(e);
    world.destroyEntity(e);
    EXPECT_FALSE(world.has<HealthComponent>(e));
}
