#include <gtest/gtest.h>

#include "AI/AiSystem.h"
#include "AI/Exposure.h"
#include "Character/PlayerMotorComponent.h"
#include "Character/PlayerStealth.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/AABox3f.h"
#include "Math/Vector3f.h"
#include "Save/SaveJson.h"

#include "third_party/nlohmann/json.hpp"

using namespace Dark;
using namespace Dark::Math;

namespace
{

    struct Stage
    {
        World    world;
        AiSystem ai;
        Entity   player{};

        Stage()
        {
            player = world.createEntity();
            TransformComponent xf{};
            world.emplace<TransformComponent>(player, xf);
            world.emplace<PlayerMotorComponent>(player);
        }

        PlayerMotor& motor() { return world.get<PlayerMotorComponent>(player)->motor; }

        PlayerStealthComponent& addStealth() { return world.emplace<PlayerStealthComponent>(player); }
    };

    bool tick(Stage& stage, float dt, bool sprint, bool camouflage, float sunElev)
    {
        return AI::tickPlayerExposure(stage.world, stage.player, stage.ai, dt, sprint, camouflage, sunElev, Vector3f{ 0.0f, 1.0f, 0.0f }, nullptr, 0, {});
    }

    void zeroProgress(PlayerMotor& motor, PlayerMoveState state, float vx)
    {
        motor.restoreProgress(state, Vector3f{ vx, 0.0f, 0.0f }, 0.0f, 0.0f, false, false, 0.0f, Vector3f{ 0.0f, 0.0f, 0.0f });
    }

} // namespace

TEST(Exposure, OpenSunIsFullAndACubeDarkensIt)
{
    World world;
    const Vector3f chest{ 0.0f, 0.45f, 0.0f };
    const Vector3f up{ 0.0f, 1.0f, 0.0f };
    EXPECT_NEAR(AI::sampleLightLevel(world, chest, 1.0f, up, nullptr, 0, {}), 1.0f, 1.0e-4f);
    EXPECT_NEAR(AI::sampleLightLevel(world, chest, -1.0f, up, nullptr, 0, {}), 0.12f, 1.0e-4f);

    const AABox3f cube{ Vector3f{ 1.0f, -1.0f, -1.0f }, Vector3f{ 3.0f, 2.0f, 1.0f } };
    const float blocked = AI::sampleLightLevel(world, chest, 1.0f, Vector3f{ 1.0f, 0.0f, 0.0f }, &cube, 1, {});
    EXPECT_NEAR(blocked, 0.12f + 0.88f * 0.3f, 1.0e-4f);
}

TEST(Exposure, PointLampRaisesLevelAndTheMuzzleIsIgnored)
{
    World world;
    const Vector3f chest{ 0.0f, 0.45f, 0.0f };
    Entity lamp = world.createEntity();
    TransformComponent xf{};
    xf.position = chest;
    world.emplace<TransformComponent>(lamp, xf);
    LocalLightComponent light{};
    light.type    = LocalLightType::Point;
    light.range   = 8.0f;
    light.enabled = true;
    world.emplace<LocalLightComponent>(lamp, light);

    const float lit = AI::sampleLightLevel(world, chest, -1.0f, Vector3f{ 0.0f, 1.0f, 0.0f }, nullptr, 0, {});
    EXPECT_GT(lit, 0.12f);
    EXPECT_NEAR(lit, 1.0f, 1.0e-4f);

    const float ignored = AI::sampleLightLevel(world, chest, -1.0f, Vector3f{ 0.0f, 1.0f, 0.0f }, nullptr, 0, lamp);
    EXPECT_NEAR(ignored, 0.12f, 1.0e-4f);
}

TEST(Exposure, MissingComponentIsLeftAlone)
{
    Stage stage;
    EXPECT_FALSE(tick(stage, 0.05f, false, false, -1.0f));
    EXPECT_FALSE(stage.world.has<PlayerStealthComponent>(stage.player));
}

