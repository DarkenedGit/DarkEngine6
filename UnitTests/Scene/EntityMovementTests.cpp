#include <gtest/gtest.h>

#include <cmath>

#include "AI/AiComponents.h"
#include "Character/PlayerMotorComponent.h"
#include "Combat/JumpAttackComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/CharacterAttach.h"
#include "Scene/EntityMovement.h"

using namespace Dark;

TEST(EntityMovement, ParseKeepsSeedAndRejectsBadJson)
{
    EntityMovement move;
    move.type      = "keep";
    move.walkSpeed = 4.0f;
    EXPECT_FALSE(parseEntityMovement("{", move));
    EXPECT_EQ(move.type, "keep");
    EXPECT_FLOAT_EQ(move.walkSpeed, 4.0f);
    EXPECT_FALSE(parseEntityMovement("{\"version\":1}", move));
    EXPECT_EQ(move.type, "keep");
    EXPECT_FALSE(parseEntityMovement("{\"version\":2,\"type\":\"player\"}", move));
    EXPECT_EQ(move.type, "keep");
    EXPECT_FALSE(loadEntityMovementType("no-such-movement", move));
    EXPECT_FLOAT_EQ(move.walkSpeed, 4.0f);
    EXPECT_EQ(entityMovementVirtualPath("player"), "entities/player.entity.movement.json");
}

TEST(EntityMovement, JumpHeightReplacesSpeed)
{
    EntityMovement move;
    move.gravity   = 24.0f;
    move.jumpSpeed = 1.0f;
    move.walkSpeed = 2.0f;
    const char* text = R"({
        "version": 1,
        "type": "player",
        "gravity": 24,
        "jumpHeight": 3,
        "walkSpeed": -4
    })";
    ASSERT_TRUE(parseEntityMovement(text, move));
    EXPECT_FLOAT_EQ(move.jumpSpeed, 12.0f);
    EXPECT_FLOAT_EQ(move.jumpHeight, 3.0f);
    EXPECT_FLOAT_EQ(move.walkSpeed, 2.0f);
    EXPECT_FLOAT_EQ(move.gravity, 24.0f);
}

TEST(EntityMovement, SpeedFillsHeightAndHeightWinsWhenBothAreSet)
{
    EntityMovement fromSpeed;
    fromSpeed.gravity = 24.0f;
    const char* speedOnly = R"({
        "version": 1,
        "type": "hunter",
        "jumpSpeed": 12
    })";
    ASSERT_TRUE(parseEntityMovement(speedOnly, fromSpeed));
    EXPECT_FLOAT_EQ(fromSpeed.jumpSpeed, 12.0f);
    EXPECT_FLOAT_EQ(fromSpeed.jumpHeight, 3.0f);

    EntityMovement both;
    both.gravity         = 24.0f;
    both.doubleJumpSpeed = 1.0f;
    const char* withBoth = R"({
        "version": 1,
        "type": "player",
        "jumpSpeed": 99,
        "jumpHeight": 3,
        "doubleJumpHeight": 3
    })";
    ASSERT_TRUE(parseEntityMovement(withBoth, both));
    EXPECT_FLOAT_EQ(both.jumpSpeed, 12.0f);
    EXPECT_FLOAT_EQ(both.jumpHeight, 3.0f);
    EXPECT_FLOAT_EQ(both.doubleJumpSpeed, 12.0f);
    EXPECT_FLOAT_EQ(both.doubleJumpHeight, 3.0f);
}

TEST(EntityMovement, AccelerationOmittedKeepsSeedAndNegativeIsIgnored)
{
    EntityMovement move;
    move.acceleration = 4.0f;
    ASSERT_TRUE(parseEntityMovement(R"({"version":1,"type":"player","walkSpeed":2})", move));
    EXPECT_FLOAT_EQ(move.acceleration, 4.0f);
    EXPECT_FLOAT_EQ(move.walkSpeed, 2.0f);

    ASSERT_TRUE(parseEntityMovement(R"({"version":1,"type":"player","acceleration":-3})", move));
    EXPECT_FLOAT_EQ(move.acceleration, 4.0f);

    ASSERT_TRUE(parseEntityMovement(R"({"version":1,"type":"wolf","acceleration":9})", move));
    EXPECT_FLOAT_EQ(move.acceleration, 9.0f);
}

