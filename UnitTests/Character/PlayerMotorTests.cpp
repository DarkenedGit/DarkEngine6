#include <gtest/gtest.h>

#include "Character/PlayerMotor.h"
#include "Character/PlayerStealth.h"
#include "Character/SkillLimits.h"

#include <cmath>
#include <limits>

using namespace Dark;
using namespace Dark::Math;

namespace
{
    float flatGround(void*, float, float)
    {
        return 0.0f;
    }

    float cliffGround(void*, float x, float)
    {
        return x >= 1.0f ? -4.0f : 0.0f;
    }

    PlayerGroundQuery flatQuery(float waterY = -100.0f)
    {
        PlayerGroundQuery q;
        q.heightAt = flatGround;
        q.waterY   = waterY;
        return q;
    }

    void advance(PlayerMotor& motor, Vector3f& pos, const PlayerMotorInput& in, float seconds, const PlayerGroundQuery& ground, float dt = 1.0f / 60.0f)
    {
        float left = seconds;
        while (left > 1.0e-6f)
        {
            const float stepDt = left < dt ? left : dt;
            motor.tick(pos, in, stepDt, ground);
            left -= stepDt;
        }
    }

    void fallUntilNearGround(PlayerMotor& motor, Vector3f& pos, const PlayerGroundQuery& ground)
    {
        PlayerMotorInput hold{};
        for (int i = 0; i < 180; ++i)
        {
            if (motor.state() == PlayerMoveState::Falling && pos.y < 0.90f)
                return;
            motor.tick(pos, hold, 1.0f / 60.0f, ground);
        }
    }
} // namespace

TEST(PlayerMotor, CrouchWalkIsSlowerThanWalk)
{
    PlayerMotor motor;
    Vector3f    walking{ 0.0f, 0.5f, 0.0f };
    Vector3f    crouching = walking;
    PlayerMotorInput walk{};
    walk.wish = Vector3f{ 0.0f, 0.0f, 1.0f };
    PlayerMotorInput low = walk;
    low.crouch = true;
    advance(motor, walking, walk, 0.5f, flatQuery());
    PlayerMotor crouched;
    advance(crouched, crouching, low, 0.5f, flatQuery());
    EXPECT_EQ(crouched.state(), PlayerMoveState::Crouch);
    EXPECT_LT(crouching.z, walking.z);
    EXPECT_NEAR(crouched.velocity().z, motor.settings().crouchSpeed, 0.15f);
    EXPECT_LT(motor.settings().crouchSpeed, motor.settings().walkSpeed);

    low.crouch = false;
    crouched.tick(crouching, low, 1.0f / 60.0f, flatQuery());
    EXPECT_EQ(crouched.state(), PlayerMoveState::Grounded);
}

TEST(PlayerStealth, StillCrouchIsQuieterThanCrouchWalk)
{
    PlayerStealthSettings s{};
    const PreySense standing = preySenseFor(s, false, true, false);
    const PreySense sprinting = preySenseFor(s, false, true, true);
    const PreySense sneaking = preySenseFor(s, true, true, false);
    const PreySense hiding = preySenseFor(s, true, false, false);

    EXPECT_GT(standing.hearRange, sneaking.hearRange);
    EXPECT_GT(sneaking.hearRange, hiding.hearRange);
    EXPECT_GT(sprinting.hearRange, standing.hearRange);
    EXPECT_LT(sneaking.sightRangeScale, 1.0f);
    EXPECT_LT(hiding.sightRangeScale, sneaking.sightRangeScale);
    EXPECT_LT(hiding.standoff, sneaking.standoff);
    EXPECT_LT(sneaking.standoff, standing.standoff);
    EXPECT_LT(hiding.targetHeight, standing.targetHeight);
}

TEST(PlayerMotor, JumpLeavesGroundAndReportsEvent)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput in{};
    in.jumpPressed = true;
    const PlayerMotorResult r = motor.tick(pos, in, 1.0f / 60.0f, flatQuery());
    EXPECT_TRUE(r.jumped);
    EXPECT_FALSE(r.doubleJumped);
    EXPECT_EQ(motor.state(), PlayerMoveState::Jumping);
    EXPECT_GT(pos.y, 0.5f);
    EXPECT_GT(motor.velocity().y, 0.0f);
}

TEST(PlayerMotor, DoubleJumpInWindowBoostsHigher)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput press{};
    press.jumpPressed = true;
    motor.tick(pos, press, 1.0f / 60.0f, flatQuery());

    PlayerMotorInput hold{};
    advance(motor, pos, hold, 0.12f, flatQuery());
    ASSERT_EQ(motor.state(), PlayerMoveState::Jumping);
    ASSERT_FALSE(motor.didDoubleJump());

    const PlayerMotorResult r = motor.tick(pos, press, 1.0f / 60.0f, flatQuery());
    EXPECT_TRUE(r.jumped);
    EXPECT_TRUE(r.doubleJumped);
    EXPECT_TRUE(motor.didDoubleJump());
    EXPECT_NEAR(motor.velocity().y, motor.settings().doubleJumpSpeed, 0.5f);
    EXPECT_EQ(motor.state(), PlayerMoveState::Jumping);
}

