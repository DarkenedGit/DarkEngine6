#include <gtest/gtest.h>

#include "Character/PlayerMotor.h"

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
} // namespace

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
