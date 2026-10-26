#include <gtest/gtest.h>

#include "SaveTestWorld.h"

#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Combat/HitVolumes.h"
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

TEST(ComponentRoundTrip, MotorClimbAndStaminaRoundTrip)
{
    World world;
    Entity e = world.createEntity();
    world.emplace<PlayerMotorComponent>(e);
    stampProceduralId(world, e, "unit/motor");
    Save::SaveSystem save;
    auto* live = world.get<PlayerMotorComponent>(e);
    ASSERT_NE(live, nullptr);

    struct ClimbCase
    {
        PlayerMoveState state;
        float           stamina;
        float           ledgeY;
        Math::Vector3f  wallDir;
        const char*     name;
    };
    const ClimbCase cases[] = {
        { PlayerMoveState::Grounded, 0.2f, 0.0f, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, "Grounded" },
        { PlayerMoveState::Hang, 0.35f, 4.5f, Math::Vector3f{ 0.0f, 0.0f, 1.0f }, "Hang" },
        { PlayerMoveState::WallRun, 0.55f, 2.25f, Math::Vector3f{ 1.0f, 0.0f, 0.0f }, "WallRun" },
    };

    for (const ClimbCase& item : cases)
    {
        live->motor.restoreProgress(item.state, Math::Vector3f{ 1.5f, -2.0f, 0.25f }, 0.4f, 0.05f, true, false, 0.0f, Math::Vector3f{ 0.0f, 0.0f, 0.0f });
        live->motor.restoreClimb(item.stamina, item.ledgeY, item.wallDir);
        const std::string json = capture(save, world);
        nlohmann::ordered_json root;
        ASSERT_TRUE(parseSave(json, root));
        nlohmann::ordered_json* comp = nullptr;
        ASSERT_TRUE(componentJson(root, "PlayerMotor", comp));
        EXPECT_EQ((*comp)["state"], item.name);
        EXPECT_EQ((*comp)["v"], 2);
        EXPECT_TRUE(comp->contains("stamina"));
        EXPECT_TRUE(comp->contains("ledgeY"));
        EXPECT_TRUE(comp->contains("wallDir"));

        live->motor.restoreProgress(PlayerMoveState::Grounded, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f, false, false, 0.0f, Math::Vector3f{ 0.0f, 0.0f, 0.0f });
        live->motor.restoreClimb(1.0f, 0.0f, Math::Vector3f{ 0.0f, 0.0f, 0.0f });
        ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
        EXPECT_EQ(live->motor.state(), item.state);
        EXPECT_FLOAT_EQ(live->motor.stamina(), item.stamina);
        EXPECT_FLOAT_EQ(live->motor.ledgeY(), item.ledgeY);
        EXPECT_FLOAT_EQ(live->motor.wallDir().x, item.wallDir.x);
        EXPECT_FLOAT_EQ(live->motor.wallDir().y, item.wallDir.y);
        EXPECT_FLOAT_EQ(live->motor.wallDir().z, item.wallDir.z);
        EXPECT_FLOAT_EQ(live->motor.velocity().x, 1.5f);
        EXPECT_FLOAT_EQ(live->motor.velocity().y, -2.0f);
    }
}

TEST(ComponentRoundTrip, MotorClimbWithoutWallBecomesFalling)
{
    World world;
    Entity e = world.createEntity();
    world.emplace<PlayerMotorComponent>(e);
    stampProceduralId(world, e, "unit/motor-wall");
    auto* live = world.get<PlayerMotorComponent>(e);
    ASSERT_NE(live, nullptr);
    live->motor.restoreProgress(PlayerMoveState::WallRun, Math::Vector3f{ 0.0f, 4.5f, 0.0f }, 0.2f, 0.0f, false, false, 0.0f, Math::Vector3f{ 0.0f, 0.0f, 0.0f });
    live->motor.restoreClimb(0.4f, 3.0f, Math::Vector3f{ 0.0f, 1.0f, 0.0f });
    Save::SaveSystem save;
    const std::string json = capture(save, world);
    live->motor.restoreProgress(PlayerMoveState::Grounded, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f, false, false, 0.0f, Math::Vector3f{ 0.0f, 0.0f, 0.0f });
    live->motor.restoreClimb(1.0f, 9.0f, Math::Vector3f{ 1.0f, 0.0f, 0.0f });
    ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    EXPECT_EQ(live->motor.state(), PlayerMoveState::Falling);
    EXPECT_FLOAT_EQ(live->motor.stamina(), 0.4f);
    EXPECT_FLOAT_EQ(live->motor.ledgeY(), 3.0f);
    EXPECT_FLOAT_EQ(live->motor.wallDir().x, 0.0f);
    EXPECT_FLOAT_EQ(live->motor.wallDir().y, 1.0f);
    EXPECT_FLOAT_EQ(live->motor.wallDir().z, 0.0f);
}

TEST(ComponentRoundTrip, HitVolumesTimersAndWeights)
{
    World world;
    Entity e = world.createEntity();
    Combat::HitVolumesComponent volumes{};
    const int arm = Combat::hitPartIndex(Combat::HitPart::RightArm);
    volumes.disableTimer[arm] = 1.5f;
    volumes.impactWeight[arm] = 0.4f;
    world.emplace<Combat::HitVolumesComponent>(e, volumes);
    stampProceduralId(world, e, "unit/hit-volumes");
    Save::SaveSystem save;
    const std::string json = capture(save, world);

    auto* live = world.get<Combat::HitVolumesComponent>(e);
    ASSERT_NE(live, nullptr);
    for (int i = 0; i < Combat::HitVolumesComponent::kSlotCount; ++i)
    {
        live->disableTimer[i] = 0.0f;
        live->impactWeight[i] = 0.0f;
    }
    ASSERT_EQ(save.applyPayloadText(world, json), Save::SaveResult::Ok);
    EXPECT_FLOAT_EQ(live->disableTimer[arm], 1.5f);
    EXPECT_FLOAT_EQ(live->impactWeight[arm], 0.4f);
    for (int i = 0; i < Combat::HitVolumesComponent::kSlotCount; ++i)
    {
        if (i == arm)
            continue;
        EXPECT_FLOAT_EQ(live->disableTimer[i], 0.0f);
        EXPECT_FLOAT_EQ(live->impactWeight[i], 0.0f);
    }

    for (int i = 0; i < Combat::HitVolumesComponent::kSlotCount; ++i)
    {
        live->disableTimer[i] = 0.0f;
        live->impactWeight[i] = 0.0f;
    }
    const std::string zeros = capture(save, world);
    live->disableTimer[arm] = 2.0f;
    live->impactWeight[arm] = 0.8f;
    ASSERT_EQ(save.applyPayloadText(world, zeros), Save::SaveResult::Ok);
    for (int i = 0; i < Combat::HitVolumesComponent::kSlotCount; ++i)
    {
        EXPECT_FLOAT_EQ(live->disableTimer[i], 0.0f);
        EXPECT_FLOAT_EQ(live->impactWeight[i], 0.0f);
    }
}