TEST(PlayerMotor, DoubleJumpAfterWindowDoesNothing)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput press{};
    press.jumpPressed = true;
    motor.tick(pos, press, 1.0f / 60.0f, flatQuery());

    PlayerMotorInput hold{};
    advance(motor, pos, hold, motor.settings().doubleJumpWindow + 0.05f, flatQuery());

    const float vyBefore = motor.velocity().y;
    const PlayerMotorResult r = motor.tick(pos, press, 1.0f / 60.0f, flatQuery());
    EXPECT_FALSE(r.doubleJumped);
    EXPECT_FALSE(motor.didDoubleJump());
    EXPECT_NEAR(motor.velocity().y, vyBefore - motor.settings().gravity / 60.0f, 0.05f);
}

TEST(PlayerMotor, EarlySecondStrokeWaitsForMinDelay)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput press{};
    press.jumpPressed = true;
    motor.tick(pos, press, 1.0f / 60.0f, flatQuery());

    PlayerMotorInput hold{};
    PlayerMotorResult r = motor.tick(pos, press, 1.0f / 60.0f, flatQuery());
    EXPECT_FALSE(r.doubleJumped);
    EXPECT_LT(motor.airTime(), motor.settings().doubleJumpMinDelay);

    const float need = motor.settings().doubleJumpMinDelay + 0.02f;
    while (motor.airTime() < need && !motor.didDoubleJump())
        r = motor.tick(pos, hold, 1.0f / 60.0f, flatQuery());
    EXPECT_TRUE(motor.didDoubleJump());
    EXPECT_TRUE(r.doubleJumped);
}

TEST(PlayerMotor, SinglePressDoesNotDoubleJump)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput press{};
    press.jumpPressed = true;
    motor.tick(pos, press, 1.0f / 60.0f, flatQuery());

    PlayerMotorInput hold{};
    advance(motor, pos, hold, 0.20f, flatQuery());
    EXPECT_FALSE(motor.didDoubleJump());
    EXPECT_TRUE(motor.didFirstJump());
}

TEST(PlayerMotor, LandsBackOnGround)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput press{};
    press.jumpPressed = true;
    motor.tick(pos, press, 1.0f / 60.0f, flatQuery());

    PlayerMotorInput hold{};
    bool landed = false;
    for (int i = 0; i < 180; ++i)
    {
        const PlayerMotorResult r = motor.tick(pos, hold, 1.0f / 60.0f, flatQuery());
        if (r.landed)
        {
            landed = true;
            EXPECT_FALSE(r.splashed);
            break;
        }
    }
    EXPECT_TRUE(landed);
    EXPECT_EQ(motor.state(), PlayerMoveState::Grounded);
    EXPECT_NEAR(pos.y, 0.5f, 1.0e-4f);
}

TEST(PlayerMotor, NoJumpWhileSwimming)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.35f, 0.0f };
    PlayerMotorInput press{};
    press.jumpPressed = true;
    const PlayerGroundQuery water = flatQuery(2.0f);
    motor.tick(pos, press, 1.0f / 60.0f, water);
    EXPECT_EQ(motor.state(), PlayerMoveState::Swimming);
    EXPECT_FALSE(motor.didFirstJump());
    EXPECT_NEAR(motor.velocity().y, 0.0f, 1.0e-4f);
}

TEST(PlayerMotor, SpeedScaleHalfWalksAtHalfWalkSpeed)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput in{};
    in.wish       = Vector3f{ 0.0f, 0.0f, 1.0f };
    in.speedScale = 0.5f;
    constexpr float kDt = 0.25f;
    motor.tick(pos, in, kDt, flatQuery());
    EXPECT_NEAR(pos.z, motor.settings().walkSpeed * 0.5f * kDt, 1.0e-4f);
    EXPECT_NEAR(motor.velocity().z, motor.settings().walkSpeed * 0.5f, 1.0e-4f);
    EXPECT_NEAR(motor.settings().walkSpeed * 0.5f, 4.0f, 1.0e-4f);
}

TEST(PlayerMotor, SpeedScaleClampsToZeroOne)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput in{};
    in.wish       = Vector3f{ 0.0f, 0.0f, 1.0f };
    in.speedScale = 2.0f;
    constexpr float kDt = 0.25f;
    motor.tick(pos, in, kDt, flatQuery());
    EXPECT_NEAR(pos.z, motor.settings().walkSpeed * kDt, 1.0e-4f);

    motor.reset();
    pos           = Vector3f{ 0.0f, 0.5f, 0.0f };
    in.speedScale = -1.0f;
    motor.tick(pos, in, kDt, flatQuery());
    EXPECT_NEAR(pos.z, 0.0f, 1.0e-4f);
    EXPECT_NEAR(motor.velocity().z, 0.0f, 1.0e-4f);
}

TEST(PlayerMotor, SpeedScaleHalfSwimsAtHalfSwimSpeed)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.35f, 0.0f };
    const PlayerGroundQuery water = flatQuery(2.0f);
    PlayerMotorInput hold{};
    motor.tick(pos, hold, 1.0f / 60.0f, water);
    ASSERT_EQ(motor.state(), PlayerMoveState::Swimming);

    PlayerMotorInput in{};
    in.wish       = Vector3f{ 0.0f, 0.0f, 1.0f };
    in.speedScale = 0.5f;
    constexpr float kDt = 0.25f;
    const float     z0  = pos.z;
    motor.tick(pos, in, kDt, water);
    EXPECT_NEAR(pos.z - z0, motor.settings().swimSpeed * 0.5f * kDt, 1.0e-4f);
}

