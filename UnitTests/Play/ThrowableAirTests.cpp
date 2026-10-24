#include <gtest/gtest.h>

#include "Math/MathDefines.h"
#include "Play/WeaponFire.h"
#include "Weapons/ThrowableFlight.h"

#include <cmath>

using namespace Dark;
using namespace Dark::Math;

namespace
{

    float heightZero(void*, float, float)
    {
        return 0.0f;
    }

    ThrowableWorld flatGround()
    {
        ThrowableWorld world;
        world.hasTerrain = true;
        world.heightAt   = heightZero;
        return world;
    }

    Vector3f aimFromPitch(float pitch)
    {
        return Vector3f{ 0.0f, std::sin(pitch), std::cos(pitch) };
    }

    ThrowableLaunch makeLaunch(float distance, float weight, float pitch, bool charged)
    {
        ThrowableLaunch launch;
        launch.itemId        = "rock";
        launch.impactClip    = "audio/foot_rock.wav";
        launch.weight        = weight;
        launch.throwDistance = distance;
        launch.origin        = Vector3f{ 0.0f, kThrowableRadius, 0.0f };
        launch.direction     = aimFromPitch(pitch);
        launch.charged       = charged;
        return launch;
    }

    float horizontal(const Vector3f& from, const Vector3f& to)
    {
        const float dx = to.x - from.x;
        const float dz = to.z - from.z;
        return std::sqrt(dx * dx + dz * dz);
    }

    bool flyUntilLanding(ThrowableFlights& flights, const ThrowableWorld& world, float maxTime, ThrowableLanding& out)
    {
        const float dt = 1.0f / 60.0f;
        float       t  = 0.0f;
        while (t < maxTime)
        {
            flights.tick(dt, world);
            t += dt;
            if (!flights.landings().empty())
            {
                out = flights.landings().front();
                return true;
            }
        }
        return false;
    }

} // namespace

// Current rifle gate, locked before the throwable airborne branch: a press jump-attacks and does not throw.
TEST(ThrowableAir, AirborneRiflePressJumpAttacksAndDoesNotThrow)
{
    WeaponAttackInput in{};
    in.airborne      = true;
    in.swordOrRifle  = true;
    in.attackPressed = true;

    const WeaponAttackDecision decision = decideWeaponAttack(in);
    EXPECT_TRUE(decision.beginJumpAttack);
    EXPECT_FALSE(decision.throwOnRelease);
    EXPECT_FALSE(decision.fireLoadoutOnRelease);
    EXPECT_FALSE(decision.allowCharge);
    EXPECT_TRUE(decision.suppressRelease);
}

TEST(ThrowableAir, AirborneThrowablePressDoesNotJumpAttack)
{
    WeaponAttackInput in{};
    in.airborne      = true;
    in.throwable     = true;
    in.attackPressed = true;

    const WeaponAttackDecision decision = decideWeaponAttack(in);
    EXPECT_FALSE(decision.beginJumpAttack);
    EXPECT_TRUE(decision.allowCharge);
    EXPECT_FALSE(decision.throwOnRelease);
    EXPECT_FALSE(decision.fireLoadoutOnRelease);
    EXPECT_FALSE(decision.suppressRelease);
}

TEST(ThrowableAir, AirborneThrowableTapAndChargedRelease)
{
    WeaponAttackInput tap{};
    tap.airborne  = true;
    tap.throwable = true;
    tap.fireQuick = true;

    const WeaponAttackDecision tapDecision = decideWeaponAttack(tap);
    EXPECT_TRUE(tapDecision.throwOnRelease);
    EXPECT_FALSE(tap.fireCharged);
    EXPECT_FALSE(tapDecision.beginJumpAttack);
    EXPECT_FALSE(tapDecision.fireLoadoutOnRelease);

    WeaponAttackInput charged = tap;
    charged.fireQuick   = false;
    charged.fireCharged = true;
    const WeaponAttackDecision chargedDecision = decideWeaponAttack(charged);
    EXPECT_TRUE(chargedDecision.throwOnRelease);
    EXPECT_TRUE(charged.fireCharged);
    EXPECT_FALSE(chargedDecision.beginJumpAttack);

    WeaponAttackInput grounded = tap;
    grounded.airborne = false;
    EXPECT_TRUE(decideWeaponAttack(grounded).throwOnRelease);
    EXPECT_FALSE(grounded.fireCharged);
}

