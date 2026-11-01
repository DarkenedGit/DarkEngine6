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

    // Horizontal stand-in for the 0.3 m wall capsule. A start overlap is not a hit,
    // matching b3World_CastMover ignoring fraction 0.
    struct WallSolid
    {
        float minX = 0.0f;
        float maxX = 0.0f;
        float minZ = 0.0f;
        float maxZ = 0.0f;
        float topY = 100.0f;
        bool  roof = false;
    };

    struct WallWorld
    {
        WallSolid solids[3]{};
        int       count = 0;
    };

    constexpr float kWallProbeRadius = 0.3f;

    float pointBoxDist2(float px, float pz, const WallSolid& solid)
    {
        const float cx = px < solid.minX ? solid.minX : (px > solid.maxX ? solid.maxX : px);
        const float cz = pz < solid.minZ ? solid.minZ : (pz > solid.maxZ ? solid.maxZ : pz);
        const float dx = px - cx;
        const float dz = pz - cz;
        return dx * dx + dz * dz;
    }

    bool sweepCircle(float ox, float oz, float dx, float dz, float maxDist, const WallSolid& solid, float& outDist)
    {
        const float radius = kWallProbeRadius;
        if (pointBoxDist2(ox, oz, solid) <= radius * radius)
            return false;

        float best = maxDist + 1.0f;
        auto  take = [&](float t) {
            if (t >= 0.0f && t <= maxDist && t < best)
                best = t;
        };

        if (std::fabs(dx) > 1.0e-8f)
        {
            const float planes[2] = { solid.minX - radius, solid.maxX + radius };
            for (float plane : planes)
            {
                const float t = (plane - ox) / dx;
                const float z = oz + dz * t;
                if (z >= solid.minZ && z <= solid.maxZ)
                    take(t);
            }
        }
        if (std::fabs(dz) > 1.0e-8f)
        {
            const float planes[2] = { solid.minZ - radius, solid.maxZ + radius };
            for (float plane : planes)
            {
                const float t = (plane - oz) / dz;
                const float x = ox + dx * t;
                if (x >= solid.minX && x <= solid.maxX)
                    take(t);
            }
        }

        const float a = dx * dx + dz * dz;
        if (a > 1.0e-12f)
        {
            const float cx[4] = { solid.minX, solid.maxX, solid.minX, solid.maxX };
            const float cz[4] = { solid.minZ, solid.minZ, solid.maxZ, solid.maxZ };
            for (int i = 0; i < 4; ++i)
            {
                const float fx   = ox - cx[i];
                const float fz   = oz - cz[i];
                const float b    = 2.0f * (fx * dx + fz * dz);
                const float c    = fx * fx + fz * fz - radius * radius;
                const float disc = b * b - 4.0f * a * c;
                if (disc < 0.0f)
                    continue;
                const float root = std::sqrt(disc);
                take((-b - root) / (2.0f * a));
                take((-b + root) / (2.0f * a));
            }
        }

        if (best > maxDist)
            return false;
        outDist = best;
        return true;
    }

    bool castWall(void* user, const Vector3f& from, const Vector3f& dir, float maxDist, float& outDist)
    {
        auto* world = static_cast<WallWorld*>(user);
        float best  = maxDist + 1.0f;
        bool  hit   = false;
        for (int i = 0; i < world->count; ++i)
        {
            const WallSolid& solid = world->solids[i];
            if (from.y - 0.1f > solid.topY)
                continue;
            float dist = 0.0f;
            if (sweepCircle(from.x, from.z, dir.x, dir.z, maxDist, solid, dist) && dist < best)
            {
                best = dist;
                hit  = true;
            }
        }
        if (!hit)
            return false;
        outDist = best;
        return true;
    }

    bool roofAt(void* user, float x, float z, float, float& outTopY)
    {
        auto* world = static_cast<WallWorld*>(user);
        for (int i = 0; i < world->count; ++i)
        {
            const WallSolid& solid = world->solids[i];
            if (!solid.roof || x < solid.minX || x > solid.maxX || z < solid.minZ || z > solid.maxZ)
                continue;
            outTopY = solid.topY;
            return true;
        }
        return false;
    }

    PlayerGroundQuery wallQuery(WallWorld& world)
    {
        PlayerGroundQuery query = flatQuery();
        query.wallAt            = castWall;
        query.ledgeAt           = roofAt;
        query.wallUser          = &world;
        return query;
    }

    void beginClimb(PlayerMotor& motor, PlayerMoveState state, const Vector3f& wallDir)
    {
        motor.restoreProgress(state, Vector3f{ 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f, false, false, 0.0f, Vector3f{ 0.0f, 0.0f, 0.0f });
        motor.restoreClimb(1.0f, 0.0f, wallDir);
    }
} // namespace