TEST(PlayerMotor, AirControlIsWeakerThanGround)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    const PlayerGroundQuery q = flatQuery();
    constexpr float kDt = 0.25f;

    PlayerMotorInput run{};
    run.wish = Vector3f{ 0.0f, 0.0f, 1.0f };
    motor.tick(pos, run, kDt, q);
    const float groundZ = pos.z;
    EXPECT_NEAR(groundZ, motor.settings().walkSpeed * kDt, 1.0e-4f);

    motor.reset();
    pos = Vector3f{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput jump{};
    jump.jumpPressed = true;
    motor.tick(pos, jump, 1.0f / 60.0f, q);

    Vector3f          airPos = pos;
    PlayerMotorInput  steer{};
    steer.wish = Vector3f{ 1.0f, 0.0f, 0.0f };
    motor.tick(airPos, steer, kDt, q);
    EXPECT_LT(airPos.x, groundZ * 0.5f);
    EXPECT_EQ(motor.state(), PlayerMoveState::Jumping);
}

TEST(PlayerMotor, WalkOffLedgeEntersFalling)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerGroundQuery q;
    q.heightAt = cliffGround;
    q.waterY   = -100.0f;

    PlayerMotorInput run{};
    run.wish = Vector3f{ 1.0f, 0.0f, 0.0f };
    for (int i = 0; i < 20; ++i)
        motor.tick(pos, run, 1.0f / 60.0f, q);

    EXPECT_EQ(motor.state(), PlayerMoveState::Falling);
    EXPECT_LT(pos.y, 0.5f);
}

TEST(PlayerMotor, WalkIntoWaterSetsSplashed)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 2.5f, 0.0f };
    PlayerGroundQuery q;
    q.heightAt = [](void*, float x, float) -> float { return x >= 1.0f ? 0.0f : 2.0f; };
    q.waterY   = 1.5f;

    PlayerMotorInput run{};
    run.wish = Vector3f{ 1.0f, 0.0f, 0.0f };
    bool splashed = false;
    for (int i = 0; i < 30; ++i)
    {
        const PlayerMotorResult r = motor.tick(pos, run, 1.0f / 60.0f, q);
        if (r.splashed)
        {
            splashed = true;
            EXPECT_FALSE(r.landed);
            break;
        }
    }
    EXPECT_TRUE(splashed);
    EXPECT_EQ(motor.state(), PlayerMoveState::Swimming);
}

TEST(PlayerMotor, JumpIntoWaterSetsSplashedNotLanded)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput press{};
    press.jumpPressed = true;
    motor.tick(pos, press, 1.0f / 60.0f, flatQuery());
    ASSERT_EQ(motor.state(), PlayerMoveState::Jumping);

    PlayerMotorInput hold{};
    bool splashed = false;
    bool landed   = false;
    const PlayerGroundQuery water = flatQuery(10.0f);
    for (int i = 0; i < 180; ++i)
    {
        const PlayerMotorResult r = motor.tick(pos, hold, 1.0f / 60.0f, water);
        if (r.splashed)
            splashed = true;
        if (r.landed)
            landed = true;
        if (motor.state() == PlayerMoveState::Swimming)
            break;
    }
    EXPECT_TRUE(splashed);
    EXPECT_FALSE(landed);
    EXPECT_EQ(motor.state(), PlayerMoveState::Swimming);
}

TEST(PlayerMotor, FallingUsesAirSpeedNotWalkSpeed)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerGroundQuery q;
    q.heightAt = cliffGround;
    q.waterY   = -100.0f;

    PlayerMotorInput run{};
    run.wish = Vector3f{ 1.0f, 0.0f, 0.0f };
    for (int i = 0; i < 20; ++i)
        motor.tick(pos, run, 1.0f / 60.0f, q);
    ASSERT_EQ(motor.state(), PlayerMoveState::Falling);

    const float x0 = pos.x;
    PlayerMotorInput strafe{};
    strafe.wish = Vector3f{ 0.0f, 0.0f, 1.0f };
    motor.tick(pos, strafe, 0.25f, q);
    EXPECT_LT(pos.z, motor.settings().walkSpeed * 0.25f * 0.5f);
    EXPECT_GT(pos.x, x0);
}

TEST(PlayerMotor, AirControlScaleZeroDoesNotSteer)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput jump{};
    jump.jumpPressed = true;
    motor.tick(pos, jump, 1.0f / 60.0f, flatQuery());
    ASSERT_EQ(motor.state(), PlayerMoveState::Jumping);

    const float x0 = pos.x;
    const float vx0 = motor.velocity().x;
    PlayerMotorInput steer{};
    steer.wish            = Vector3f{ 1.0f, 0.0f, 0.0f };
    steer.airControlScale = 0.0f;
    motor.tick(pos, steer, 0.25f, flatQuery());
    EXPECT_NEAR(pos.x, x0, 1.0e-4f);
    EXPECT_NEAR(motor.velocity().x, vx0, 1.0e-4f);
}