TEST(Exposure, StanceScalesExposureAndCamouflage)
{
    Stage still;
    still.addStealth();
    zeroProgress(still.motor(), PlayerMoveState::Crouch, 0.0f);
    ASSERT_TRUE(tick(still, 0.0f, false, false, -1.0f));
    EXPECT_NEAR(still.world.get<PlayerStealthComponent>(still.player)->exposure, 0.2064f, 1.0e-4f);
    EXPECT_NEAR(still.ai.preySense().hearRange, 1.25f, 1.0e-4f);

    Stage stand;
    stand.addStealth();
    ASSERT_TRUE(tick(stand, 0.0f, false, false, -1.0f));
    EXPECT_NEAR(stand.world.get<PlayerStealthComponent>(stand.player)->exposure, 0.4128f, 1.0e-4f);
    EXPECT_NEAR(stand.ai.preySense().hearRange, 4.0f, 1.0e-4f);

    Stage hidden;
    hidden.addStealth();
    ASSERT_TRUE(tick(hidden, 0.0f, false, true, -1.0f));
    EXPECT_NEAR(hidden.world.get<PlayerStealthComponent>(hidden.player)->exposure, 0.12384f, 1.0e-4f);
}

TEST(Exposure, SprintStridePostsNoiseOnce)
{
    Stage stage;
    PlayerStealthComponent& stealth = stage.addStealth();
    stealth.strideLeft = 0.0f;
    stage.motor().setHorizontalVelocity(3.0f, 0.0f);
    ASSERT_TRUE(tick(stage, 0.05f, true, false, -1.0f));
    EXPECT_EQ(stage.ai.noiseCount(), 1);
    EXPECT_NEAR(stealth.strideLeft, 2.2f, 1.0e-4f);
    EXPECT_NEAR(stage.ai.preySense().hearRange, 24.0f, 1.0e-4f);

    ASSERT_TRUE(tick(stage, 0.01f, true, false, -1.0f));
    EXPECT_EQ(stage.ai.noiseCount(), 1);
    EXPECT_GT(stealth.strideLeft, 0.0f);
}

TEST(Exposure, AirborneMotionDoesNotStride)
{
    Stage stage;
    PlayerStealthComponent& stealth = stage.addStealth();
    zeroProgress(stage.motor(), PlayerMoveState::Falling, 3.0f);
    ASSERT_TRUE(tick(stage, 0.05f, true, false, -1.0f));
    EXPECT_EQ(stage.ai.noiseCount(), 0);
    EXPECT_NEAR(stealth.strideLeft, 0.0f, 1.0e-4f);
}

TEST(Exposure, StealthRecordRoundTrips)
{
    PlayerStealthComponent comp;
    comp.settings.hearStand = 0.0f;
    comp.settings.hearWalk  = 6.0f;
    comp.lightLevel         = 0.25f;
    comp.exposure           = 0.4f;
    comp.strideLeft         = -0.5f;

    nlohmann::ordered_json obj = nlohmann::ordered_json::object();
    Save::SaveWriter       out(obj, nullptr);
    PlayerStealthComponent::kPersist.capture(&comp, &out);
    ASSERT_TRUE(out.ok());

    PlayerStealthComponent back;
    Save::SaveReader       in(obj, nullptr);
    PlayerStealthComponent::kPersist.apply(&back, &in, PlayerStealthComponent::kSaveVersion);
    EXPECT_FALSE(in.failed());
    EXPECT_NEAR(back.settings.hearStand, 0.0f, 1.0e-4f);
    EXPECT_NEAR(back.settings.hearWalk, 6.0f, 1.0e-4f);
    EXPECT_NEAR(back.settings.hearSprint, 24.0f, 1.0e-4f);
    EXPECT_NEAR(back.lightLevel, 0.25f, 1.0e-4f);
    EXPECT_NEAR(back.exposure, 0.4f, 1.0e-4f);
    EXPECT_NEAR(back.strideLeft, -0.5f, 1.0e-4f);

    obj["hearWalk"] = 5000.0;
    PlayerStealthComponent kept;
    kept.settings.hearWalk = 3.0f;
    Save::SaveReader bad(obj, nullptr);
    PlayerStealthComponent::kPersist.apply(&kept, &bad, PlayerStealthComponent::kSaveVersion);
    EXPECT_TRUE(bad.failed());
    EXPECT_NEAR(kept.settings.hearWalk, 3.0f, 1.0e-4f);
}