TEST(ApproachSpeed, RampsUpDownAndSnapsWhenUnset)
{
    EXPECT_FLOAT_EQ(approachSpeed(0.0f, 3.6f, 0.0f, 0.25f), 3.6f);
    EXPECT_FLOAT_EQ(approachSpeed(0.0f, 3.6f, 8.0f, 0.25f), 2.0f);
    EXPECT_FLOAT_EQ(approachSpeed(3.6f, 7.0f, 8.0f, 0.25f), 5.6f);
    EXPECT_FLOAT_EQ(approachSpeed(7.0f, 3.6f, 8.0f, 0.25f), 5.0f);
    EXPECT_FLOAT_EQ(approachSpeed(2.0f, 0.0f, 8.0f, 0.25f), 0.0f);
    EXPECT_FLOAT_EQ(approachSpeed(1.0f, 3.0f, 8.0f, 0.0f), 3.0f);
}

TEST(PlayerMotor, AccelerationRampsWalkRunAndStop)
{
    PlayerMotor motor;
    motor.settings().acceleration = 8.0f;
    Vector3f pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput in{};
    in.wish = Vector3f{ 0.0f, 0.0f, 1.0f };
    constexpr float kDt = 0.25f;

    motor.tick(pos, in, kDt, flatQuery());
    EXPECT_NEAR(motor.velocity().z, 2.0f, 1.0e-4f);
    EXPECT_NEAR(pos.z, 2.0f * kDt, 1.0e-4f);
    EXPECT_LT(motor.velocity().z, motor.settings().walkSpeed);

    motor.tick(pos, in, kDt, flatQuery());
    EXPECT_NEAR(motor.velocity().z, motor.settings().walkSpeed, 1.0e-4f);

    in.sprint = true;
    motor.tick(pos, in, kDt, flatQuery());
    EXPECT_NEAR(motor.velocity().z, motor.settings().walkSpeed + 2.0f, 1.0e-4f);

    const float gained = pos.z;
    in.wish   = Vector3f{ 0.0f, 0.0f, 0.0f };
    in.sprint = false;
    const float before = motor.velocity().z;
    motor.tick(pos, in, kDt, flatQuery());
    EXPECT_NEAR(motor.velocity().z, before - 2.0f, 1.0e-4f);
    EXPECT_GT(pos.z, gained);
}