TEST(PlayerMotor, AirControlScaleNegativeClampsToZero)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput jump{};
    jump.jumpPressed = true;
    motor.tick(pos, jump, 1.0f / 60.0f, flatQuery());

    const float x0 = pos.x;
    PlayerMotorInput steer{};
    steer.wish            = Vector3f{ 1.0f, 0.0f, 0.0f };
    steer.airControlScale = -4.0f;
    motor.tick(pos, steer, 0.25f, flatQuery());
    EXPECT_NEAR(pos.x, x0, 1.0e-4f);
    EXPECT_NEAR(motor.velocity().x, 0.0f, 1.0e-4f);
}

TEST(PlayerMotor, AllowDoubleJumpFalseBlocksSecondPress)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput press{};
    press.jumpPressed = true;
    motor.tick(pos, press, 1.0f / 60.0f, flatQuery());

    PlayerMotorInput hold{};
    hold.allowDoubleJump = false;
    advance(motor, pos, hold, 0.12f, flatQuery());
    ASSERT_EQ(motor.state(), PlayerMoveState::Jumping);
    ASSERT_FALSE(motor.didDoubleJump());

    PlayerMotorInput second{};
    second.jumpPressed     = true;
    second.allowDoubleJump = false;
    const PlayerMotorResult r = motor.tick(pos, second, 1.0f / 60.0f, flatQuery());
    EXPECT_FALSE(r.jumped);
    EXPECT_FALSE(r.doubleJumped);
    EXPECT_FALSE(motor.didDoubleJump());
}

TEST(PlayerMotor, AllowDoubleJumpFalseIgnoresEarlyPendingPress)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput press{};
    press.jumpPressed = true;
    motor.tick(pos, press, 1.0f / 60.0f, flatQuery());

    PlayerMotorInput blocked{};
    blocked.jumpPressed     = true;
    blocked.allowDoubleJump = false;
    PlayerMotorResult r     = motor.tick(pos, blocked, 1.0f / 60.0f, flatQuery());
    EXPECT_FALSE(r.doubleJumped);
    EXPECT_LT(motor.airTime(), motor.settings().doubleJumpMinDelay);

    PlayerMotorInput hold{};
    hold.allowDoubleJump = false;
    const float need = motor.settings().doubleJumpMinDelay + 0.02f;
    while (motor.airTime() < need && !motor.didDoubleJump())
        r = motor.tick(pos, hold, 1.0f / 60.0f, flatQuery());
    EXPECT_FALSE(motor.didDoubleJump());
    EXPECT_FALSE(r.doubleJumped);
}

TEST(PlayerMotor, AllowDoubleJumpDefaultStillDoubleJumps)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput press{};
    press.jumpPressed = true;
    EXPECT_TRUE(press.allowDoubleJump);
    EXPECT_NEAR(press.airControlScale, 1.0f, 1.0e-6f);
    motor.tick(pos, press, 1.0f / 60.0f, flatQuery());

    PlayerMotorInput hold{};
    advance(motor, pos, hold, 0.12f, flatQuery());
    const PlayerMotorResult r = motor.tick(pos, press, 1.0f / 60.0f, flatQuery());
    EXPECT_TRUE(r.doubleJumped);
    EXPECT_TRUE(motor.didDoubleJump());
}

TEST(PlayerMotor, LandJumpBufferReJumpsWhenAllowed)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    const PlayerGroundQuery q = flatQuery();
    PlayerMotorInput press{};
    press.jumpPressed = true;
    motor.tick(pos, press, 1.0f / 60.0f, q);

    fallUntilNearGround(motor, pos, q);
    ASSERT_EQ(motor.state(), PlayerMoveState::Falling);
    ASSERT_GT(motor.airTime(), motor.settings().doubleJumpWindow);
    ASSERT_FALSE(motor.didDoubleJump());

    PlayerMotorInput late{};
    late.jumpPressed = true;
    motor.tick(pos, late, 1.0f / 60.0f, q);
    EXPECT_FALSE(motor.didDoubleJump());

    PlayerMotorInput hold{};
    bool landedRejump = false;
    for (int i = 0; i < 30; ++i)
    {
        const PlayerMotorResult r = motor.tick(pos, hold, 1.0f / 60.0f, q);
        if (r.landed)
        {
            landedRejump = r.jumped;
            break;
        }
    }
    EXPECT_TRUE(landedRejump);
    EXPECT_EQ(motor.state(), PlayerMoveState::Jumping);
}

TEST(PlayerMotor, AllowJumpBufferFalseSkipsLandReJump)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    const PlayerGroundQuery q = flatQuery();
    PlayerMotorInput press{};
    press.jumpPressed = true;
    motor.tick(pos, press, 1.0f / 60.0f, q);

    fallUntilNearGround(motor, pos, q);
    ASSERT_GT(motor.airTime(), motor.settings().doubleJumpWindow);

    PlayerMotorInput late{};
    late.jumpPressed = true;
    motor.tick(pos, late, 1.0f / 60.0f, q);

    PlayerMotorInput blocked{};
    blocked.allowJumpBuffer = false;
    bool landed = false;
    for (int i = 0; i < 30; ++i)
    {
        const PlayerMotorResult r = motor.tick(pos, blocked, 1.0f / 60.0f, q);
        if (r.landed)
        {
            landed = true;
            EXPECT_FALSE(r.jumped);
            break;
        }
        EXPECT_FALSE(r.jumped);
    }
    EXPECT_TRUE(landed);
    EXPECT_EQ(motor.state(), PlayerMoveState::Grounded);
}