TEST(ThrowableAir, AirborneChargedThrowTravelsFartherThanTap)
{
    constexpr float kDistance = 12.0f;
    constexpr float kWeight   = 0.75f;
    float           pitch     = 0.0f;
    ASSERT_TRUE(throwableChargedPitch(kDistance, kWeight, false, pitch));

    WeaponAttackInput chargedIn{};
    chargedIn.airborne    = true;
    chargedIn.throwable   = true;
    chargedIn.fireCharged = true;
    WeaponAttackInput tapIn = chargedIn;
    tapIn.fireCharged = false;
    tapIn.fireQuick   = true;
    EXPECT_TRUE(decideWeaponAttack(chargedIn).throwOnRelease);
    EXPECT_TRUE(chargedIn.fireCharged);
    EXPECT_TRUE(decideWeaponAttack(tapIn).throwOnRelease);
    EXPECT_FALSE(tapIn.fireCharged);

    ThrowableLaunch chargedLaunch = makeLaunch(kDistance, kWeight, pitch, true);
    ThrowableLaunch tapLaunch     = makeLaunch(kDistance, kWeight, pitch, false);
    chargedLaunch.origin.y        = 3.0f;
    tapLaunch.origin.y            = 3.0f;

    ThrowableFlights charged;
    ThrowableFlights tap;
    ASSERT_TRUE(charged.launch(chargedLaunch));
    ASSERT_TRUE(tap.launch(tapLaunch));

    ThrowableLanding chargedLanding;
    ThrowableLanding tapLanding;
    const ThrowableWorld ground = flatGround();
    ASSERT_TRUE(flyUntilLanding(charged, ground, 8.0f, chargedLanding));
    ASSERT_TRUE(flyUntilLanding(tap, ground, 8.0f, tapLanding));
    EXPECT_TRUE(chargedLanding.hitTerrain);
    EXPECT_TRUE(tapLanding.hitTerrain);

    const Vector3f chargedOrigin = chargedLaunch.origin;
    const Vector3f tapOrigin     = tapLaunch.origin;
    const float    chargedRange  = horizontal(chargedOrigin, chargedLanding.position);
    const float    tapRange      = horizontal(tapOrigin, tapLanding.position);
    EXPECT_GT(chargedRange, tapRange);
}

TEST(ThrowableAir, HardCcAndBusyJumpAttackDoNotThrow)
{
    WeaponAttackInput in{};
    in.airborne    = true;
    in.throwable   = true;
    in.fireCharged = true;
    in.ccLocked    = true;

    WeaponAttackDecision decision = decideWeaponAttack(in);
    EXPECT_FALSE(decision.allowCharge);
    EXPECT_FALSE(decision.throwOnRelease);
    EXPECT_FALSE(decision.beginJumpAttack);

    in.ccLocked = false;
    in.jumpBusy = true;
    decision    = decideWeaponAttack(in);
    EXPECT_FALSE(decision.allowCharge);
    EXPECT_FALSE(decision.throwOnRelease);
    EXPECT_FALSE(decision.beginJumpAttack);
}

TEST(ThrowableAir, SwimThrowableThrowsAndRifleFires)
{
    WeaponAttackInput thrown{};
    thrown.swimming  = true;
    thrown.throwable = true;
    thrown.fireQuick = true;
    const WeaponAttackDecision throwDecision = decideWeaponAttack(thrown);
    EXPECT_TRUE(throwDecision.throwOnRelease);
    EXPECT_FALSE(throwDecision.fireLoadoutOnRelease);
    EXPECT_FALSE(throwDecision.beginJumpAttack);

    thrown.fireQuick   = false;
    thrown.fireCharged = true;
    EXPECT_TRUE(decideWeaponAttack(thrown).throwOnRelease);

    WeaponAttackInput rifle{};
    rifle.swimming    = true;
    rifle.swordOrRifle = true;
    rifle.fireQuick   = true;
    const WeaponAttackDecision rifleDecision = decideWeaponAttack(rifle);
    EXPECT_TRUE(rifleDecision.fireLoadoutOnRelease);
    EXPECT_FALSE(rifleDecision.throwOnRelease);
    EXPECT_FALSE(rifleDecision.beginJumpAttack);
}

