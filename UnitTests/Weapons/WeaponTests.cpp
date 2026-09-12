#include <gtest/gtest.h>

#include "Weapons/WeaponLoadout.h"
#include "Collision/HitResult.h"
#include "Math/MathDefines.h"
#include "Math/Ray3f.h"
#include "Math/Vector3f.h"

#include <vector>

using namespace Dark;
using namespace Dark::Math;

namespace
{
    struct FakeWorld
    {
        std::vector<Vector3f> targets;
        std::vector<char>     alive;
        float                 groundY = 0.0f;

        static Collision::RayHit3D raycast(void* user, const Ray3f& ray, float maxDistance)
        {
            auto* w = static_cast<FakeWorld*>(user);
            Collision::RayHit3D hit{};
            if (!w || ray.Direction.y >= -1.0e-6f)
                return hit;
            const float t = (w->groundY - ray.Origin.y) / ray.Direction.y;
            if (t < 0.0f || t > maxDistance)
                return hit;
            hit.hit    = true;
            hit.t      = t;
            hit.point  = ray.PointAt(t);
            hit.normal = Vector3f{ 0.0f, 1.0f, 0.0f };
            return hit;
        }

        static float heightAt(void* user, float, float)
        {
            auto* w = static_cast<FakeWorld*>(user);
            return w ? w->groundY : 0.0f;
        }

        static int count(void* user)
        {
            auto* w = static_cast<FakeWorld*>(user);
            return w ? static_cast<int>(w->targets.size()) : 0;
        }

        static bool aliveAt(void* user, int i)
        {
            auto* w = static_cast<FakeWorld*>(user);
            if (!w || i < 0 || i >= static_cast<int>(w->alive.size()))
                return false;
            return w->alive[static_cast<size_t>(i)] != 0;
        }

        static Vector3f center(void* user, int i)
        {
            auto* w = static_cast<FakeWorld*>(user);
            if (!w || i < 0 || i >= static_cast<int>(w->targets.size()))
                return Vector3f{ 0.0f, 0.0f, 0.0f };
            return w->targets[static_cast<size_t>(i)];
        }

        WeaponWorldQuery query()
        {
            WeaponWorldQuery q{};
            q.raycastTerrain     = raycast;
            q.heightAt           = heightAt;
            q.terrainUser        = this;
            q.targetCount        = count;
            q.targetAlive        = aliveAt;
            q.targetCenter       = center;
            q.targetHalfExtents  = Vector3f{ 1.0f, 1.0f, 1.0f };
            q.targetUser         = this;
            q.maxRange           = 80.0f;
            return q;
        }
    };

    struct HitLog
    {
        std::vector<WeaponHit> hits;
        static void onHit(void* user, const WeaponHit& hit)
        {
            static_cast<HitLog*>(user)->hits.push_back(hit);
        }
    };

    WeaponFireRequest aim(const Vector3f& origin, const Vector3f& dir, const Vector3f& owner)
    {
        WeaponFireRequest r{};
        r.origin    = origin;
        r.direction = dir;
        r.ownerPos  = owner;
        return r;
    }
} // namespace

TEST(MeleeWeapon, HitsTargetInFrontAndReportsDamage)
{
    FakeWorld world;
    world.targets.push_back(Vector3f{ 0.0f, 1.0f, 2.0f });
    world.alive.push_back(1);
    HitLog log;
    MeleeWeapon w;
    w.setHitListener(&HitLog::onHit, &log);
    EXPECT_TRUE(w.fire(aim(Vector3f{ 0, 1, 0 }, Vector3f{ 0, 0, 1 }, Vector3f{ 0, 0.5f, 0 }), world.query()));
    ASSERT_EQ(log.hits.size(), 1u);
    EXPECT_TRUE(log.hits[0].hitTarget);
    EXPECT_EQ(log.hits[0].targetIndex, 0);
    EXPECT_FLOAT_EQ(log.hits[0].damage, 16.0f);
    EXPECT_NEAR(log.hits[0].direction.z, 1.0f, 1.0e-4f);
    EXPECT_FALSE(w.canFire());
}

TEST(MeleeWeapon, MissesTargetOutOfRangeAndBehind)
{
    FakeWorld world;
    world.targets.push_back(Vector3f{ 0.0f, 1.0f, 8.0f });
    world.targets.push_back(Vector3f{ 0.0f, 1.0f, -1.5f });
    world.alive.push_back(1);
    world.alive.push_back(1);
    HitLog log;
    MeleeWeapon w;
    w.setHitListener(&HitLog::onHit, &log);
    EXPECT_TRUE(w.fire(aim(Vector3f{ 0, 1, 0 }, Vector3f{ 0, 0, 1 }, Vector3f{ 0, 0.5f, 0 }), world.query()));
    EXPECT_TRUE(log.hits.empty());
}