TEST(PlayerMotor, ClearJumpBufferSkipsLandReJump)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    const PlayerGroundQuery q = flatQuery();
    PlayerMotorInput press{};
    press.jumpPressed = true;
    motor.tick(pos, press, 1.0f / 60.0f, q);

    fallUntilNearGround(motor, pos, q);
    ASSERT_GT(motor.airTime(), motor.settings().doubleJumpWindow);

    PlayerMotorInput late{};
    late.jumpPressed = true;
    motor.tick(pos, late, 1.0f / 60.0f, q);
    motor.clearJumpBuffer();

    PlayerMotorInput hold{};
    bool landed = false;
    for (int i = 0; i < 30; ++i)
    {
        const PlayerMotorResult r = motor.tick(pos, hold, 1.0f / 60.0f, q);
        if (r.landed)
        {
            landed = true;
            EXPECT_FALSE(r.jumped);
            break;
        }
    }
    EXPECT_TRUE(landed);
    EXPECT_EQ(motor.state(), PlayerMoveState::Grounded);
}

PlayerMotorInput dodgeTap(MoveCardinal dir)
{
    PlayerMotorInput in{};
    in.dodgeTap = dir;
    in.dodgeTapWish = moveCardinalWish(dir, Vector3f{ 0.0f, 0.0f, 1.0f }, Vector3f{ 1.0f, 0.0f, 0.0f });
    in.allowDodge = true;
    return in;
}

TEST(PlayerMotor, DoubleTapDodgeBurstsThenEnds)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    const PlayerGroundQuery q = flatQuery();

    const PlayerMotorResult first = motor.tick(pos, dodgeTap(MoveCardinal::Forward), 1.0f / 60.0f, q);
    EXPECT_FALSE(first.dodged);
    EXPECT_EQ(motor.state(), PlayerMoveState::Grounded);

    PlayerMotorInput gap{};
    advance(motor, pos, gap, 0.10f, q);
    const PlayerMotorResult second = motor.tick(pos, dodgeTap(MoveCardinal::Forward), 1.0f / 60.0f, q);
    EXPECT_TRUE(second.dodged);
    EXPECT_EQ(motor.state(), PlayerMoveState::Dodge);
    EXPECT_NEAR(motor.velocity().z, motor.settings().dodgeSpeed, 1.0e-3f);

    const float zAtStart = pos.z;
    advance(motor, pos, gap, 0.20f, q);
    EXPECT_EQ(motor.state(), PlayerMoveState::Dodge);
    EXPECT_GT(pos.z, zAtStart + motor.settings().walkSpeed * 0.15f);

    advance(motor, pos, gap, 0.40f, q);
    EXPECT_EQ(motor.state(), PlayerMoveState::Grounded);
    EXPECT_NEAR(motor.velocity().z, 0.0f, 1.0e-3f);
}

TEST(PlayerMotor, DodgeIgnoresLateOrDifferentTap)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    const PlayerGroundQuery q = flatQuery();
    motor.tick(pos, dodgeTap(MoveCardinal::Left), 1.0f / 60.0f, q);
    PlayerMotorInput gap{};
    advance(motor, pos, gap, 0.20f, q);
    const PlayerMotorResult late = motor.tick(pos, dodgeTap(MoveCardinal::Left), 1.0f / 60.0f, q);
    EXPECT_FALSE(late.dodged);
    EXPECT_EQ(motor.state(), PlayerMoveState::Grounded);

    motor.tick(pos, dodgeTap(MoveCardinal::Forward), 1.0f / 60.0f, q);
    advance(motor, pos, gap, 0.08f, q);
    const PlayerMotorResult other = motor.tick(pos, dodgeTap(MoveCardinal::Right), 1.0f / 60.0f, q);
    EXPECT_FALSE(other.dodged);
    EXPECT_EQ(motor.state(), PlayerMoveState::Grounded);
}

TEST(PlayerMotor, DodgeBlockedWhileAirborne)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    const PlayerGroundQuery q = flatQuery();
    PlayerMotorInput jump{};
    jump.jumpPressed = true;
    motor.tick(pos, jump, 1.0f / 60.0f, q);
    ASSERT_EQ(motor.state(), PlayerMoveState::Jumping);

    motor.tick(pos, dodgeTap(MoveCardinal::Forward), 1.0f / 60.0f, q);
    PlayerMotorInput gap{};
    advance(motor, pos, gap, 0.05f, q);
    const PlayerMotorResult second = motor.tick(pos, dodgeTap(MoveCardinal::Forward), 1.0f / 60.0f, q);
    EXPECT_FALSE(second.dodged);
    EXPECT_NE(motor.state(), PlayerMoveState::Dodge);
}

