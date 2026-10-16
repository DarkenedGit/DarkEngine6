#include <gtest/gtest.h>

#include "ECS/World.h"
#include "Save/PlayClock.h"

using namespace Dark;

TEST(PlayClock, SessionClockCopiesCloudTime)
{
    WorldClockComponent clock;
    clock.playTimeSec = 9.0;
    Save::SessionClockState state;
    state.cloudTime    = 3.5f;
    state.envTimeOfDay = 0.25f;
    state.waterTime    = 1.5f;
    Save::writeSessionClock(clock, state);
    EXPECT_FLOAT_EQ(clock.cloudTime, 3.5f);
    EXPECT_FLOAT_EQ(clock.envTimeOfDay, 0.25f);
    EXPECT_FLOAT_EQ(clock.waterTime, 1.5f);
    EXPECT_FLOAT_EQ(clock.simTimeSec, 3.5f);
    EXPECT_DOUBLE_EQ(clock.playTimeSec, 9.0);

    clock.cloudTime = 8.0f;
    Save::readSessionClock(clock, state);
    EXPECT_FLOAT_EQ(state.cloudTime, 8.0f);
    EXPECT_FLOAT_EQ(state.envTimeOfDay, 0.25f);
    EXPECT_FLOAT_EQ(state.waterTime, 1.5f);
    EXPECT_FLOAT_EQ(clock.simTimeSec, 3.5f);
}

TEST(PlayClock, SessionEntityAndBodyLook)
{
    World world;
    const Entity first = Save::ensureSessionEntity(world, Entity{});
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(world.alive(first));
    EXPECT_EQ(Save::ensureSessionEntity(world, first), first);
    ASSERT_NE(world.get<TagComponent>(first), nullptr);
    EXPECT_EQ(world.get<TagComponent>(first)->name, "Session");
    ASSERT_NE(world.get<WorldClockComponent>(first), nullptr);
    ASSERT_NE(world.get<AiClockComponent>(first), nullptr);
    ASSERT_NE(world.get<PersistentIdComponent>(first), nullptr);
    EXPECT_EQ(world.get<PersistentIdComponent>(first)->origin, PersistOrigin::Procedural);

    world.destroyEntity(first);
    const Entity again = Save::ensureSessionEntity(world, first);
    EXPECT_NE(again, first);
    EXPECT_TRUE(world.alive(again));

    Entity body = world.createEntity();
    world.emplace<LookComponent>(body);
    world.emplace<AnimGraphComponent>(body);
    world.emplace<PlayerMotorComponent>(body);
    world.get<PlayerMotorComponent>(body)->motor.restoreProgress(PlayerMoveState::Crouch, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f, false, false, 0.0f, Math::Vector3f{ 0.0f, 0.0f, 0.0f });

    Save::BodyLookState captured;
    captured.yaw          = 1.2f;
    captured.pitch        = -0.4f;
    captured.lightOn      = true;
    captured.lowerBodyYaw = 0.7f;
    Save::captureBodyLook(world, body, captured);
    EXPECT_FLOAT_EQ(world.get<LookComponent>(body)->yaw, 1.2f);
    EXPECT_FLOAT_EQ(world.get<LookComponent>(body)->pitch, -0.4f);
    EXPECT_TRUE(world.get<LookComponent>(body)->lightOn);
    EXPECT_FLOAT_EQ(world.get<AnimGraphComponent>(body)->graph.player().lowerBodyYaw(), 0.7f);

    Save::BodyLookState restored;
    restored.yaw = 9.0f;
    Save::restoreBodyLook(world, body, restored);
    EXPECT_FLOAT_EQ(restored.yaw, 1.2f);
    EXPECT_FLOAT_EQ(restored.pitch, -0.4f);
    EXPECT_TRUE(restored.lightOn);
    EXPECT_FLOAT_EQ(restored.lowerBodyYaw, 0.7f);
    EXPECT_TRUE(restored.crouch);

    Save::BodyLookState seeded;
    seeded.yaw = 4.0f;
    Save::restoreBodyLook(world, Entity{}, seeded);
    EXPECT_FLOAT_EQ(seeded.yaw, 4.0f);
    EXPECT_FALSE(seeded.crouch);

    Entity bare = world.createEntity();
    Save::BodyLookState kept;
    kept.yaw          = 2.0f;
    kept.lowerBodyYaw = 0.3f;
    Save::captureBodyLook(world, bare, kept);
    Save::restoreBodyLook(world, bare, kept);
    EXPECT_FLOAT_EQ(kept.yaw, 2.0f);
    EXPECT_FLOAT_EQ(kept.lowerBodyYaw, 0.3f);
    EXPECT_FALSE(kept.crouch);
}