TEST(EntityMovement, ContentFilesMatchCurrentFeel)
{
    EntityMovement player;
    player.walkSpeed       = 1.0f;
    player.runSpeed        = 1.0f;
    player.acceleration    = 1.0f;
    player.jumpSpeed       = 1.0f;
    player.doubleJumpSpeed = 1.0f;
    player.gravity         = 1.0f;
    ASSERT_TRUE(loadEntityMovementType("player", player));
    EXPECT_EQ(player.type, "player");
    EXPECT_FLOAT_EQ(player.walkSpeed, 2.5f);
    EXPECT_FLOAT_EQ(player.runSpeed, 7.5f);
    EXPECT_FLOAT_EQ(player.acceleration, 8.0f);
    EXPECT_FLOAT_EQ(player.jumpHeight, 3.0f);
    EXPECT_FLOAT_EQ(player.doubleJumpSpeed, 12.0f);
    EXPECT_FLOAT_EQ(player.gravity, 9.8f);
    EXPECT_NEAR(player.jumpSpeed, std::sqrt(2.0f * player.gravity * player.jumpHeight), 1.0e-5f);
    EXPECT_NEAR(player.doubleJumpHeight, (12.0f * 12.0f) / (2.0f * player.gravity), 1.0e-4f);

    EntityMovement hunter;
    hunter.walkSpeed           = 1.0f;
    hunter.runSpeed            = 1.0f;
    hunter.acceleration        = 1.0f;
    hunter.jumpSpeed           = 1.0f;
    hunter.leapForwardSpeed    = 1.0f;
    hunter.leapForwardSpeedMax = 1.0f;
    hunter.gravity             = 1.0f;
    ASSERT_TRUE(loadEntityMovementType("hunter", hunter));
    EXPECT_FLOAT_EQ(hunter.walkSpeed, 2.5f);
    EXPECT_FLOAT_EQ(hunter.runSpeed, 6.0f);
    EXPECT_FLOAT_EQ(hunter.acceleration, 8.0f);
    EXPECT_FLOAT_EQ(hunter.jumpSpeed, 5.0f);
    EXPECT_FLOAT_EQ(hunter.leapForwardSpeed, 7.5f);
    EXPECT_FLOAT_EQ(hunter.leapForwardSpeedMax, 10.0f);
    EXPECT_FLOAT_EQ(hunter.gravity, 9.8f);
    EXPECT_NEAR(hunter.jumpHeight, (5.0f * 5.0f) / (2.0f * hunter.gravity), 1.0e-4f);

    EntityMovement wolf;
    wolf.walkSpeed           = 1.0f;
    wolf.runSpeed            = 1.0f;
    wolf.acceleration        = 1.0f;
    wolf.jumpSpeed           = 1.0f;
    wolf.leapForwardSpeed    = 1.0f;
    wolf.leapForwardSpeedMax = 1.0f;
    wolf.gravity             = 1.0f;
    ASSERT_TRUE(loadEntityMovementType("wolf", wolf));
    EXPECT_FLOAT_EQ(wolf.walkSpeed, 2.0f);
    EXPECT_FLOAT_EQ(wolf.runSpeed, 6.0f);
    EXPECT_FLOAT_EQ(wolf.acceleration, 8.0f);
    EXPECT_FLOAT_EQ(wolf.jumpSpeed, 5.0f);
    EXPECT_FLOAT_EQ(wolf.leapForwardSpeed, 10.0f);
    EXPECT_FLOAT_EQ(wolf.leapForwardSpeedMax, 12.0f);
    EXPECT_FLOAT_EQ(wolf.gravity, 9.8f);
    EXPECT_NEAR(wolf.jumpHeight, (5.0f * 5.0f) / (2.0f * wolf.gravity), 1.0e-4f);
}