TEST(PlayerMotor, AccelerationDoesNotSoftenDodgeOrSwim)
{
    PlayerMotor motor;
    motor.settings().acceleration = 8.0f;
    Vector3f pos{ 0.0f, 0.5f, 0.0f };
    motor.restoreProgress(PlayerMoveState::Dodge, Vector3f{ 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f, false, false, 0.5f, Vector3f{ 0.0f, 0.0f, 1.0f });
    PlayerMotorInput dodge{};
    constexpr float kDt = 0.25f;
    motor.tick(pos, dodge, kDt, flatQuery());
    EXPECT_EQ(motor.state(), PlayerMoveState::Dodge);
    EXPECT_NEAR(motor.velocity().z, motor.settings().dodgeSpeed, 1.0e-4f);
    EXPECT_NEAR(pos.z, motor.settings().dodgeSpeed * kDt, 1.0e-4f);

    PlayerMotor swimmer;
    swimmer.settings().acceleration = 8.0f;
    Vector3f swimPos{ 0.0f, 0.35f, 0.0f };
    const PlayerGroundQuery water = flatQuery(2.0f);
    PlayerMotorInput hold{};
    swimmer.tick(swimPos, hold, 1.0f / 60.0f, water);
    ASSERT_EQ(swimmer.state(), PlayerMoveState::Swimming);
    PlayerMotorInput swim{};
    swim.wish = Vector3f{ 0.0f, 0.0f, 1.0f };
    const float z0 = swimPos.z;
    swimmer.tick(swimPos, swim, kDt, water);
    EXPECT_NEAR(swimPos.z - z0, swimmer.settings().swimSpeed * kDt, 1.0e-4f);
}

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

TEST(PlayerMotor, CrouchLowersTheRootAndStandsBackUp)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerMotorInput in{};
    in.crouch = true;
    advance(motor, pos, in, 0.25f, flatQuery());
    EXPECT_EQ(motor.state(), PlayerMoveState::Crouch);
    EXPECT_NEAR(motor.crouchOffset(), motor.settings().crouchDrop, 1.0e-3f);
    EXPECT_NEAR(pos.y, motor.settings().groundOffset - motor.settings().crouchDrop, 1.0e-3f);

    in.crouch = false;
    advance(motor, pos, in, 0.25f, flatQuery());
    EXPECT_EQ(motor.state(), PlayerMoveState::Grounded);
    EXPECT_NEAR(motor.crouchOffset(), 0.0f, 1.0e-3f);
    EXPECT_NEAR(pos.y, motor.settings().groundOffset, 1.0e-3f);
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

TEST(PlayerMotor, JumpAtSurfaceLeapsOutOfWater)
{
    PlayerMotor             motor;
    Vector3f                pos{ 0.0f, 0.35f, 0.0f };
    const PlayerGroundQuery water = flatQuery(4.0f);
    motor.tick(pos, PlayerMotorInput{}, 1.0f / 60.0f, water);
    ASSERT_EQ(motor.state(), PlayerMoveState::Swimming);
    PlayerMotorInput press{};
    press.jumpPressed           = true;
    const PlayerMotorResult out = motor.tick(pos, press, 1.0f / 60.0f, water);
    EXPECT_TRUE(out.leapt);
    EXPECT_EQ(motor.state(), PlayerMoveState::Jumping);
    EXPECT_GT(motor.velocity().y, 0.0f);
}

TEST(PlayerMotor, DivingDrainsBreathAndHidesBelowSurface)
{
    PlayerMotor             motor;
    Vector3f                pos{ 0.0f, 0.35f, 0.0f };
    const PlayerGroundQuery water = flatQuery(4.0f);
    motor.tick(pos, PlayerMotorInput{}, 1.0f / 60.0f, water);
    PlayerMotorInput dive{};
    dive.crouch = true;
    advance(motor, pos, dive, 2.0f, water);
    EXPECT_TRUE(motor.submerged());
    EXPECT_LT(pos.y, 4.0f);
    advance(motor, pos, dive, 5.0f, water);
    EXPECT_LT(motor.breath(), 1.0f);
    PlayerMotorInput up{};
    up.jumpHeld = true;
    advance(motor, pos, up, 3.0f, water);
    EXPECT_FALSE(motor.submerged());
    EXPECT_NEAR(motor.depth(), 0.0f, 1.0e-4f);
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
    EXPECT_NEAR(motor.settings().walkSpeed * 0.5f, 1.8f, 1.0e-4f);
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
    for (int i = 0; i < 40; ++i)
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
    for (int i = 0; i < 40; ++i)
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
        EXPECT_FLOAT_EQ(motor.settings().dodgeSpeed, 10.0f);
        return pos.z - z0;
    };

    const float elapsed = kStep * static_cast<float>(kSteps);
    const float plain   = dodgeDelta(kSkillIdentity);
    const float scaled  = dodgeDelta(kRunScaleMax);
    EXPECT_NEAR(scaled, plain, 1.0e-4f);
    EXPECT_NEAR(scaled, 10.0f * elapsed, 1.0e-3f);
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
    ASSERT_FLOAT_EQ(motor.settings().sprintSpeed, 7.0f);
    EXPECT_NEAR(pos.z, motor.settings().sprintSpeed * kRunScaleMax * kDt, 1.0e-4f);
    EXPECT_NEAR(motor.velocity().z, motor.settings().sprintSpeed * kRunScaleMax, 1.0e-4f);
    EXPECT_FLOAT_EQ(motor.settings().sprintSpeed, 7.0f);
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
    ASSERT_FLOAT_EQ(motor.settings().walkSpeed, 3.6f);
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
    ASSERT_FLOAT_EQ(motor.settings().crouchSpeed, 1.6f);
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
    ASSERT_FLOAT_EQ(sprinter.settings().sprintSpeed, 7.0f);
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

namespace
{
    struct BedLevel
    {
        float y = 0.0f;
    };

    float bedHeight(void* user, float, float)
    {
        return static_cast<const BedLevel*>(user)->y;
    }

    PlayerGroundQuery bedQuery(BedLevel& bed, float waterY)
    {
        PlayerGroundQuery q;
        q.heightAt = bedHeight;
        q.user     = &bed;
        q.waterY   = waterY;
        return q;
    }
}

TEST(PlayerMotor, ShallowWaterStaysOnTheBed)
{
    PlayerMotor motor;
    BedLevel    bed{ 0.0f };
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    const PlayerGroundQuery water = bedQuery(bed, 0.5f);
    PlayerMotorInput walk{};
    walk.wish = Vector3f{ 0.0f, 0.0f, 1.0f };
    advance(motor, pos, walk, 0.4f, water);
    EXPECT_EQ(motor.state(), PlayerMoveState::Grounded);
    EXPECT_EQ(motor.posture(), SwimPosture::None);
    EXPECT_NEAR(pos.y, 0.5f, 1.0e-3f);
    EXPECT_GT(pos.z, 0.2f);
}

TEST(PlayerMotor, DeepWaterDivesThenTreadsAndStands)
{
    PlayerMotor motor;
    BedLevel    bed{ 0.0f };
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    PlayerGroundQuery water = bedQuery(bed, 1.5f);
    PlayerMotorInput push{};
    push.wish = Vector3f{ 0.0f, 0.0f, 1.0f };
    const PlayerMotorResult entered = motor.tick(pos, push, 1.0f / 60.0f, water);
    EXPECT_TRUE(entered.splashed);
    EXPECT_EQ(motor.state(), PlayerMoveState::Swimming);
    EXPECT_EQ(motor.posture(), SwimPosture::Diving);

    advance(motor, pos, push, 0.50f, water);
    EXPECT_EQ(motor.posture(), SwimPosture::Horizontal);
    EXPECT_NEAR(motor.swimPose(), 1.0f, 0.05f);
    EXPECT_NEAR(pos.y, 1.5f - motor.settings().swimHipDrop, 0.08f);

    PlayerMotorInput still{};
    advance(motor, pos, still, 0.35f, water);
    EXPECT_EQ(motor.posture(), SwimPosture::Treading);
    EXPECT_LT(motor.swimPose(), 0.15f);

    push.wish = Vector3f{ 0.0f, 0.0f, 1.0f };
    advance(motor, pos, push, 0.35f, water);
    EXPECT_EQ(motor.posture(), SwimPosture::Horizontal);

    bed.y = 1.1f;
    const PlayerMotorResult left = motor.tick(pos, push, 1.0f / 60.0f, water);
    EXPECT_TRUE(left.stood);
    EXPECT_EQ(motor.state(), PlayerMoveState::Grounded);
    EXPECT_EQ(motor.posture(), SwimPosture::Standing);
    advance(motor, pos, push, 0.60f, water);
    EXPECT_EQ(motor.posture(), SwimPosture::None);
    EXPECT_EQ(motor.state(), PlayerMoveState::Grounded);
    EXPECT_NEAR(pos.y, bed.y + motor.settings().groundOffset, 0.05f);
}

TEST(PlayerMotor, OneMeterColumnKeepsTheCurrentMode)
{
    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.0f };
    const PlayerGroundQuery line = flatQuery(1.0f);
    motor.tick(pos, PlayerMotorInput{}, 1.0f / 60.0f, line);
    EXPECT_EQ(motor.state(), PlayerMoveState::Grounded);

    PlayerMotor swimmer;
    Vector3f    swimPos{ 0.0f, 0.5f, 0.0f };
    swimmer.tick(swimPos, PlayerMotorInput{}, 1.0f / 60.0f, flatQuery(2.0f));
    ASSERT_EQ(swimmer.state(), PlayerMoveState::Swimming);
    swimmer.tick(swimPos, PlayerMotorInput{}, 1.0f / 60.0f, line);
    EXPECT_EQ(swimmer.state(), PlayerMoveState::Swimming);
}

