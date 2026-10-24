#include <gtest/gtest.h>

#include "Character/Health.h"
#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/ThrowableBelt.h"
#include "Gameplay/ThrowableCatalog.h"
#include "Math/MathDefines.h"
#include "Weapons/ThrowableFlight.h"
#include "Weapons/WeaponLoadout.h"

#include <cmath>
#include <string>
#include <vector>

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

    int pickupCount(World& world)
    {
        int n = 0;
        world.each<ItemPickupComponent>([&](Entity, ItemPickupComponent&) { ++n; });
        return n;
    }

} // namespace

TEST(ThrowableFlight, ChargedPitchLandsAtThrowDistance)
{
    constexpr float kDistance = 12.0f;
    constexpr float kWeight   = 0.75f;
    float           pitch     = 0.0f;
    ASSERT_TRUE(throwableChargedPitch(kDistance, kWeight, true, pitch));
    EXPECT_NEAR(throwableChargedSpeed(kDistance), std::sqrt(kDistance * kThrowableBaseGravity), 1.0e-4f);

    ThrowableFlights flights;
    ASSERT_TRUE(flights.launch(makeLaunch(kDistance, kWeight, pitch, true)));
    ThrowableLanding landing;
    ASSERT_TRUE(flyUntilLanding(flights, flatGround(), 8.0f, landing));
    EXPECT_TRUE(landing.hitTerrain);
    EXPECT_FALSE(landing.expired);
    EXPECT_NEAR(landing.position.y, 0.0f, 0.05f);
    EXPECT_NEAR(horizontal(Vector3f{ 0.0f, kThrowableRadius, 0.0f }, landing.position), kDistance, 0.05f);
}

TEST(ThrowableFlight, HeavierDropsShorterAndHigherAimStillReaches)
{
    constexpr float kDistance = 16.0f;
    constexpr float kLight    = 0.5f;
    constexpr float kHeavy    = 0.75f;
    float           shared    = 0.0f;
    float           higher    = 0.0f;
    ASSERT_TRUE(throwableChargedPitch(kDistance, kLight, false, shared));
    ASSERT_TRUE(throwableChargedPitch(kDistance, kHeavy, true, higher));
    EXPECT_GT(higher, shared);
    EXPECT_FLOAT_EQ(throwableChargedSpeed(kDistance), std::sqrt(kDistance * kThrowableBaseGravity));

    ThrowableFlights lightFlights;
    ThrowableFlights heavySame;
    ThrowableFlights heavyHigh;
    ASSERT_TRUE(lightFlights.launch(makeLaunch(kDistance, kLight, shared, true)));
    ASSERT_TRUE(heavySame.launch(makeLaunch(kDistance, kHeavy, shared, true)));
    ASSERT_TRUE(heavyHigh.launch(makeLaunch(kDistance, kHeavy, higher, true)));

    ThrowableLanding lightLanding;
    ThrowableLanding heavySameLanding;
    ThrowableLanding heavyHighLanding;
    const ThrowableWorld ground = flatGround();
    ASSERT_TRUE(flyUntilLanding(lightFlights, ground, 8.0f, lightLanding));
    ASSERT_TRUE(flyUntilLanding(heavySame, ground, 8.0f, heavySameLanding));
    ASSERT_TRUE(flyUntilLanding(heavyHigh, ground, 8.0f, heavyHighLanding));

    const Vector3f origin{ 0.0f, kThrowableRadius, 0.0f };
    const float    lightRange     = horizontal(origin, lightLanding.position);
    const float    heavySameRange = horizontal(origin, heavySameLanding.position);
    const float    heavyHighRange = horizontal(origin, heavyHighLanding.position);
    EXPECT_NEAR(lightRange, kDistance, 0.05f);
    EXPECT_LT(heavySameRange, lightRange - 1.0f);
    EXPECT_NEAR(heavyHighRange, kDistance, 0.05f);
}