TEST(PlayerMotor, DodgeDisplacementIgnoresRunScale)
{
    const PlayerGroundQuery q = flatQuery();
    constexpr float kStep = 1.0f / 60.0f;
    constexpr int   kSteps = 12;

    auto dodgeDelta = [&](float runScale) -> float
    {
        PlayerMotor motor;
        Vector3f    pos{ 0.0f, 0.5f, 0.0f };
        PlayerMotorInput first = dodgeTap(MoveCardinal::Forward);
        first.runScale = runScale;
        motor.tick(pos, first, kStep, q);

        PlayerMotorInput gap{};
        gap.runScale = runScale;
        advance(motor, pos, gap, 0.10f, q);

        PlayerMotorInput second = dodgeTap(MoveCardinal::Forward);
        second.runScale = runScale;
        const float z0 = pos.z;
        const PlayerMotorResult entered = motor.tick(pos, second, kStep, q);
        EXPECT_TRUE(entered.dodged);
        for (int i = 1; i < kSteps; ++i)
            motor.tick(pos, gap, kStep, q);
        EXPECT_EQ(motor.state(), PlayerMoveState::Dodge);
        EXPECT_NEAR(motor.velocity().z, motor.settings().dodgeSpeed, 1.0e-3f);
        EXPECT_FLOAT_EQ(motor.settings().dodgeSpeed, 24.0f);
        return pos.z - z0;
    };

    const float elapsed = kStep * static_cast<float>(kSteps);
    const float plain   = dodgeDelta(kSkillIdentity);
    const float scaled  = dodgeDelta(kRunScaleMax);
    EXPECT_NEAR(scaled, plain, 1.0e-4f);
    EXPECT_NEAR(scaled, 24.0f * elapsed, 1.0e-3f);
}

TEST(PlayerMotor, SprintDisplacementUsesRunScale)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput in{};
    in.wish     = Vector3f{ 0.0f, 0.0f, 1.0f };
    in.sprint   = true;
    in.runScale = kRunScaleMax;
    constexpr float kDt = 0.25f;
    motor.tick(pos, in, kDt, flatQuery());
    ASSERT_FLOAT_EQ(motor.settings().sprintSpeed, 16.0f);
    EXPECT_NEAR(pos.z, motor.settings().sprintSpeed * kRunScaleMax * kDt, 1.0e-4f);
    EXPECT_NEAR(motor.velocity().z, motor.settings().sprintSpeed * kRunScaleMax, 1.0e-4f);
    EXPECT_FLOAT_EQ(motor.settings().sprintSpeed, 16.0f);
}

TEST(PlayerMotor, WalkDisplacementUsesRunScale)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput in{};
    in.wish     = Vector3f{ 0.0f, 0.0f, 1.0f };
    in.runScale = kRunScaleMax;
    constexpr float kDt = 0.25f;
    motor.tick(pos, in, kDt, flatQuery());
    ASSERT_FLOAT_EQ(motor.settings().walkSpeed, 8.0f);
    EXPECT_EQ(motor.state(), PlayerMoveState::Grounded);
    EXPECT_NEAR(pos.z, motor.settings().walkSpeed * kRunScaleMax * kDt, 1.0e-4f);
    EXPECT_NEAR(motor.velocity().z, motor.settings().walkSpeed * kRunScaleMax, 1.0e-4f);
}

TEST(PlayerMotor, CrouchDisplacementIgnoresRunScale)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput in{};
    in.wish     = Vector3f{ 0.0f, 0.0f, 1.0f };
    in.crouch   = true;
    in.sprint   = true;
    in.runScale = kRunScaleMax;
    constexpr float kDt = 0.25f;
    motor.tick(pos, in, kDt, flatQuery());
    EXPECT_EQ(motor.state(), PlayerMoveState::Crouch);
    ASSERT_FLOAT_EQ(motor.settings().crouchSpeed, 3.4f);
    EXPECT_NEAR(pos.z, motor.settings().crouchSpeed * kDt, 1.0e-4f);
    EXPECT_NEAR(motor.velocity().z, motor.settings().crouchSpeed, 1.0e-4f);
}

TEST(PlayerMotor, SwimScaleChangesSwimNotSprint)
{
    constexpr float kDt = 0.25f;
    const PlayerGroundQuery water = flatQuery(2.0f);

    PlayerMotor swimmer;
    Vector3f    swimPos{ 0.0f, 0.35f, 0.0f };
    PlayerMotorInput boot{};
    swimmer.tick(swimPos, boot, 1.0f / 60.0f, water);
    ASSERT_EQ(swimmer.state(), PlayerMoveState::Swimming);

    PlayerMotorInput swim{};
    swim.wish      = Vector3f{ 0.0f, 0.0f, 1.0f };
    swim.sprint    = true;
    swim.swimScale = kSwimScaleMax;
    swim.runScale  = kRunScaleMax;
    const float z0 = swimPos.z;
    swimmer.tick(swimPos, swim, kDt, water);
    ASSERT_FLOAT_EQ(swimmer.settings().swimSpeed, 5.0f);
    EXPECT_NEAR(swimPos.z - z0, swimmer.settings().swimSpeed * kSwimScaleMax * kDt, 1.0e-4f);
    EXPECT_FLOAT_EQ(swimmer.settings().swimSpeed, 5.0f);

    PlayerMotor sprinter;
    Vector3f    sprintPos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput sprint{};
    sprint.wish      = Vector3f{ 0.0f, 0.0f, 1.0f };
    sprint.sprint    = true;
    sprint.swimScale = kSwimScaleMax;
    sprinter.tick(sprintPos, sprint, kDt, flatQuery());
    ASSERT_FLOAT_EQ(sprinter.settings().sprintSpeed, 16.0f);
    EXPECT_NEAR(sprintPos.z, sprinter.settings().sprintSpeed * kDt, 1.0e-4f);
    EXPECT_EQ(sprinter.state(), PlayerMoveState::Grounded);
}