TEST(PlayerMotor, WallRunClimbsAWideFace)
{
    WallWorld world;
    world.count          = 1;
    world.solids[0]      = WallSolid{ -4.0f, 4.0f, 1.0f, 1.3f, 100.0f, false };
    const PlayerGroundQuery query = wallQuery(world);

    PlayerMotor motor;
    beginClimb(motor, PlayerMoveState::WallRun, Vector3f{ 0.0f, 0.0f, 1.0f });
    Vector3f         pos{ 0.0f, 1.0f, 0.65f };
    PlayerMotorInput in;
    in.wish = Vector3f{ 0.0f, 0.0f, 1.0f };
    advance(motor, pos, in, 0.30f, query);

    EXPECT_EQ(motor.state(), PlayerMoveState::WallRun);
    EXPECT_GT(pos.y, 1.5f);
    EXPECT_NEAR(pos.x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(pos.z, 0.65f, 1.0e-4f);
}

TEST(PlayerMotor, WallRunStrafeSlidesAlongTheFace)
{
    WallWorld world;
    world.count     = 1;
    world.solids[0] = WallSolid{ -4.0f, 4.0f, 1.0f, 1.3f, 100.0f, false };
    const PlayerGroundQuery query = wallQuery(world);

    PlayerMotor motor;
    beginClimb(motor, PlayerMoveState::WallRun, Vector3f{ 0.0f, 0.0f, 1.0f });
    Vector3f         pos{ 0.0f, 1.0f, 0.65f };
    PlayerMotorInput in;
    in.wish = Vector3f{ 1.0f, 0.0f, 0.0f };
    advance(motor, pos, in, 0.30f, query);

    EXPECT_EQ(motor.state(), PlayerMoveState::WallRun);
    EXPECT_GT(pos.x, 0.8f);
    EXPECT_NEAR(pos.y, 1.0f, 0.05f);
    EXPECT_NEAR(pos.z, 0.65f, 0.05f);
}

TEST(PlayerMotor, WallRunBackClimbsDown)
{
    WallWorld world;
    world.count     = 1;
    world.solids[0] = WallSolid{ -4.0f, 4.0f, 1.0f, 1.3f, 100.0f, false };
    const PlayerGroundQuery query = wallQuery(world);

    PlayerMotor motor;
    beginClimb(motor, PlayerMoveState::WallRun, Vector3f{ 0.0f, 0.0f, 1.0f });
    Vector3f         pos{ 0.0f, 3.0f, 0.65f };
    PlayerMotorInput in;
    in.wish = Vector3f{ 0.0f, 0.0f, -1.0f };
    advance(motor, pos, in, 0.30f, query);

    EXPECT_EQ(motor.state(), PlayerMoveState::WallRun);
    EXPECT_LT(pos.y, 2.2f);
    EXPECT_NEAR(pos.x, 0.0f, 0.05f);
    EXPECT_NEAR(pos.z, 0.65f, 0.05f);
}

TEST(PlayerMotor, WallRunStrafeUnderALedgeKeepsMoving)
{
    WallWorld world;
    world.count     = 1;
    world.solids[0] = WallSolid{ -4.0f, 4.0f, 1.0f, 1.3f, 2.0f, true };
    const PlayerGroundQuery query = wallQuery(world);

    PlayerMotor motor;
    beginClimb(motor, PlayerMoveState::WallRun, Vector3f{ 0.0f, 0.0f, 1.0f });
    Vector3f         pos{ 0.0f, 1.5f, 0.65f };
    PlayerMotorInput in;
    in.wish = Vector3f{ 1.0f, 0.0f, 0.0f };
    advance(motor, pos, in, 0.20f, query);

    EXPECT_EQ(motor.state(), PlayerMoveState::WallRun);
    EXPECT_GT(pos.x, 0.4f);
    EXPECT_NEAR(pos.y, 1.5f, 0.05f);
}

TEST(PlayerMotor, HangShimmySlidesAlongTheLedge)
{
    WallWorld world;
    world.count     = 1;
    world.solids[0] = WallSolid{ -4.0f, 4.0f, 1.0f, 1.3f, 100.0f, false };
    const PlayerGroundQuery query = wallQuery(world);

    PlayerMotor motor;
    beginClimb(motor, PlayerMoveState::Hang, Vector3f{ 0.0f, 0.0f, 1.0f });
    Vector3f         pos{ 0.0f, 2.0f, 0.65f };
    PlayerMotorInput in;
    in.wish = Vector3f{ -1.0f, 0.0f, 0.0f };
    advance(motor, pos, in, 0.30f, query);

    EXPECT_EQ(motor.state(), PlayerMoveState::Hang);
    EXPECT_LT(pos.x, -0.8f);
    EXPECT_NEAR(pos.y, 2.0f, 0.05f);
    EXPECT_NEAR(pos.z, 0.65f, 0.05f);
}

TEST(PlayerMotor, WallRunNearTheEdgeStillClimbs)
{
    WallWorld world;
    world.count     = 1;
    world.solids[0] = WallSolid{ -2.0f, 0.0f, 1.0f, 1.3f, 100.0f, false };
    const PlayerGroundQuery query = wallQuery(world);

    PlayerMotor motor;
    beginClimb(motor, PlayerMoveState::WallRun, Vector3f{ 0.0f, 0.0f, 1.0f });
    Vector3f         pos{ 0.05f, 1.0f, 0.65f };
    PlayerMotorInput in;
    in.wish = Vector3f{ 0.0f, 0.0f, 1.0f };
    advance(motor, pos, in, 0.30f, query);

    EXPECT_EQ(motor.state(), PlayerMoveState::WallRun);
    EXPECT_GT(pos.y, 1.5f);
    EXPECT_NEAR(pos.x, 0.05f, 1.0e-3f);
}

TEST(PlayerMotor, WallRunPushOffTheEdgeLetsGo)
{
    WallWorld world;
    world.count     = 1;
    world.solids[0] = WallSolid{ -2.0f, 0.0f, 1.0f, 1.3f, 100.0f, false };
    const PlayerGroundQuery query = wallQuery(world);

    PlayerMotor motor;
    beginClimb(motor, PlayerMoveState::WallRun, Vector3f{ 0.0f, 0.0f, 1.0f });
    Vector3f         pos{ 0.05f, 2.0f, 0.65f };
    PlayerMotorInput in;
    in.wish = Vector3f{ 1.0f, 0.0f, 0.0f };
    motor.tick(pos, in, 1.0f / 60.0f, query);

    EXPECT_EQ(motor.state(), PlayerMoveState::Falling);
    EXPECT_GT(pos.x, 0.20f);
    advance(motor, pos, in, 0.20f, query);
    EXPECT_NE(motor.state(), PlayerMoveState::WallRun);
    EXPECT_NE(motor.state(), PlayerMoveState::Hang);
    EXPECT_GT(pos.x, 0.50f);
}

TEST(PlayerMotor, WallRunOutsideCornerLetsGo)
{
    WallWorld world;
    world.count     = 1;
    world.solids[0] = WallSolid{ 0.0f, 4.0f, 0.0f, 4.0f, 100.0f, false };
    const PlayerGroundQuery query = wallQuery(world);
    const Vector3f into{ 0.70710678f, 0.0f, 0.70710678f };

    PlayerMotor motor;
    beginClimb(motor, PlayerMoveState::WallRun, into);
    Vector3f         pos{ -0.25f, 3.0f, -0.25f };
    PlayerMotorInput in;
    in.wish = into;
    motor.tick(pos, in, 1.0f / 60.0f, query);

    EXPECT_EQ(motor.state(), PlayerMoveState::Falling);
    EXPECT_LT(pos.x, -0.35f);
    EXPECT_LT(pos.z, -0.35f);
    advance(motor, pos, in, 0.25f, query);
    EXPECT_NE(motor.state(), PlayerMoveState::WallRun);
    EXPECT_NE(motor.state(), PlayerMoveState::Hang);
}

TEST(PlayerMotor, HangPushOffTheEdgeLetsGo)
{
    WallWorld world;
    world.count     = 1;
    world.solids[0] = WallSolid{ -2.0f, 0.0f, 1.0f, 1.3f, 100.0f, false };
    const PlayerGroundQuery query = wallQuery(world);

    PlayerMotor motor;
    beginClimb(motor, PlayerMoveState::Hang, Vector3f{ 0.0f, 0.0f, 1.0f });
    Vector3f         pos{ 0.05f, 2.0f, 0.65f };
    PlayerMotorInput in;
    in.wish = Vector3f{ 1.0f, 0.0f, 0.0f };
    motor.tick(pos, in, 1.0f / 60.0f, query);

    EXPECT_EQ(motor.state(), PlayerMoveState::Falling);
    EXPECT_GT(pos.x, 0.20f);

    PlayerMotor hold;
    beginClimb(hold, PlayerMoveState::Hang, Vector3f{ 0.0f, 0.0f, 1.0f });
    WallWorld wide;
    wide.count     = 1;
    wide.solids[0] = WallSolid{ -4.0f, 4.0f, 1.0f, 1.3f, 100.0f, false };
    const PlayerGroundQuery face = wallQuery(wide);
    Vector3f                   mid{ 0.0f, 2.0f, 0.65f };
    advance(hold, mid, PlayerMotorInput{}, 0.20f, face);
    EXPECT_EQ(hold.state(), PlayerMoveState::Hang);
    EXPECT_NEAR(mid.x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(mid.z, 0.65f, 1.0e-4f);
}

TEST(PlayerMotor, MantleOntoAClearRoof)
{
    WallWorld world;
    world.count     = 1;
    world.solids[0] = WallSolid{ -4.0f, 4.0f, 1.0f, 1.3f, 2.0f, true };
    const PlayerGroundQuery query = wallQuery(world);

    PlayerMotor motor;
    beginClimb(motor, PlayerMoveState::WallRun, Vector3f{ 0.0f, 0.0f, 1.0f });
    Vector3f         pos{ 0.0f, 1.5f, 0.65f };
    PlayerMotorInput in;
    in.wish = Vector3f{ 0.0f, 0.0f, 1.0f };
    const PlayerMotorResult result = motor.tick(pos, in, 1.0f / 60.0f, query);

    EXPECT_TRUE(result.climbed);
    EXPECT_EQ(motor.state(), PlayerMoveState::Grounded);
    EXPECT_NEAR(pos.y, 2.5f, 1.0e-3f);
    EXPECT_GT(pos.z, 1.3f);
}

TEST(PlayerMotor, MantleBesideATallerWallLetsGo)
{
    WallWorld world;
    world.count     = 2;
    world.solids[0] = WallSolid{ -4.0f, 4.0f, 1.0f, 1.3f, 2.0f, true };
    world.solids[1] = WallSolid{ 0.55f, 1.2f, 1.2f, 4.0f, 30.0f, false };
    const PlayerGroundQuery query = wallQuery(world);

    PlayerMotor motor;
    beginClimb(motor, PlayerMoveState::WallRun, Vector3f{ 0.0f, 0.0f, 1.0f });
    Vector3f         pos{ 0.0f, 1.5f, 0.65f };
    PlayerMotorInput in;
    in.wish = Vector3f{ 0.0f, 0.0f, 1.0f };
    const PlayerMotorResult result = motor.tick(pos, in, 1.0f / 60.0f, query);

    EXPECT_FALSE(result.climbed);
    EXPECT_EQ(motor.state(), PlayerMoveState::Falling);
    EXPECT_LT(pos.z, 0.65f);
}

TEST(PlayerMotor, JumpBesideAWallDoesNotClimb)
{
    WallWorld world;
    world.count     = 1;
    world.solids[0] = WallSolid{ -4.0f, 4.0f, 1.0f, 1.3f, 100.0f, false };
    const PlayerGroundQuery query = wallQuery(world);

    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.65f };
    PlayerMotorInput in;
    in.jumpPressed = true;
    in.wish        = Vector3f{ 0.0f, 0.0f, 1.0f };
    const PlayerMotorResult result = motor.tick(pos, in, 1.0f / 60.0f, query);

    EXPECT_TRUE(result.jumped);
    EXPECT_EQ(motor.state(), PlayerMoveState::Jumping);
    EXPECT_GT(motor.velocity().y, 0.0f);
}

TEST(PlayerMotor, AirborneWishDoesNotGrabAWall)
{
    WallWorld world;
    world.count     = 1;
    world.solids[0] = WallSolid{ -4.0f, 4.0f, 1.0f, 1.3f, 100.0f, false };
    const PlayerGroundQuery query = wallQuery(world);

    PlayerMotor motor;
    motor.restoreProgress(PlayerMoveState::Falling, Vector3f{ 0.0f, -1.0f, 0.4f }, 0.2f, 0.0f, true, false, 0.0f, Vector3f{ 0.0f, 0.0f, 0.0f });
    Vector3f         pos{ 0.0f, 2.0f, 0.65f };
    PlayerMotorInput in;
    in.wish = Vector3f{ 0.0f, 0.0f, 1.0f };
    motor.tick(pos, in, 1.0f / 60.0f, query);

    EXPECT_EQ(motor.state(), PlayerMoveState::Falling);

    in.climbPressed = true;
    motor.tick(pos, in, 1.0f / 60.0f, query);
    EXPECT_EQ(motor.state(), PlayerMoveState::WallRun);
}

TEST(PlayerMotor, InteractStartsAWallClimb)
{
    WallWorld world;
    world.count     = 1;
    world.solids[0] = WallSolid{ -4.0f, 4.0f, 1.0f, 1.3f, 100.0f, false };
    const PlayerGroundQuery query = wallQuery(world);

    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 0.5f, 0.65f };
    const ClimbGrab offer = motor.findClimbGrab(query, pos);
    EXPECT_TRUE(offer.valid);
    EXPECT_FALSE(offer.downward);

    PlayerMotorInput in;
    in.climbPressed = true;
    in.wish         = Vector3f{ 0.0f, 0.0f, 1.0f };
    motor.tick(pos, in, 1.0f / 60.0f, query);
    EXPECT_EQ(motor.state(), PlayerMoveState::WallRun);
    EXPECT_FALSE(motor.findClimbGrab(query, pos).valid);

    in.climbPressed = false;
    advance(motor, pos, in, 0.30f, query);
    EXPECT_EQ(motor.state(), PlayerMoveState::WallRun);
    EXPECT_GT(pos.y, 1.2f);
}