TEST(ProjectileWeapon, InstantHitsTargetAlongAim)
{
    FakeWorld world;
    world.targets.push_back(Vector3f{ 0.0f, 1.0f, 10.0f });
    world.alive.push_back(1);
    HitLog log;
    ProjectileWeaponDesc d{};
    d.instant  = true;
    d.damage   = 24.0f;
    d.cooldown = 0.0f;
    ProjectileWeapon w{ d };
    w.setHitListener(&HitLog::onHit, &log);
    EXPECT_TRUE(w.fire(aim(Vector3f{ 0, 1, 0 }, Vector3f{ 0, 0, 1 }, Vector3f{ 0, 0.5f, 0 }), world.query()));
    ASSERT_EQ(log.hits.size(), 1u);
    EXPECT_TRUE(log.hits[0].hitTarget);
    EXPECT_FLOAT_EQ(log.hits[0].damage, 24.0f);
    EXPECT_EQ(log.hits[0].weapon, WeaponKind::Projectile);
    EXPECT_NEAR(log.hits[0].direction.z, 1.0f, 1.0e-4f);
}

TEST(ProjectileWeapon, InstantHitsGroundWhenNoTarget)
{
    FakeWorld world;
    world.groundY = 0.0f;
    HitLog log;
    ProjectileWeaponDesc d{};
    d.instant  = true;
    d.cooldown = 0.0f;
    ProjectileWeapon w{ d };
    w.setHitListener(&HitLog::onHit, &log);
    EXPECT_TRUE(w.fire(aim(Vector3f{ 0, 5, 0 }, Vector3f{ 0, -1, 0 }, Vector3f{ 0, 0.5f, 0 }), world.query()));
    ASSERT_EQ(log.hits.size(), 1u);
    EXPECT_FALSE(log.hits[0].hitTarget);
    EXPECT_NEAR(log.hits[0].point.y, 0.0f, 1.0e-3f);
}

TEST(ProjectileWeapon, ZeroSpeedIsHitscan)
{
    FakeWorld world;
    world.targets.push_back(Vector3f{ 0.0f, 1.0f, 6.0f });
    world.alive.push_back(1);
    HitLog log;
    ProjectileWeaponDesc d{};
    d.instant  = false;
    d.speed    = 0.0f;
    d.cooldown = 0.0f;
    ProjectileWeapon w{ d };
    w.setHitListener(&HitLog::onHit, &log);
    EXPECT_TRUE(w.isInstant());
    EXPECT_TRUE(w.fire(aim(Vector3f{ 0, 1, 0 }, Vector3f{ 0, 0, 1 }, Vector3f{ 0, 0.5f, 0 }), world.query()));
    ASSERT_EQ(log.hits.size(), 1u);
    EXPECT_TRUE(log.hits[0].hitTarget);
}

TEST(ProjectileWeapon, TravelsOverTimeAndHitsTarget)
{
    FakeWorld world;
    world.targets.push_back(Vector3f{ 0.0f, 1.0f, 10.0f });
    world.alive.push_back(1);
    HitLog log;
    ProjectileWeaponDesc d{};
    d.instant  = false;
    d.speed    = 20.0f;
    d.gravity  = 0.0f;
    d.cooldown = 0.0f;
    d.maxRange = 40.0f;
    ProjectileWeapon w{ d };
    w.setHitListener(&HitLog::onHit, &log);
    const WeaponWorldQuery q = world.query();
    EXPECT_TRUE(w.fire(aim(Vector3f{ 0, 1, 0 }, Vector3f{ 0, 0, 1 }, Vector3f{ 0, 0.5f, 0 }), q));
    EXPECT_TRUE(log.hits.empty());
    int alive = 0;
    for (const LiveProjectile& s : w.live())
        alive += s.alive ? 1 : 0;
    EXPECT_EQ(alive, 1);
    for (int i = 0; i < 40; ++i)
        w.tick(0.05f, q);
    ASSERT_EQ(log.hits.size(), 1u);
    EXPECT_TRUE(log.hits[0].hitTarget);
    EXPECT_NEAR(log.hits[0].point.z, 9.0f, 1.5f);
}

TEST(ProjectileWeapon, GravityDropsFlightPath)
{
    FakeWorld world;
    HitLog    logFlat;
    HitLog    logDrop;
    ProjectileWeaponDesc flat{};
    flat.instant  = false;
    flat.speed    = 20.0f;
    flat.gravity  = 0.0f;
    flat.cooldown = 0.0f;
    flat.maxLife  = 2.0f;
    flat.maxRange = 80.0f;
    ProjectileWeaponDesc drop = flat;
    drop.gravity              = 20.0f;
    ProjectileWeapon a{ flat };
    ProjectileWeapon b{ drop };
    a.setHitListener(&HitLog::onHit, &logFlat);
    b.setHitListener(&HitLog::onHit, &logDrop);
    const WeaponWorldQuery q = world.query();
    const auto             req = aim(Vector3f{ 0, 8, 0 }, Vector3f{ 0, 0, 1 }, Vector3f{ 0, 0.5f, 0 });
    EXPECT_TRUE(a.fire(req, q));
    EXPECT_TRUE(b.fire(req, q));
    a.tick(0.4f, q);
    b.tick(0.4f, q);
    float ya = 0.0f, yb = 0.0f;
    for (const LiveProjectile& s : a.live())
        if (s.alive)
            ya = s.position.y;
    for (const LiveProjectile& s : b.live())
        if (s.alive)
            yb = s.position.y;
    EXPECT_GT(ya, yb + 1.0f);
}