TEST(ThrowableFlight, TapLandsCloserThanCharged)
{
    constexpr float kDistance = 12.0f;
    constexpr float kWeight   = 0.75f;
    float           pitch     = 0.0f;
    ASSERT_TRUE(throwableChargedPitch(kDistance, kWeight, false, pitch));

    ThrowableFlights charged;
    ThrowableFlights tap;
    ASSERT_TRUE(charged.launch(makeLaunch(kDistance, kWeight, pitch, true)));
    ASSERT_TRUE(tap.launch(makeLaunch(kDistance, kWeight, pitch, false)));

    ThrowableLanding chargedLanding;
    ThrowableLanding tapLanding;
    const ThrowableWorld ground = flatGround();
    ASSERT_TRUE(flyUntilLanding(charged, ground, 8.0f, chargedLanding));
    ASSERT_TRUE(flyUntilLanding(tap, ground, 8.0f, tapLanding));

    const Vector3f origin{ 0.0f, kThrowableRadius, 0.0f };
    const float    chargedRange = horizontal(origin, chargedLanding.position);
    const float    tapRange     = horizontal(origin, tapLanding.position);
    EXPECT_NEAR(chargedRange, kDistance, 0.05f);
    EXPECT_LT(tapRange, chargedRange - 1.0f);
    EXPECT_LT(tapRange, kDistance * 0.6f);
    EXPECT_GT(tapRange, kDistance * 0.2f);
}

TEST(ThrowableFlight, ImpactSpawnsSameIdAndReportsClip)
{
    ThrowableLaunch launch = makeLaunch(10.0f, 1.0f, 0.4f, true);
    launch.itemId          = "rock";
    launch.impactClip      = "audio/foot_rock.wav";

    ThrowableFlights flights;
    ASSERT_TRUE(flights.launch(launch));
    ThrowableLanding landing;
    ASSERT_TRUE(flyUntilLanding(flights, flatGround(), 8.0f, landing));
    EXPECT_EQ(landing.itemId, "rock");
    EXPECT_EQ(landing.impactClip, "audio/foot_rock.wav");

    World world;
    EXPECT_EQ(pickupCount(world), 0);
    const Entity pickup = placeThrowablePickup(world, landing.itemId, landing.position);
    ASSERT_TRUE(pickup.valid());
    const ItemPickupComponent* item = world.get<ItemPickupComponent>(pickup);
    const TransformComponent*  xf   = world.get<TransformComponent>(pickup);
    ASSERT_NE(item, nullptr);
    ASSERT_NE(xf, nullptr);
    ASSERT_NE(item->item.def, nullptr);
    EXPECT_STREQ(item->item.def->id, "rock");
    EXPECT_EQ(item->item.count, 1);
    EXPECT_NEAR(xf->position.x, landing.position.x, 1.0e-4f);
    EXPECT_NEAR(xf->position.y, landing.position.y, 1.0e-4f);
    EXPECT_NEAR(xf->position.z, landing.position.z, 1.0e-4f);
    EXPECT_EQ(pickupCount(world), 1);
}

TEST(ThrowableFlight, BodyHitStoresWorldPositionAndDoesNotParent)
{
    World  world;
    Entity thrower = world.createEntity();
    Entity victim  = world.createEntity();
    world.emplace<TransformComponent>(victim, Vector3f{ 0.0f, 2.0f, 3.0f }, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });

    ThrowableBody body;
    body.entity      = victim;
    body.center      = Vector3f{ 0.0f, 2.0f, 3.0f };
    body.halfExtents = Vector3f{ 0.4f, 0.8f, 0.4f };
    body.alive       = true;
    std::vector<ThrowableBody> bodies{ body };

    ThrowableWorld query;
    query.bodies    = bodies.data();
    query.bodyCount = 1;

    ThrowableLaunch launch = makeLaunch(10.0f, 1.0f, 0.0f, true);
    launch.origin          = Vector3f{ 0.0f, 2.0f, 0.0f };
    launch.thrower         = thrower;

    ThrowableFlights flights;
    ASSERT_TRUE(flights.launch(launch));
    ThrowableLanding landing;
    ASSERT_TRUE(flyUntilLanding(flights, query, 2.0f, landing));
    EXPECT_TRUE(landing.hitBody);
    EXPECT_FALSE(landing.expired);
    EXPECT_EQ(landing.hitEntity, victim);
    EXPECT_LT(landing.position.z, 3.0f);
    EXPECT_GT(landing.position.z, 2.0f);

    const Vector3f hit = landing.position;
    const Entity   pickup = placeThrowablePickup(world, landing.itemId, hit);
    ASSERT_TRUE(pickup.valid());
    EXPECT_NE(pickup, victim);
    TransformComponent* victimXf = world.get<TransformComponent>(victim);
    ASSERT_NE(victimXf, nullptr);
    victimXf->position = Vector3f{ 8.0f, 0.0f, -4.0f };
    const TransformComponent* pickupXf = world.get<TransformComponent>(pickup);
    ASSERT_NE(pickupXf, nullptr);
    EXPECT_NEAR(pickupXf->position.x, hit.x, 1.0e-4f);
    EXPECT_NEAR(pickupXf->position.y, hit.y, 1.0e-4f);
    EXPECT_NEAR(pickupXf->position.z, hit.z, 1.0e-4f);
    EXPECT_NE(pickupXf->position, victimXf->position);
}

