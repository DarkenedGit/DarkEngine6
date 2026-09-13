#include <gtest/gtest.h>

#include "Character/PlayerMotorComponent.h"
#include "ECS/World.h"

using Dark::Entity;
using Dark::PlayerMotorComponent;
using Dark::World;

TEST(PlayerMotorComponent, EmplaceAndDestroy)
{
    World world;
    Entity e = world.createEntity();
    world.emplace<PlayerMotorComponent>(e);
    ASSERT_NE(world.get<PlayerMotorComponent>(e), nullptr);
    world.get<PlayerMotorComponent>(e)->motor.reset();
    world.destroyEntity(e);
    EXPECT_FALSE(world.has<PlayerMotorComponent>(e));
}