TEST(ProjectileWeapon, CooldownBlocksSecondShot)
{
    FakeWorld world;
    ProjectileWeaponDesc d{};
    d.instant  = true;
    d.cooldown = 0.5f;
    ProjectileWeapon w{ d };
    const WeaponWorldQuery q = world.query();
    const auto             req = aim(Vector3f{ 0, 5, 0 }, Vector3f{ 0, -1, 0 }, Vector3f{ 0, 0.5f, 0 });
    EXPECT_TRUE(w.fire(req, q));
    EXPECT_FALSE(w.fire(req, q));
    w.tick(0.6f, q);
    EXPECT_TRUE(w.canFire());
}

TEST(ProjectileWeapon, ZeroRecoilProducesNoKick)
{
    FakeWorld world;
    ProjectileWeaponDesc d{};
    d.instant         = true;
    d.cooldown        = 0.0f;
    d.recoilPitchDeg  = 0.0f;
    d.recoilYawDeg    = 0.0f;
    ProjectileWeapon w{ d };
    EXPECT_TRUE(w.fire(aim(Vector3f{ 0, 5, 0 }, Vector3f{ 0, -1, 0 }, Vector3f{ 0, 0.5f, 0 }), world.query()));
    const RecoilKick k = w.takeRecoil();
    EXPECT_NEAR(k.pitch, 0.0f, 1.0e-6f);
    EXPECT_NEAR(k.yaw, 0.0f, 1.0e-6f);
}

TEST(ProjectileWeapon, RecoilPitchKicksUpAfterShot)
{
    FakeWorld world;
    ProjectileWeaponDesc d{};
    d.instant         = true;
    d.cooldown        = 0.0f;
    d.recoilPitchDeg  = 10.0f;
    d.recoilYawDeg    = 0.0f;
    ProjectileWeapon w{ d };
    EXPECT_TRUE(w.fire(aim(Vector3f{ 0, 5, 0 }, Vector3f{ 0, -1, 0 }, Vector3f{ 0, 0.5f, 0 }), world.query()));
    const RecoilKick k = w.takeRecoil();
    EXPECT_NEAR(k.pitch, 10.0f * DegToRad, 1.0e-5f);
    EXPECT_NEAR(k.yaw, 0.0f, 1.0e-6f);
    const RecoilKick empty = w.takeRecoil();
    EXPECT_NEAR(empty.pitch, 0.0f, 1.0e-6f);
}

TEST(ProjectileWeapon, RecoilYawStaysWithinConfiguredSpread)
{
    FakeWorld world;
    ProjectileWeaponDesc d{};
    d.instant         = true;
    d.cooldown        = 0.0f;
    d.recoilPitchDeg  = 0.0f;
    d.recoilYawDeg    = 4.0f;
    ProjectileWeapon w{ d };
    const float limit = 4.0f * DegToRad + 1.0e-4f;
    for (int i = 0; i < 24; ++i)
    {
        ASSERT_TRUE(w.fire(aim(Vector3f{ 0, 5, 0 }, Vector3f{ 0, -1, 0 }, Vector3f{ 0, 0.5f, 0 }), world.query()));
        const RecoilKick k = w.takeRecoil();
        EXPECT_GE(k.yaw, -limit);
        EXPECT_LE(k.yaw, limit);
        EXPECT_NEAR(k.pitch, 0.0f, 1.0e-6f);
    }
}

TEST(ProjectileWeapon, FailedFireDoesNotKick)
{
    FakeWorld world;
    ProjectileWeaponDesc d{};
    d.instant         = true;
    d.cooldown        = 1.0f;
    d.recoilPitchDeg  = 8.0f;
    ProjectileWeapon w{ d };
    const auto req = aim(Vector3f{ 0, 5, 0 }, Vector3f{ 0, -1, 0 }, Vector3f{ 0, 0.5f, 0 });
    ASSERT_TRUE(w.fire(req, world.query()));
    (void)w.takeRecoil();
    EXPECT_FALSE(w.fire(req, world.query()));
    const RecoilKick k = w.takeRecoil();
    EXPECT_NEAR(k.pitch, 0.0f, 1.0e-6f);
    EXPECT_NEAR(k.yaw, 0.0f, 1.0e-6f);
}

TEST(WeaponLoadout, KeysOneAndTwoSelectMeleeThenProjectile)
{
    WeaponLoadout loadout;
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Melee);
    EXPECT_TRUE(loadout.selectProjectile());
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Projectile);
    EXPECT_EQ(loadout.slot(), 1);
    EXPECT_FALSE(loadout.selectSlot(1));
    EXPECT_TRUE(loadout.selectMelee());
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Melee);
    EXPECT_STREQ(loadout.active().name(), "Melee");
    loadout.selectProjectile();
    EXPECT_STREQ(loadout.active().name(), "Rifle");
}