TEST(ThrowableFlight, OpeningSegmentIgnoresThrowerLaterHitCounts)
{
    Entity thrower{ 7u };
    ThrowableBody body;
    body.entity      = thrower;
    body.center      = Vector3f{ 0.0f, 1.0f, 0.0f };
    body.halfExtents = Vector3f{ 0.5f, 1.0f, 0.5f };
    body.alive       = true;
    std::vector<ThrowableBody> bodies{ body };

    ThrowableWorld query;
    query.bodies    = bodies.data();
    query.bodyCount = 1;

    ThrowableLaunch launch = makeLaunch(10.0f, 1.0f, 0.0f, true);
    launch.origin          = Vector3f{ 0.0f, 1.0f, 0.0f };
    launch.thrower         = thrower;

    ThrowableFlights flights;
    ASSERT_TRUE(flights.launch(launch));

    Vector3f pos{};
    bool     left = false;
    for (int i = 0; i < 30; ++i)
    {
        flights.tick(1.0f / 60.0f, query);
        EXPECT_TRUE(flights.landings().empty());
        ASSERT_TRUE(flights.livePosition(0, pos));
        if (pos.z > body.center.z + body.halfExtents.z + kThrowableRadius + 0.05f)
        {
            left = true;
            break;
        }
    }
    ASSERT_TRUE(left);

    bodies[0].center = Vector3f{ pos.x, pos.y, pos.z + 1.2f };
    query.bodies     = bodies.data();
    ThrowableLanding landing;
    ASSERT_TRUE(flyUntilLanding(flights, query, 1.0f, landing));
    EXPECT_TRUE(landing.hitBody);
    EXPECT_EQ(landing.hitEntity, thrower);
    EXPECT_NEAR(landing.position.z, bodies[0].center.z - bodies[0].halfExtents.z - kThrowableRadius, 0.2f);

    const Vector3f stored = landing.position;
    bodies[0].center      = Vector3f{ 20.0f, 0.0f, 20.0f };
    EXPECT_NEAR(landing.position.z, stored.z, 1.0e-5f);
}

