#include "Content/ArchetypeParams.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/CharacterAttach.h"

#include <gtest/gtest.h>

#include <string>

using namespace Dark;

TEST(NpcMovementProfile, ModProfileWinsOverWolfTag)
{
    World  world;
    Entity modWolf = world.createEntity();
    world.emplace<TagComponent>(modWolf, "Wolf");
    world.emplace<ArchetypeParamsComponent>(modWolf).movementType = "frost_wolf";
    EXPECT_EQ(std::string(npcMovementProfile(world, modWolf, true)), "frost_wolf");

    Entity wolf = world.createEntity();
    world.emplace<TagComponent>(wolf, "Wolf");
    EXPECT_EQ(std::string(npcMovementProfile(world, wolf, true)), "wolf");

    Entity hunter = world.createEntity();
    world.emplace<ArchetypeParamsComponent>(hunter);
    EXPECT_EQ(std::string(npcMovementProfile(world, hunter, false)), "hunter");
}