TEST(ThrowableAir, LandingDoesNotCancelThrowableHold)
{
    WeaponAttackInput in{};
    in.throwable = true;
    in.airborne  = true;

    EXPECT_TRUE(decideWeaponAttack(in).allowCharge);

    in.airborne = false;
    EXPECT_TRUE(decideWeaponAttack(in).allowCharge);
    EXPECT_FALSE(decideWeaponAttack(in).throwOnRelease);
}

TEST(ThrowableAir, SwapToRifleSuppressesInAirAndFiresOnGround)
{
    WeaponAttackInput air{};
    air.airborne     = true;
    air.swordOrRifle = true;
    const WeaponAttackDecision beforeRelease = decideWeaponAttack(air);
    EXPECT_TRUE(beforeRelease.suppressRelease);
    EXPECT_FALSE(beforeRelease.throwOnRelease);
    EXPECT_FALSE(beforeRelease.fireLoadoutOnRelease);
    EXPECT_FALSE(beforeRelease.beginJumpAttack);

    air.fireCharged = true;
    const WeaponAttackDecision released = decideWeaponAttack(air);
    EXPECT_TRUE(released.suppressRelease);
    EXPECT_FALSE(released.throwOnRelease);
    EXPECT_FALSE(released.fireLoadoutOnRelease);
    EXPECT_FALSE(released.beginJumpAttack);

    WeaponAttackInput ground{};
    ground.swordOrRifle = true;
    ground.fireCharged  = true;
    const WeaponAttackDecision grounded = decideWeaponAttack(ground);
    EXPECT_TRUE(grounded.fireLoadoutOnRelease);
    EXPECT_FALSE(grounded.throwOnRelease);
    EXPECT_FALSE(grounded.suppressRelease);
    EXPECT_FALSE(grounded.beginJumpAttack);
}

TEST(ThrowableAir, EditorAndSandboxShareOnePredicate)
{
    WeaponAttackInput rifle{};
    rifle.airborne      = true;
    rifle.swordOrRifle  = true;
    rifle.attackPressed = true;

    WeaponAttackInput sword = rifle;
    WeaponAttackInput thrown{};
    thrown.airborne      = true;
    thrown.throwable     = true;
    thrown.attackPressed = true;

    const WeaponAttackDecision rifleDecision  = decideWeaponAttack(rifle);
    const WeaponAttackDecision swordDecision  = decideWeaponAttack(sword);
    const WeaponAttackDecision thrownDecision = decideWeaponAttack(thrown);

    EXPECT_TRUE(rifleDecision.beginJumpAttack);
    EXPECT_FALSE(rifleDecision.throwOnRelease);
    EXPECT_FALSE(rifleDecision.allowCharge);
    EXPECT_EQ(rifleDecision.beginJumpAttack, swordDecision.beginJumpAttack);
    EXPECT_EQ(rifleDecision.throwOnRelease, swordDecision.throwOnRelease);
    EXPECT_EQ(rifleDecision.allowCharge, swordDecision.allowCharge);
    EXPECT_EQ(rifleDecision.suppressRelease, swordDecision.suppressRelease);
    EXPECT_EQ(rifleDecision.fireLoadoutOnRelease, swordDecision.fireLoadoutOnRelease);

    EXPECT_FALSE(thrownDecision.beginJumpAttack);
    EXPECT_TRUE(thrownDecision.allowCharge);
    EXPECT_FALSE(thrownDecision.throwOnRelease);
}