TEST(PlayerMotor, InteractOnTheLipClimbsDown)
{
    WallWorld world;
    world.count     = 1;
    world.solids[0] = WallSolid{ -4.0f, 4.0f, 1.0f, 1.3f, 3.5f, false };
    const PlayerGroundQuery query = wallQuery(world);

    PlayerMotor motor;
    Vector3f    pos{ 0.0f, 4.0f, 1.15f };
    const ClimbGrab offer = motor.findClimbGrab(query, pos);
    EXPECT_TRUE(offer.valid);
    EXPECT_TRUE(offer.downward);

    PlayerMotorInput in;
    in.climbPressed = true;
    motor.tick(pos, in, 1.0f / 60.0f, query);
    EXPECT_EQ(motor.state(), PlayerMoveState::WallRun);
    EXPECT_LT(pos.y, 3.6f);
    EXPECT_GT(pos.z, 1.6f);

    in.climbPressed = false;
    advance(motor, pos, in, 0.30f, query);
    EXPECT_EQ(motor.state(), PlayerMoveState::WallRun);
    EXPECT_LT(pos.y, 2.5f);

    const float yBeforeSteer = pos.y;
    in.wish = Vector3f{ 1.0f, 0.0f, 0.0f };
    motor.tick(pos, in, 1.0f / 60.0f, query);
    EXPECT_NEAR(motor.velocity().y, 0.0f, 0.05f);
    EXPECT_GT(pos.y, yBeforeSteer - 0.05f);
}