TEST(EntityMovement, ApplyUsesContentFiles)
{
    World world;

    Entity player = world.createEntity();
    auto&  motor  = world.emplace<PlayerMotorComponent>(player).motor;
    motor.settings().walkSpeed       = 1.0f;
    motor.settings().sprintSpeed     = 1.0f;
    motor.settings().jumpSpeed       = 1.0f;
    motor.settings().doubleJumpSpeed = 1.0f;
    motor.settings().crouchSpeed     = 1.1f;
    motor.setHorizontalVelocity(2.0f, 3.0f);

    Entity hunter = world.createEntity();
    world.emplace<TagComponent>(hunter, "hunter");
    world.emplace<AiAgentComponent>(hunter);
    auto& hjac = world.emplace<JumpAttackComponent>(hunter);
    Combat::JumpAttackDef hdef = hjac.jump.def();
    hdef.leapVerticalSpeed     = 1.0f;
    hdef.leapForwardSpeed      = 1.0f;
    hdef.leapForwardSpeedMax   = 1.0f;
    hdef.gravity               = 24.0f;
    hdef.telegraphSeconds      = 0.40f;
    hjac.jump.setDef(hdef);

    Entity wolf = world.createEntity();
    world.emplace<TagComponent>(wolf, "Wolf");
    world.emplace<AiAgentComponent>(wolf);
    world.emplace<JumpAttackComponent>(wolf);

    applyAuthoredMovement(world);

    EXPECT_FLOAT_EQ(motor.settings().walkSpeed, 2.5f);
    EXPECT_FLOAT_EQ(motor.settings().sprintSpeed, 7.5f);
    EXPECT_FLOAT_EQ(motor.settings().acceleration, 8.0f);
    EXPECT_NEAR(motor.settings().jumpSpeed, std::sqrt(2.0f * 9.8f * 3.0f), 1.0e-5f);
    EXPECT_FLOAT_EQ(motor.settings().doubleJumpSpeed, 12.0f);
    EXPECT_FLOAT_EQ(motor.settings().crouchSpeed, 1.1f);
    EXPECT_FLOAT_EQ(motor.settings().gravity, 9.8f);
    EXPECT_FLOAT_EQ(motor.velocity().x, 2.0f);
    EXPECT_FLOAT_EQ(motor.velocity().z, 3.0f);

    const AiAgentComponent* hai = world.get<AiAgentComponent>(hunter);
    ASSERT_NE(hai, nullptr);
    EXPECT_TRUE(hai->authoredMove);
    EXPECT_FLOAT_EQ(hai->walkSpeed, 2.5f);
    EXPECT_FLOAT_EQ(hai->runSpeed, 6.0f);
    EXPECT_FLOAT_EQ(hai->acceleration, 8.0f);
    const Combat::JumpAttackDef& applied = world.get<JumpAttackComponent>(hunter)->jump.def();
    EXPECT_FLOAT_EQ(applied.leapVerticalSpeed, 5.0f);
    EXPECT_FLOAT_EQ(applied.leapForwardSpeed, 7.5f);
    EXPECT_FLOAT_EQ(applied.leapForwardSpeedMax, 10.0f);
    EXPECT_FLOAT_EQ(applied.gravity, 9.8f);
    EXPECT_FLOAT_EQ(applied.telegraphSeconds, 0.40f);

    const AiAgentComponent* wai = world.get<AiAgentComponent>(wolf);
    ASSERT_NE(wai, nullptr);
    EXPECT_TRUE(wai->authoredMove);
    EXPECT_FLOAT_EQ(wai->walkSpeed, 2.0f);
    EXPECT_FLOAT_EQ(wai->runSpeed, 6.0f);
    EXPECT_FLOAT_EQ(wai->acceleration, 8.0f);
    EXPECT_FLOAT_EQ(world.get<JumpAttackComponent>(wolf)->jump.def().leapVerticalSpeed, 5.0f);
    EXPECT_FLOAT_EQ(world.get<JumpAttackComponent>(wolf)->jump.def().leapForwardSpeed, 10.0f);
    EXPECT_FLOAT_EQ(world.get<JumpAttackComponent>(wolf)->jump.def().leapForwardSpeedMax, 12.0f);
}
