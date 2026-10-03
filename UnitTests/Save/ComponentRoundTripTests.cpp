#include <gtest/gtest.h>

#include "SaveTestWorld.h"

#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"

using namespace Dark;

namespace
{
    std::string capture(Save::SaveSystem& save, World& world)
    {
        armSave(save, testHost());
        std::string json;
        EXPECT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
        return json;
    }
}

TEST(ComponentRoundTrip, TransformFloatsMatchExactly)
{
    World world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = { 1.25f, 2.5f, -3.75f };
    xf.rotation = { 1.0f, 0.0f, 0.0f, 0.0f };
    xf.scale    = { 2.0f, 2.0f, 2.0f };
    world.emplace<TransformComponent>(e, xf);
    stampProceduralId(world, e, "unit/box", "box");
    Save::SaveSystem save;
    const std::string json = capture(save, world);
    auto* live = world.get<TransformComponent>(e);
    live->position = { 0.0f, 0.0f, 0.0f };
    live->scale = { 1.0f, 1.0f, 1.0f };
    ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    EXPECT_EQ(live->position.x, 1.25f);
    EXPECT_EQ(live->position.y, 2.5f);
    EXPECT_EQ(live->position.z, -3.75f);
    EXPECT_EQ(live->scale.x, 2.0f);
    EXPECT_EQ(live->rotation.w, 1.0f);
    EXPECT_EQ(live->rotation.x, 0.0f);
}

TEST(ComponentRoundTrip, HealthRestore)
{
    World world;
    Entity e = world.createEntity();
    HealthComponent health{};
    health.health.restore(40.0f, 1.5f);
    world.emplace<HealthComponent>(e, health);
    stampProceduralId(world, e, "unit/hp");
    Save::SaveSystem save;
    const std::string json = capture(save, world);
    auto* live = world.get<HealthComponent>(e);
    live->health.restore(100.0f, 0.0f);
    ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    EXPECT_EQ(live->health.hp(), 40.0f);
    EXPECT_EQ(live->health.timeSinceDamage(), 1.5f);
}