TEST(PlayerMotor, JumpScaleSetsTakeoffVelocity)
{
    PlayerMotor motor;
    ASSERT_FLOAT_EQ(motor.settings().jumpSpeed, 12.0f);
    ASSERT_FLOAT_EQ(motor.settings().doubleJumpSpeed, 16.0f);

    Vector3f pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput press{};
    press.jumpPressed = true;
    press.jumpScale   = kJumpScaleMax;
    // dt 0 leaves the takeoff in velocity().y. A positive dt subtracts gravity after beginJump.
    const PlayerMotorResult first = motor.tick(pos, press, 0.0f, flatQuery());
    EXPECT_TRUE(first.jumped);
    EXPECT_FALSE(first.doubleJumped);
    EXPECT_FLOAT_EQ(motor.velocity().y, motor.settings().jumpSpeed * kJumpScaleMax);
    EXPECT_NEAR(motor.velocity().y, 13.8f, 1.0e-4f);

    PlayerMotorInput hold{};
    hold.jumpScale = kJumpScaleMax;
    advance(motor, pos, hold, 0.12f, flatQuery());
    ASSERT_EQ(motor.state(), PlayerMoveState::Jumping);
    ASSERT_FALSE(motor.didDoubleJump());

    const PlayerMotorResult second = motor.tick(pos, press, 1.0f / 60.0f, flatQuery());
    EXPECT_TRUE(second.doubleJumped);
    EXPECT_FLOAT_EQ(motor.velocity().y, motor.settings().doubleJumpSpeed * kJumpScaleMax);
    EXPECT_NEAR(motor.velocity().y, 18.4f, 1.0e-4f);
    EXPECT_FLOAT_EQ(motor.settings().jumpSpeed, 12.0f);
    EXPECT_FLOAT_EQ(motor.settings().doubleJumpSpeed, 16.0f);
    EXPECT_FLOAT_EQ(motor.settings().gravity, 24.0f);
    EXPECT_FLOAT_EQ(motor.settings().coyoteTime, 0.10f);
    EXPECT_FLOAT_EQ(motor.settings().jumpBuffer, 0.12f);
    EXPECT_FLOAT_EQ(motor.settings().doubleJumpWindow, 0.28f);

    PlayerMotor integrated;
    Vector3f    integratedPos{ 0.0f, 0.5f, 0.0f };
    constexpr float kDt = 1.0f / 60.0f;
    integrated.tick(integratedPos, press, kDt, flatQuery());
    EXPECT_NEAR(integrated.velocity().y, integrated.settings().jumpSpeed * kJumpScaleMax - integrated.settings().gravity * kDt, 1.0e-4f);
}

TEST(PlayerMotor, SpeedScaleZeroStopsHorizontalAtAnyRunScale)
{
    constexpr float kDt = 0.25f;
    const float runScales[] = { kRunScaleMax, 4.0f, std::numeric_limits<float>::quiet_NaN() };
    for (float runScale : runScales)
    {
        PlayerMotor motor;
        Vector3f    pos{ 0.0f, 0.5f, 0.0f };
        PlayerMotorInput in{};
        in.wish       = Vector3f{ 0.0f, 0.0f, 1.0f };
        in.sprint     = true;
        in.speedScale = 0.0f;
        in.runScale   = runScale;
        motor.tick(pos, in, kDt, flatQuery());
        EXPECT_NEAR(pos.z, 0.0f, 1.0e-4f);
        EXPECT_NEAR(motor.velocity().z, 0.0f, 1.0e-4f);
        EXPECT_NEAR(motor.velocity().x, 0.0f, 1.0e-4f);
    }

    PlayerMotor walker;
    Vector3f    walkPos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput walk{};
    walk.wish       = Vector3f{ 0.0f, 0.0f, 1.0f };
    walk.speedScale = 0.0f;
    walk.runScale   = kRunScaleMax;
    walker.tick(walkPos, walk, kDt, flatQuery());
    EXPECT_NEAR(walkPos.z, 0.0f, 1.0e-4f);
    EXPECT_NEAR(walker.velocity().z, 0.0f, 1.0e-4f);
}

