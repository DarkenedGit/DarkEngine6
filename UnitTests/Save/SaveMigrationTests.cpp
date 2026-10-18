#include <gtest/gtest.h>

#include "SaveTestWorld.h"

#include "Character/PlayerMotorComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"

using namespace Dark;

namespace
{
    int g_begin = 0;

    void beginLoad(void*, World&) { ++g_begin; }

    std::string captured(World& world, Save::SaveSystem& save)
    {
        Save::SaveHost host = testHost();
        host.beginLoad = beginLoad;
        armSave(save, host);
        std::string json;
        EXPECT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
        return json;
    }
}

TEST(SaveMigration, NewerSchemaDoesNotTouchTheWorld)
{
    g_begin = 0;
    World world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = { 1.0f, 0.0f, 0.0f };
    world.emplace<TransformComponent>(e, xf);
    stampProceduralId(world, e, "unit/schema");
    Save::SaveSystem save;
    std::string json = captured(world, save);
    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(json, root));
    root["schema"] = 2;
    world.get<TransformComponent>(e)->position.x = 9.0f;
    EXPECT_EQ(save.applyPayloadText(world, root.dump(2)), Save::SaveResult::SchemaTooNew);
    EXPECT_EQ(g_begin, 0);
    EXPECT_EQ(world.get<TransformComponent>(e)->position.x, 9.0f);
}

TEST(SaveMigration, NewerComponentVersionIsSkipped)
{
    World world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = { 2.0f, 0.0f, 0.0f };
    world.emplace<TransformComponent>(e, xf);
    stampProceduralId(world, e, "unit/compver");
    Save::SaveSystem save;
    armSave(save, testHost());
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(json, root));
    nlohmann::ordered_json* comp = nullptr;
    ASSERT_TRUE(componentJson(root, "Transform", comp));
    (*comp)["v"] = 99;
    rehashPayload(root);
    world.get<TransformComponent>(e)->position.x = 8.0f;
    EXPECT_EQ(save.applyPayloadText(world, root.dump(2)), Save::SaveResult::Ok);
    EXPECT_EQ(world.get<TransformComponent>(e)->position.x, 8.0f);
}

TEST(SaveMigration, OlderComponentVersionStillApplies)
{
    World world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = { 3.0f, 0.0f, 0.0f };
    world.emplace<TransformComponent>(e, xf);
    stampProceduralId(world, e, "unit/oldver");
    Save::SaveSystem save;
    armSave(save, testHost());
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(json, root));
    nlohmann::ordered_json* comp = nullptr;
    ASSERT_TRUE(componentJson(root, "Transform", comp));
    (*comp)["v"] = 0;
    rehashPayload(root);
    world.get<TransformComponent>(e)->position.x = 8.0f;
    EXPECT_EQ(save.applyPayloadText(world, root.dump(2)), Save::SaveResult::Ok);
    EXPECT_EQ(world.get<TransformComponent>(e)->position.x, 3.0f);
}

TEST(SaveMigration, MotorVersion1LeavesClimbFields)
{
    World world;
    Entity e = world.createEntity();
    world.emplace<PlayerMotorComponent>(e);
    stampProceduralId(world, e, "unit/motor-v1");
    auto* live = world.get<PlayerMotorComponent>(e);
    ASSERT_NE(live, nullptr);
    live->motor.restoreProgress(PlayerMoveState::Hang, Math::Vector3f{ 0.0f, 4.0f, 0.0f }, 1.0f, 0.0f, false, false, 0.0f, Math::Vector3f{ 0.0f, 0.0f, 0.0f });
    live->motor.restoreClimb(0.2f, 6.25f, Math::Vector3f{ 0.0f, 0.0f, -1.0f });
    Save::SaveSystem save;
    armSave(save, testHost());
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(json, root));
    nlohmann::ordered_json* comp = nullptr;
    ASSERT_TRUE(componentJson(root, "PlayerMotor", comp));
    (*comp)["v"]     = 1;
    (*comp)["state"] = "Grounded";
    comp->erase("stamina");
    comp->erase("ledgeY");
    comp->erase("wallDir");
    rehashPayload(root);
    EXPECT_EQ(save.applyPayloadText(world, root.dump(2)), Save::SaveResult::Ok);
    EXPECT_EQ(live->motor.state(), PlayerMoveState::Grounded);
    EXPECT_FLOAT_EQ(live->motor.stamina(), 0.2f);
    EXPECT_FLOAT_EQ(live->motor.ledgeY(), 6.25f);
    EXPECT_FLOAT_EQ(live->motor.wallDir().x, 0.0f);
    EXPECT_FLOAT_EQ(live->motor.wallDir().z, -1.0f);
}