TEST(PlayerMotor, WallDrainIsQuieterWhenNotClimbingUp)
{
    WallWorld world;
    world.count     = 1;
    world.solids[0] = WallSolid{ -8.0f, 8.0f, 1.0f, 1.3f, 100.0f, false };
    const PlayerGroundQuery query = wallQuery(world);

    auto remaining = [&](PlayerMoveState state, const Vector3f& wish, const Vector3f& pos0) {
        PlayerMotor motor;
        beginClimb(motor, state, Vector3f{ 0.0f, 0.0f, 1.0f });
        Vector3f         pos = pos0;
        PlayerMotorInput in;
        in.wish = wish;
        advance(motor, pos, in, 1.0f, query);
        EXPECT_EQ(motor.state(), state);
        return motor.stamina();
    };

    const float up   = remaining(PlayerMoveState::WallRun, Vector3f{ 0.0f, 0.0f, 1.0f }, Vector3f{ 0.0f, 2.0f, 0.65f });
    const float side = remaining(PlayerMoveState::WallRun, Vector3f{ 1.0f, 0.0f, 0.0f }, Vector3f{ -2.0f, 2.0f, 0.65f });
    const float idle = remaining(PlayerMoveState::WallRun, Vector3f{ 0.0f, 0.0f, 0.0f }, Vector3f{ 0.0f, 2.0f, 0.65f });
    const float hang = remaining(PlayerMoveState::Hang, Vector3f{ 0.0f, 0.0f, 0.0f }, Vector3f{ 0.0f, 2.0f, 0.65f });

    EXPECT_NEAR(up, 0.55f, 0.02f);
    EXPECT_NEAR(side, 0.88f, 0.02f);
    EXPECT_NEAR(idle, 0.97f, 0.02f);
    EXPECT_NEAR(hang, 0.97f, 0.02f);
    EXPECT_GT(idle, side);
    EXPECT_GT(side, up);
}