TEST(PlayerMotor, AirControlIgnoresRunScale)
{
    const PlayerGroundQuery q = flatQuery();
    constexpr float kDt = 0.25f;

    PlayerMotor plain;
    PlayerMotor scaled;
    Vector3f    plainPos{ 0.0f, 0.5f, 0.0f };
    Vector3f    scaledPos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput jump{};
    jump.jumpPressed = true;
    plain.tick(plainPos, jump, 1.0f / 60.0f, q);
    scaled.tick(scaledPos, jump, 1.0f / 60.0f, q);
    ASSERT_EQ(plain.state(), PlayerMoveState::Jumping);
    ASSERT_EQ(scaled.state(), PlayerMoveState::Jumping);

    PlayerMotorInput steer{};
    steer.wish = Vector3f{ 1.0f, 0.0f, 0.0f };
    PlayerMotorInput boosted = steer;
    boosted.runScale = kRunScaleMax;
    plain.tick(plainPos, steer, kDt, q);
    scaled.tick(scaledPos, boosted, kDt, q);

    EXPECT_GT(plainPos.x, 0.0f);
    EXPECT_NEAR(plain.velocity().x, plain.settings().airSpeed, 1.0e-3f);
    EXPECT_NEAR(scaledPos.x, plainPos.x, 1.0e-4f);
    EXPECT_NEAR(scaled.velocity().x, plain.velocity().x, 1.0e-4f);
}

TEST(PlayerMotor, SkillScalesClampAndNonFiniteBecomeIdentity)
{
    constexpr float kDt = 0.25f;
    PlayerMotorInput sprint{};
    sprint.wish   = Vector3f{ 0.0f, 0.0f, 1.0f };
    sprint.sprint = true;

    PlayerMotor high;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    sprint.runScale = 4.0f;
    high.tick(pos, sprint, kDt, flatQuery());
    EXPECT_NEAR(pos.z, high.settings().sprintSpeed * kRunScaleMax * kDt, 1.0e-4f);

    PlayerMotor low;
    pos = Vector3f{ 0.0f, 0.5f, 0.0f };
    sprint.runScale = 0.25f;
    low.tick(pos, sprint, kDt, flatQuery());
    EXPECT_NEAR(pos.z, low.settings().sprintSpeed * kSkillIdentity * kDt, 1.0e-4f);

    PlayerMotor nanRun;
    pos = Vector3f{ 0.0f, 0.5f, 0.0f };
    sprint.runScale = std::numeric_limits<float>::quiet_NaN();
    nanRun.tick(pos, sprint, kDt, flatQuery());
    EXPECT_TRUE(std::isfinite(nanRun.velocity().z));
    EXPECT_NEAR(pos.z, nanRun.settings().sprintSpeed * kSkillIdentity * kDt, 1.0e-4f);

    PlayerMotor infRun;
    pos = Vector3f{ 0.0f, 0.5f, 0.0f };
    sprint.runScale = std::numeric_limits<float>::infinity();
    infRun.tick(pos, sprint, kDt, flatQuery());
    EXPECT_NEAR(pos.z, infRun.settings().sprintSpeed * kSkillIdentity * kDt, 1.0e-4f);

    const PlayerGroundQuery water = flatQuery(2.0f);
    PlayerMotor swimmer;
    Vector3f    swimPos{ 0.0f, 0.35f, 0.0f };
    PlayerMotorInput boot{};
    swimmer.tick(swimPos, boot, 1.0f / 60.0f, water);
    ASSERT_EQ(swimmer.state(), PlayerMoveState::Swimming);

    PlayerMotorInput swim{};
    swim.wish      = Vector3f{ 0.0f, 0.0f, 1.0f };
    swim.swimScale = std::numeric_limits<float>::quiet_NaN();
    float z0 = swimPos.z;
    swimmer.tick(swimPos, swim, kDt, water);
    EXPECT_NEAR(swimPos.z - z0, swimmer.settings().swimSpeed * kSkillIdentity * kDt, 1.0e-4f);

    swim.swimScale = 8.0f;
    z0 = swimPos.z;
    swimmer.tick(swimPos, swim, kDt, water);
    EXPECT_NEAR(swimPos.z - z0, swimmer.settings().swimSpeed * kSwimScaleMax * kDt, 1.0e-4f);

    swim.swimScale = 0.1f;
    z0 = swimPos.z;
    swimmer.tick(swimPos, swim, kDt, water);
    EXPECT_NEAR(swimPos.z - z0, swimmer.settings().swimSpeed * kSwimScaleMin * kDt, 1.0e-4f);

    PlayerMotor nanJump;
    Vector3f    jumpPos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput press{};
    press.jumpPressed = true;
    press.jumpScale   = std::numeric_limits<float>::quiet_NaN();
    nanJump.tick(jumpPos, press, 0.0f, flatQuery());
    EXPECT_FLOAT_EQ(nanJump.velocity().y, nanJump.settings().jumpSpeed * kSkillIdentity);

    PlayerMotor capped;
    Vector3f    capPos{ 0.0f, 0.5f, 0.0f };
    press.jumpScale = 3.0f;
    capped.tick(capPos, press, 0.0f, flatQuery());
    EXPECT_FLOAT_EQ(capped.velocity().y, capped.settings().jumpSpeed * kJumpScaleMax);

    PlayerMotor weak;
    Vector3f    weakPos{ 0.0f, 0.5f, 0.0f };
    press.jumpScale = 0.2f;
    weak.tick(weakPos, press, 0.0f, flatQuery());
    EXPECT_FLOAT_EQ(weak.velocity().y, weak.settings().jumpSpeed * kJumpScaleMin);
}