TEST(ThrowableFlight, OpenAirExpirySpawnsAtLastPoint)
{
    ThrowableLaunch launch = makeLaunch(10.0f, 1.0f, 0.2f, true);
    launch.maxLife         = 0.35f;
    launch.impactClip      = "audio/splash.wav";
    launch.itemId          = "bottle";

    ThrowableFlights flights;
    ASSERT_TRUE(flights.launch(launch));
    flights.tick(1.0f, ThrowableWorld{});
    ASSERT_EQ(flights.landings().size(), 1u);
    ASSERT_EQ(flights.liveCount(), 0);
    const ThrowableLanding& landing = flights.landings().front();
    EXPECT_TRUE(landing.expired);
    EXPECT_FALSE(landing.hitTerrain);
    EXPECT_FALSE(landing.hitBody);
    EXPECT_FALSE(landing.hitSolid);
    EXPECT_EQ(landing.impactClip, "audio/splash.wav");

    const float    speed = throwableChargedSpeed(launch.throwDistance);
    const float    g     = throwableGravity(launch.weight);
    const Vector3f dir   = aimFromPitch(0.2f);
    const Vector3f expected = launch.origin + dir * speed * launch.maxLife + Vector3f{ 0.0f, -0.5f * g * launch.maxLife * launch.maxLife, 0.0f };
    EXPECT_NEAR(landing.position.x, expected.x, 0.05f);
    EXPECT_NEAR(landing.position.y, expected.y, 0.05f);
    EXPECT_NEAR(landing.position.z, expected.z, 0.05f);

    World world;
    EXPECT_EQ(pickupCount(world), 0);
    const Entity pickup = placeThrowablePickup(world, landing.itemId, landing.position);
    ASSERT_TRUE(pickup.valid());
    const ItemPickupComponent* item = world.get<ItemPickupComponent>(pickup);
    ASSERT_NE(item, nullptr);
    ASSERT_NE(item->item.def, nullptr);
    EXPECT_STREQ(item->item.def->id, "bottle");
    const TransformComponent* xf = world.get<TransformComponent>(pickup);
    ASSERT_NE(xf, nullptr);
    EXPECT_NEAR(xf->position.y, landing.position.y, 1.0e-4f);
}

TEST(ThrowableFlight, BlockingCubeStopsTheThrow)
{
    const AABox3f cube = AABox3f::FromCenterExtents(Vector3f{ 0.0f, 2.0f, 3.0f }, Vector3f{ 0.5f, 2.0f, 0.5f });
    ThrowableWorld blocked;
    blocked.cubes     = &cube;
    blocked.cubeCount = 1;

    ThrowableLaunch launch = makeLaunch(10.0f, 1.0f, 0.0f, true);
    launch.origin          = Vector3f{ 0.0f, 2.0f, 0.0f };

    ThrowableFlights stopped;
    ThrowableFlights open;
    ASSERT_TRUE(stopped.launch(launch));
    ASSERT_TRUE(open.launch(launch));
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i)
    {
        stopped.tick(dt, blocked);
        open.tick(dt, ThrowableWorld{});
    }
    ASSERT_EQ(stopped.landings().size(), 1u);
    EXPECT_TRUE(open.landings().empty());
    EXPECT_GT(open.liveCount(), 0);
    EXPECT_TRUE(stopped.landings().front().hitSolid);
    EXPECT_FALSE(stopped.landings().front().hitBody);
    EXPECT_LT(stopped.landings().front().position.z, 3.0f);
    EXPECT_GT(stopped.landings().front().position.z, 1.5f);
}

TEST(ThrowableFlight, PlaySphereStopsTheThrow)
{
    const Sphere3f sphere{ Vector3f{ 0.0f, 2.0f, 3.0f }, 0.6f };
    ThrowableWorld blocked;
    blocked.spheres     = &sphere;
    blocked.sphereCount = 1;

    ThrowableLaunch launch = makeLaunch(10.0f, 1.0f, 0.0f, true);
    launch.origin          = Vector3f{ 0.0f, 2.0f, 0.0f };

    ThrowableFlights flights;
    ASSERT_TRUE(flights.launch(launch));
    ThrowableLanding landing;
    ASSERT_TRUE(flyUntilLanding(flights, blocked, 2.0f, landing));
    EXPECT_TRUE(landing.hitSolid);
    EXPECT_LT(landing.position.z, sphere.Center.z);
    EXPECT_GT(landing.position.z, 1.5f);
}

TEST(ThrowableFlight, ImpactDoesNoDamageAndEmitsNoWeaponHit)
{
    int hits = 0;
    WeaponLoadout loadout;
    loadout.setHitListener([](void* user, const WeaponHit&) { *static_cast<int*>(user) += 1; }, &hits);

    ThrowableBelt belt;
    ASSERT_GT(belt.add("rock"), 0);
    ASSERT_GT(belt.add("rock"), 0);
    ASSERT_TRUE(belt.setHand("rock"));
    ASSERT_TRUE(loadout.selectSlot(2, belt));

    World world;
    Entity victim = world.createEntity();
    HealthSettings settings;
    settings.maxHp       = 40.0f;
    settings.regenPerSec = 0.0f;
    HealthComponent health;
    health.health = Health{ settings };
    world.emplace<HealthComponent>(victim, std::move(health));

    ThrowableBody body;
    body.entity      = victim;
    body.center      = Vector3f{ 0.0f, 2.0f, 2.5f };
    body.halfExtents = Vector3f{ 0.45f, 0.9f, 0.45f };
    body.alive       = true;
    std::vector<ThrowableBody> bodies{ body };

    ThrowableWorld query;
    query.bodies    = bodies.data();
    query.bodyCount = 1;

    ThrowableLaunch launch = makeLaunch(10.0f, 1.0f, 0.0f, true);
    launch.origin          = Vector3f{ 0.0f, 2.0f, 0.0f };
    launch.thrower         = world.createEntity();

    ThrowableFlights flights;
    ASSERT_TRUE(beginThrowableThrow(flights, loadout, belt, launch));
    ThrowableLanding landing;
    ASSERT_TRUE(flyUntilLanding(flights, query, 2.0f, landing));
    EXPECT_TRUE(landing.hitBody);
    EXPECT_FLOAT_EQ(landing.damage, 0.0f);
    EXPECT_FALSE(landing.emittedWeaponHit);
    EXPECT_EQ(hits, 0);
    EXPECT_FLOAT_EQ(world.get<HealthComponent>(victim)->health.hp(), 40.0f);

    WeaponFireRequest req{};
    req.origin    = launch.origin;
    req.direction = launch.direction;
    EXPECT_FALSE(loadout.fire(req, WeaponWorldQuery{}));
    EXPECT_EQ(hits, 0);
    EXPECT_EQ(belt.count("rock"), 1);
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Throwable);
}

TEST(ThrowableFlight, TwoThrowsStayInFlightWhileCountAllows)
{
    WeaponLoadout loadout;
    ThrowableBelt belt;
    ASSERT_GT(belt.add("rock"), 0);
    ASSERT_GT(belt.add("rock"), 0);
    ASSERT_TRUE(belt.setHand("rock"));
    ASSERT_TRUE(loadout.selectSlot(2, belt));

    ThrowableLaunch launch = makeLaunch(10.0f, 1.0f, 0.3f, true);
    launch.maxLife         = 4.0f;
    ThrowableFlights flights;
    ASSERT_TRUE(beginThrowableThrow(flights, loadout, belt, launch));
    launch.origin = Vector3f{ 0.4f, kThrowableRadius, 0.0f };
    ASSERT_TRUE(beginThrowableThrow(flights, loadout, belt, launch));
    EXPECT_FALSE(beginThrowableThrow(flights, loadout, belt, launch));

    flights.tick(0.2f, ThrowableWorld{});
    EXPECT_EQ(flights.liveCount(), 2);
    EXPECT_TRUE(flights.landings().empty());
    EXPECT_EQ(belt.count("rock"), 0);
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Melee);

    Vector3f first{};
    Vector3f second{};
    ASSERT_TRUE(flights.livePosition(0, first));
    ASSERT_TRUE(flights.livePosition(1, second));
    EXPECT_NE(first.x, second.x);
}

TEST(ThrowableFlight, SampleWeightsReachWhenChargedAndStayShortOnATap)
{
    ThrowableCatalog cat;
    ASSERT_TRUE(cat.loadFromContent());
    const ThrowableDef* heavier = nullptr;
    ASSERT_GE(cat.items().size(), 2u);
    for (const ThrowableDef& item : cat.items())
    {
        float pitch = 0.0f;
        EXPECT_TRUE(throwableChargedPitch(item.throwDistance, item.weight, true, pitch)) << item.id;
        const float tapScale = kThrowableTapSpeedScale * kThrowableTapSpeedScale;
        EXPECT_LT(tapScale / item.weight, 1.0f) << item.id;
        if (!heavier || item.weight > heavier->weight)
            heavier = &item;
    }
    ASSERT_NE(heavier, nullptr);
    EXPECT_LT(heavier->weight, 1.0f);
}
