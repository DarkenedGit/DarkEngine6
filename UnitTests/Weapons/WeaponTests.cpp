#include <gtest/gtest.h>

#include "Weapons/WeaponLoadout.h"
#include "Gameplay/ThrowableBelt.h"
#include "Character/HealthComponent.h"
#include "Character/SkillLimits.h"
#include "Character/SkillXp.h"
#include "Collision/HitResult.h"
#include "Combat/DamageEvent.h"
#include "Combat/HitVolumes.h"
#include "Combat/WeaponHitAdapter.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/Capsule3f.h"
#include "Math/MathDefines.h"
#include "Math/Ray3f.h"
#include "Math/Vector3f.h"
#include "Play/WeaponTargets.h"
#include "Weapons/HittableComponent.h"

#include <cmath>
#include <limits>
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

TEST(ProjectileWeapon, RecoilScaleShortensPitchKick)
{
    FakeWorld world;
    ProjectileWeaponDesc d{};
    d.instant        = true;
    d.cooldown       = 0.0f;
    d.recoilPitchDeg = 10.0f;
    d.recoilYawDeg   = 0.0f;
    ProjectileWeapon w{ d };
    const WeaponWorldQuery q = world.query();
    WeaponFireRequest req = aim(Vector3f{ 0, 5, 0 }, Vector3f{ 0, -1, 0 }, Vector3f{ 0, 0.5f, 0 });
    req.recoilScale = kRecoilScaleMin;
    ASSERT_TRUE(w.fire(req, q));
    const RecoilKick scaled = w.takeRecoil();
    EXPECT_NEAR(scaled.pitch, 10.0f * kRecoilScaleMin * DegToRad, 1.0e-5f);
    EXPECT_NEAR(scaled.yaw, 0.0f, 1.0e-6f);
    EXPECT_FLOAT_EQ(w.desc().recoilPitchDeg, 10.0f);
    EXPECT_FLOAT_EQ(w.desc().recoilYawDeg, 0.0f);

    req.recoilScale = kSkillIdentity;
    ASSERT_TRUE(w.fire(req, q));
    const RecoilKick full = w.takeRecoil();
    EXPECT_NEAR(full.pitch, 10.0f * DegToRad, 1.0e-5f);
    EXPECT_FLOAT_EQ(w.desc().recoilPitchDeg, 10.0f);
}

TEST(ProjectileWeapon, RecoilScaleShrinksYawSpread)
{
    FakeWorld world;
    ProjectileWeaponDesc d{};
    d.instant        = true;
    d.cooldown       = 0.0f;
    d.recoilPitchDeg = 0.0f;
    d.recoilYawDeg   = 4.0f;
    ProjectileWeapon w{ d };
    const WeaponWorldQuery q = world.query();
    WeaponFireRequest req = aim(Vector3f{ 0, 5, 0 }, Vector3f{ 0, -1, 0 }, Vector3f{ 0, 0.5f, 0 });
    req.recoilScale = kRecoilScaleMin;
    const float limit = 4.0f * kRecoilScaleMin * DegToRad + 1.0e-4f;
    for (int i = 0; i < 24; ++i)
    {
        ASSERT_TRUE(w.fire(req, q));
        const RecoilKick k = w.takeRecoil();
        EXPECT_GE(k.yaw, -limit);
        EXPECT_LE(k.yaw, limit);
        EXPECT_NEAR(k.pitch, 0.0f, 1.0e-6f);
    }
    EXPECT_FLOAT_EQ(w.desc().recoilYawDeg, 4.0f);
}

TEST(ProjectileWeapon, CooldownScaleShortensWait)
{
    FakeWorld world;
    ProjectileWeaponDesc d{};
    d.instant  = true;
    d.cooldown = 1.0f;
    ProjectileWeapon fullW{ d };
    ProjectileWeapon fastW{ d };
    const WeaponWorldQuery q = world.query();
    WeaponFireRequest req = aim(Vector3f{ 0, 5, 0 }, Vector3f{ 0, -1, 0 }, Vector3f{ 0, 0.5f, 0 });
    ASSERT_TRUE(fullW.fire(req, q));
    req.cooldownScale = kCooldownScaleMin;
    ASSERT_TRUE(fastW.fire(req, q));
    EXPECT_FLOAT_EQ(fullW.desc().cooldown, 1.0f);
    EXPECT_FLOAT_EQ(fastW.desc().cooldown, 1.0f);
    fullW.tick(0.90f, q);
    fastW.tick(0.90f, q);
    EXPECT_FALSE(fullW.canFire());
    EXPECT_TRUE(fastW.canFire());
}

TEST(ProjectileWeapon, ScaleAboveOneClampsIntoRange)
{
    FakeWorld world;
    ProjectileWeaponDesc d{};
    d.instant        = true;
    d.cooldown       = 1.0f;
    d.recoilPitchDeg = 10.0f;
    d.recoilYawDeg   = 0.0f;
    ProjectileWeapon w{ d };
    const WeaponWorldQuery q = world.query();
    WeaponFireRequest req = aim(Vector3f{ 0, 5, 0 }, Vector3f{ 0, -1, 0 }, Vector3f{ 0, 0.5f, 0 });
    req.recoilScale   = 2.0f;
    req.cooldownScale = 2.0f;
    ASSERT_TRUE(w.fire(req, q));
    const RecoilKick k = w.takeRecoil();
    EXPECT_NEAR(k.pitch, 10.0f * DegToRad, 1.0e-5f);
    EXPECT_GT(k.pitch, 10.0f * kRecoilScaleMin * DegToRad + 1.0e-3f);
    EXPECT_FLOAT_EQ(w.desc().recoilPitchDeg, 10.0f);
    EXPECT_FLOAT_EQ(w.desc().cooldown, 1.0f);
    w.tick(0.90f, q);
    EXPECT_FALSE(w.canFire());
    w.tick(0.20f, q);
    EXPECT_TRUE(w.canFire());
}

TEST(ProjectileWeapon, ScaleBelowMinClampsUp)
{
    FakeWorld world;
    ProjectileWeaponDesc d{};
    d.instant        = true;
    d.cooldown       = 1.0f;
    d.recoilPitchDeg = 10.0f;
    d.recoilYawDeg   = 0.0f;
    ProjectileWeapon w{ d };
    const WeaponWorldQuery q = world.query();
    WeaponFireRequest req = aim(Vector3f{ 0, 5, 0 }, Vector3f{ 0, -1, 0 }, Vector3f{ 0, 0.5f, 0 });
    req.recoilScale   = 0.50f;
    req.cooldownScale = 0.50f;
    ASSERT_TRUE(w.fire(req, q));
    const RecoilKick k = w.takeRecoil();
    EXPECT_NEAR(k.pitch, 10.0f * kRecoilScaleMin * DegToRad, 1.0e-5f);
    EXPECT_GT(k.pitch, 10.0f * 0.50f * DegToRad + 1.0e-3f);
    w.tick(0.60f, q);
    EXPECT_FALSE(w.canFire());
    w.tick(0.30f, q);
    EXPECT_TRUE(w.canFire());
    EXPECT_FLOAT_EQ(w.desc().cooldown, 1.0f);
}

TEST(ProjectileWeapon, NonFiniteScaleIsIdentityNotLowCap)
{
    FakeWorld world;
    ProjectileWeaponDesc d{};
    d.instant        = true;
    d.cooldown       = 1.0f;
    d.recoilPitchDeg = 10.0f;
    d.recoilYawDeg   = 0.0f;
    ProjectileWeapon w{ d };
    const WeaponWorldQuery q = world.query();
    WeaponFireRequest req = aim(Vector3f{ 0, 5, 0 }, Vector3f{ 0, -1, 0 }, Vector3f{ 0, 0.5f, 0 });
    const float badScales[] = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    for (float scale : badScales)
    {
        w.clear();
        req.recoilScale   = scale;
        req.cooldownScale = scale;
        ASSERT_TRUE(w.fire(req, q));
        const RecoilKick k = w.takeRecoil();
        EXPECT_NEAR(k.pitch, 10.0f * DegToRad, 1.0e-5f);
        EXPECT_GT(k.pitch, 10.0f * kRecoilScaleMin * DegToRad + 1.0e-3f);
        w.tick(0.90f, q);
        EXPECT_FALSE(w.canFire());
    }
    EXPECT_FLOAT_EQ(w.desc().recoilPitchDeg, 10.0f);
    EXPECT_FLOAT_EQ(w.desc().cooldown, 1.0f);
}

TEST(ProjectileWeapon, SkillScalesDoNotChangeDamage)
{
    FakeWorld world;
    world.targets.push_back(Vector3f{ 0.0f, 1.0f, 10.0f });
    world.alive.push_back(1);
    HitLog log;
    ProjectileWeaponDesc d{};
    d.instant        = true;
    d.damage         = 24.0f;
    d.cooldown       = 0.0f;
    d.recoilPitchDeg = 10.0f;
    ProjectileWeapon w{ d };
    w.setHitListener(&HitLog::onHit, &log);
    WeaponFireRequest req = aim(Vector3f{ 0, 1, 0 }, Vector3f{ 0, 0, 1 }, Vector3f{ 0, 0.5f, 0 });
    req.damageScale   = 1.85f;
    req.recoilScale   = kRecoilScaleMin;
    req.cooldownScale = kCooldownScaleMin;
    ASSERT_TRUE(w.fire(req, world.query()));
    ASSERT_EQ(log.hits.size(), 1u);
    EXPECT_FLOAT_EQ(log.hits[0].damage, 24.0f * 1.85f);
    const RecoilKick k = w.takeRecoil();
    EXPECT_NEAR(k.pitch, 10.0f * kRecoilScaleMin * DegToRad, 1.0e-5f);
}

TEST(MeleeWeapon, IgnoresRecoilAndCooldownScales)
{
    FakeWorld world;
    world.targets.push_back(Vector3f{ 0.0f, 1.0f, 2.0f });
    world.alive.push_back(1);
    HitLog log;
    MeleeWeaponDesc d{};
    d.damage   = 16.0f;
    d.cooldown = 1.0f;
    MeleeWeapon w{ d };
    w.setHitListener(&HitLog::onHit, &log);
    WeaponFireRequest req = aim(Vector3f{ 0, 1, 0 }, Vector3f{ 0, 0, 1 }, Vector3f{ 0, 0.5f, 0 });
    req.damageScale   = 1.85f;
    req.recoilScale   = kRecoilScaleMin;
    req.cooldownScale = kCooldownScaleMin;
    ASSERT_TRUE(w.fire(req, world.query()));
    ASSERT_EQ(log.hits.size(), 1u);
    EXPECT_FLOAT_EQ(log.hits[0].damage, 16.0f * 1.85f);
    EXPECT_FLOAT_EQ(w.desc().cooldown, 1.0f);
    w.tick(0.90f, world.query());
    EXPECT_FALSE(w.canFire());
}

TEST(ProjectileWeapon, ShootLockBlocksGrantAfterSuccessfulFire)
{
    FakeWorld world;
    ProjectileWeaponDesc d{};
    d.instant  = true;
    d.cooldown = 0.0f;
    ProjectileWeapon w{ d };
    const WeaponWorldQuery q = world.query();
    const WeaponFireRequest req = aim(Vector3f{ 0, 5, 0 }, Vector3f{ 0, -1, 0 }, Vector3f{ 0, 0.5f, 0 });
    const SkillDef* shoot = skillCatalog().find(SkillId::Shoot);
    ASSERT_NE(shoot, nullptr);

    SkillComponent locked;
    locked.shootLock = 0.05f;
    ASSERT_TRUE(w.fire(req, q));
    EXPECT_FLOAT_EQ(locked.xp(SkillId::Shoot), 0.0f);
    noteShotXp(locked, true);
    EXPECT_FLOAT_EQ(locked.xp(SkillId::Shoot), 0.0f);
    EXPECT_FLOAT_EQ(locked.shootLock, 0.05f);

    SkillComponent open;
    ASSERT_TRUE(w.fire(req, q));
    noteShotXp(open, false);
    EXPECT_FLOAT_EQ(open.xp(SkillId::Shoot), 0.0f);
    noteShotXp(open, true);
    EXPECT_FLOAT_EQ(open.xp(SkillId::Shoot), shoot->xpPerEvent);
    EXPECT_FLOAT_EQ(open.shootLock, kShootGrantInterval);
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

TEST(WeaponLoadout, SlotTwoIsDistinctKind)
{
    WeaponLoadout loadout;
    EXPECT_TRUE(loadout.selectSlot(2));
    EXPECT_EQ(loadout.slot(), 2);
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Throwable);
    EXPECT_FALSE(loadout.fire(WeaponFireRequest{}, WeaponWorldQuery{}));
    EXPECT_FLOAT_EQ(loadout.projectile().cooldownLeft(), 0.0f);
    for (const LiveProjectile& shot : loadout.projectile().live())
        EXPECT_FALSE(shot.alive);
}

TEST(WeaponLoadout, SlotZeroStaysMeleeAndSlotOneStaysProjectile)
{
    WeaponLoadout loadout;
    ThrowableBelt belt;
    ASSERT_EQ(belt.add("rock"), 1);
    ASSERT_TRUE(loadout.selectSlot(2, belt));
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Throwable);

    EXPECT_TRUE(loadout.selectSlot(0));
    EXPECT_EQ(loadout.slot(), 0);
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Melee);
    EXPECT_STREQ(loadout.active().name(), "Melee");
    EXPECT_TRUE(loadout.fire(WeaponFireRequest{}, WeaponWorldQuery{}));

    EXPECT_TRUE(loadout.selectSlot(1));
    EXPECT_EQ(loadout.slot(), 1);
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Projectile);
    EXPECT_STREQ(loadout.active().name(), "Rifle");
    EXPECT_TRUE(loadout.fire(WeaponFireRequest{}, WeaponWorldQuery{}));
    EXPECT_GT(loadout.projectile().cooldownLeft(), 0.0f);
}

TEST(WeaponLoadout, SelectSlotTwoWithZeroCountsDoesNotStayOnThrowable)
{
    WeaponLoadout loadout;
    ThrowableBelt belt;
    EXPECT_FALSE(loadout.selectSlot(2, belt));
    EXPECT_EQ(loadout.slot(), 0);
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Melee);
    EXPECT_NE(loadout.activeKind(), WeaponKind::Throwable);
}

TEST(WeaponLoadout, Ae14CycleOnlyWhenThrowableAlreadySelected)
{
    WeaponLoadout loadout;
    ThrowableBelt belt;
    ASSERT_EQ(belt.add("rock"), 1);
    ASSERT_EQ(belt.add("bottle"), 1);
    ASSERT_TRUE(belt.setHand("rock"));

    ASSERT_TRUE(loadout.selectSlot(2, belt));
    EXPECT_EQ(loadout.slot(), 2);
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Throwable);
    EXPECT_EQ(belt.hand(), "rock");
    EXPECT_EQ(belt.count("rock"), 1);
    EXPECT_EQ(belt.count("bottle"), 1);

    EXPECT_TRUE(loadout.selectSlot(1));
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Projectile);
    ASSERT_TRUE(loadout.selectSlot(2, belt));
    EXPECT_EQ(belt.hand(), "rock");
    EXPECT_EQ(belt.count("rock"), 1);
    EXPECT_EQ(belt.count("bottle"), 1);

    ASSERT_TRUE(loadout.selectSlot(2, belt));
    EXPECT_EQ(loadout.slot(), 2);
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Throwable);
    EXPECT_EQ(belt.hand(), "bottle");
    EXPECT_EQ(belt.count("rock"), 1);
    EXPECT_EQ(belt.count("bottle"), 1);

    ASSERT_TRUE(loadout.selectSlot(2, belt));
    EXPECT_EQ(belt.hand(), "rock");
    EXPECT_EQ(belt.count("rock"), 1);
    EXPECT_EQ(belt.count("bottle"), 1);
}

TEST(WeaponLoadout, SpendWhileCountRemainsKeepsHand)
{
    WeaponLoadout loadout;
    ThrowableBelt belt;
    ASSERT_EQ(belt.add("rock"), 1);
    ASSERT_EQ(belt.add("rock"), 2);
    ASSERT_EQ(belt.add("bottle"), 1);
    ASSERT_TRUE(belt.setHand("rock"));
    ASSERT_TRUE(loadout.selectSlot(2, belt));

    ASSERT_TRUE(loadout.spendThrowable(belt));
    EXPECT_EQ(loadout.slot(), 2);
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Throwable);
    EXPECT_EQ(belt.hand(), "rock");
    EXPECT_EQ(belt.count("rock"), 1);
    EXPECT_EQ(belt.count("bottle"), 1);
}

TEST(WeaponLoadout, Ae4LastRockLeavesBottleInHand)
{
    WeaponLoadout loadout;
    ThrowableBelt belt;
    ASSERT_EQ(belt.add("rock"), 1);
    ASSERT_EQ(belt.add("bottle"), 1);
    ASSERT_TRUE(belt.setHand("rock"));
    ASSERT_TRUE(loadout.selectSlot(2, belt));
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Throwable);

    ASSERT_TRUE(loadout.spendThrowable(belt));
    EXPECT_EQ(loadout.slot(), 2);
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Throwable);
    EXPECT_EQ(belt.hand(), "bottle");
    EXPECT_EQ(belt.count("rock"), 0);
    EXPECT_EQ(belt.count("bottle"), 1);
}

TEST(WeaponLoadout, Ae5LastThrowableReturnsToSword)
{
    WeaponLoadout loadout;
    ThrowableBelt belt;
    ASSERT_EQ(belt.add("rock"), 1);
    ASSERT_TRUE(belt.setHand("rock"));
    ASSERT_TRUE(loadout.selectSlot(2, belt));

    ASSERT_TRUE(loadout.spendThrowable(belt));
    EXPECT_EQ(loadout.slot(), 0);
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Melee);
    EXPECT_TRUE(belt.hand().empty());
    EXPECT_EQ(belt.count("rock"), 0);

    EXPECT_FALSE(loadout.selectSlot(2, belt));
    EXPECT_EQ(loadout.slot(), 0);
    EXPECT_NE(loadout.activeKind(), WeaponKind::Throwable);

    ASSERT_EQ(belt.add("rock"), 1);
    EXPECT_EQ(loadout.slot(), 0);
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Melee);
    EXPECT_TRUE(belt.hand().empty());

    ASSERT_TRUE(loadout.selectSlot(2, belt));
    EXPECT_EQ(loadout.slot(), 2);
    EXPECT_EQ(loadout.activeKind(), WeaponKind::Throwable);
    EXPECT_EQ(belt.hand(), "rock");
}

namespace
{
    using Dark::Combat::DamageEvent;
    using Dark::Combat::HitPart;
    using Dark::Combat::HitSegment;
    using Dark::Combat::HitVolumesComponent;
    using Dark::Combat::damageEventFromWeaponHit;
    using Dark::Combat::hitPartIndex;

    HitSegment spherePart(HitPart part, const Vector3f& at, float radius)
    {
        HitSegment segment;
        segment.part   = part;
        segment.a      = at;
        segment.b      = at;
        segment.radius = radius;
        segment.active = true;
        return segment;
    }

    void expectOnCapsule(const Vector3f& point, const HitSegment& segment)
    {
        const Capsule3f cap(segment.a, segment.b, segment.radius);
        const float     radial = (point - cap.ClosestPointOnSegment(point)).Magnitude();
        EXPECT_NEAR(radial, segment.radius, 1.5e-3f);
    }

    void expectNormalToward(const Vector3f& normal, const Vector3f& point, const Vector3f& from)
    {
        Vector3f want = from - point;
        ASSERT_GT(want.MagnitudeSqrd(), 1.0e-8f);
        want.Normalize();
        EXPECT_NEAR(normal.x, want.x, 2.0e-3f);
        EXPECT_NEAR(normal.y, want.y, 2.0e-3f);
        EXPECT_NEAR(normal.z, want.z, 2.0e-3f);
    }

    struct PartBody
    {
        Vector3f              center{};
        Vector3f              half{ 1.0f, 1.0f, 1.0f };
        bool                  alive        = true;
        bool                  listCapsules = false;
        std::vector<HitSegment> segments;
    };

    struct PartWorld
    {
        std::vector<PartBody> bodies;
        float                 groundY = -1000.0f;

        static int count(void* user)
        {
            auto* w = static_cast<PartWorld*>(user);
            return w ? static_cast<int>(w->bodies.size()) : 0;
        }

        static bool aliveAt(void* user, int index)
        {
            auto* w = static_cast<PartWorld*>(user);
            if (!w || index < 0 || index >= static_cast<int>(w->bodies.size()))
                return false;
            return w->bodies[static_cast<size_t>(index)].alive;
        }

        static Vector3f center(void* user, int index)
        {
            auto* w = static_cast<PartWorld*>(user);
            if (!w || index < 0 || index >= static_cast<int>(w->bodies.size()))
                return Vector3f{ 0.0f, 0.0f, 0.0f };
            return w->bodies[static_cast<size_t>(index)].center;
        }

        static Vector3f halfAt(void* user, int index)
        {
            auto* w = static_cast<PartWorld*>(user);
            if (!w || index < 0 || index >= static_cast<int>(w->bodies.size()))
                return Vector3f{ 1.0f, 1.0f, 1.0f };
            return w->bodies[static_cast<size_t>(index)].half;
        }

        static const HitSegment* capsules(void* user, int index, int* outCount)
        {
            if (outCount)
                *outCount = 0;
            auto* w = static_cast<PartWorld*>(user);
            if (!w || index < 0 || index >= static_cast<int>(w->bodies.size()))
                return nullptr;
            const PartBody& body = w->bodies[static_cast<size_t>(index)];
            if (!body.listCapsules || body.segments.empty())
                return nullptr;
            if (outCount)
                *outCount = static_cast<int>(body.segments.size());
            return body.segments.data();
        }

        WeaponWorldQuery query()
        {
            WeaponWorldQuery q{};
            q.targetCount         = count;
            q.targetAlive         = aliveAt;
            q.targetCenter        = center;
            q.targetHalfExtentsAt = halfAt;
            q.targetCapsules      = capsules;
            q.targetUser          = this;
            q.maxRange            = 80.0f;
            q.heightAt            = [](void* user, float, float) {
                auto* w = static_cast<PartWorld*>(user);
                return w ? w->groundY : 0.0f;
            };
            q.terrainUser = this;
            return q;
        }
    };
} // namespace

TEST(DamageEvent, PartDefaultsNoneForWholeBodyEvents)
{
    const DamageEvent grind{};
    EXPECT_EQ(grind.part, HitPart::None);
    DamageEvent dot{};
    dot.flags  = Dark::Combat::DamageFlags::DotTick;
    dot.amount = 3.0f;
    EXPECT_EQ(dot.part, HitPart::None);

    WeaponHit hit{};
    EXPECT_EQ(hit.part, HitPart::None);
    const Entity source{ makeEntityID(1, 1) };
    EXPECT_EQ(damageEventFromWeaponHit(hit, source, nullptr).part, HitPart::None);
    hit.part = HitPart::LeftLeg;
    Dark::Combat::AttackDef attack{};
    attack.damage = 9.0f;
    const DamageEvent copied = damageEventFromWeaponHit(hit, source, &attack);
    EXPECT_EQ(copied.part, HitPart::LeftLeg);
    EXPECT_FLOAT_EQ(copied.amount, 9.0f);
    EXPECT_EQ(copied.hitDir, hit.direction);
}

TEST(WeaponRay, HitscanPicksHeadNotSideChest)
{
    PartWorld world;
    PartBody  body;
    body.center       = Vector3f{ 0.0f, 1.5f, 3.0f };
    body.half         = Vector3f{ 1.0f, 1.0f, 1.0f };
    body.listCapsules = true;
    body.segments.push_back(spherePart(HitPart::Chest, Vector3f{ 0.9f, 1.5f, 3.0f }, 0.20f));
    body.segments.push_back(spherePart(HitPart::Head, Vector3f{ 0.0f, 1.7f, 3.0f }, 0.15f));
    world.bodies.push_back(body);

    WeaponHit hit{};
    const Vector3f origin{ 0.0f, 1.7f, 0.0f };
    ASSERT_TRUE(weaponRaycastClosest(world.query(), Ray3f{ origin, Vector3f{ 0.0f, 0.0f, 1.0f } }, 40.0f, hit));
    EXPECT_EQ(hit.part, HitPart::Head);
    EXPECT_EQ(hit.targetIndex, 0);
    EXPECT_TRUE(hit.hitTarget);
    expectOnCapsule(hit.point, world.bodies[0].segments[1]);
    expectNormalToward(hit.normal, hit.point, origin);
}

TEST(WeaponRay, TieWithinMillimeterUsesPartOrder)
{
    PartWorld world;
    PartBody  body;
    body.center       = Vector3f{ 0.0f, 1.0f, 5.0f };
    body.half         = Vector3f{ 0.2f, 0.2f, 0.2f };
    body.listCapsules = true;
    body.segments.push_back(spherePart(HitPart::Chest, Vector3f{ 0.0f, 1.0f, 5.0f }, 0.50f));
    body.segments.push_back(spherePart(HitPart::Head, Vector3f{ 0.0f, 1.0f, 5.0004f }, 0.50f));
    world.bodies.push_back(body);

    WeaponHit hit{};
    ASSERT_TRUE(weaponRaycastClosest(world.query(), Ray3f{ Vector3f{ 0.0f, 1.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f } }, 40.0f, hit));
    EXPECT_EQ(hit.part, HitPart::Head);
    expectOnCapsule(hit.point, world.bodies[0].segments[1]);
}

TEST(WeaponRay, MissesActiveCapsulesAndHitsLaterBox)
{
    PartWorld world;
    PartBody  human;
    human.center       = Vector3f{ 0.0f, 1.0f, 3.0f };
    human.half         = Vector3f{ 1.0f, 1.0f, 1.0f };
    human.listCapsules = true;
    human.segments.push_back(spherePart(HitPart::Chest, Vector3f{ 2.0f, 1.0f, 3.0f }, 0.20f));
    world.bodies.push_back(human);

    PartBody wolf;
    wolf.center = Vector3f{ 0.0f, 1.0f, 8.0f };
    wolf.half   = Vector3f{ 0.4f, 1.0f, 0.4f };
    world.bodies.push_back(wolf);

    WeaponHit hit{};
    ASSERT_TRUE(weaponRaycastClosest(world.query(), Ray3f{ Vector3f{ 0.0f, 1.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f } }, 40.0f, hit));
    EXPECT_EQ(hit.targetIndex, 1);
    EXPECT_EQ(hit.part, HitPart::None);
    EXPECT_TRUE(hit.hitTarget);
    EXPECT_NEAR(hit.point.z, 7.6f, 1.0e-3f);
}

TEST(ProjectileWeapon, RifleBoxHitReportsNoPart)
{
    PartWorld world;
    PartBody  wolf;
    wolf.center = Vector3f{ 0.0f, 1.0f, 6.0f };
    wolf.half   = Vector3f{ 0.5f, 1.0f, 0.5f };
    world.bodies.push_back(wolf);

    HitLog log;
    ProjectileWeaponDesc d{};
    d.instant        = true;
    d.cooldown       = 0.0f;
    d.recoilPitchDeg = 0.0f;
    d.recoilYawDeg   = 0.0f;
    ProjectileWeapon w{ d };
    w.setHitListener(&HitLog::onHit, &log);
    ASSERT_TRUE(w.fire(aim(Vector3f{ 0, 1, 0 }, Vector3f{ 0, 0, 1 }, Vector3f{ 0, 0.5f, 0 }), world.query()));
    ASSERT_EQ(log.hits.size(), 1u);
    EXPECT_TRUE(log.hits[0].hitTarget);
    EXPECT_EQ(log.hits[0].part, HitPart::None);
    EXPECT_NEAR(log.hits[0].point.z, 5.5f, 1.0e-3f);
    EXPECT_NEAR(log.hits[0].direction.z, 1.0f, 1.0e-4f);
}

TEST(WeaponSweep, CapsuleBeatsBroadBox)
{
    PartWorld world;
    PartBody  body;
    body.center       = Vector3f{ 0.0f, 1.7f, 3.0f };
    body.half         = Vector3f{ 1.0f, 1.0f, 1.0f };
    body.listCapsules = true;
    body.segments.push_back(spherePart(HitPart::Chest, Vector3f{ 1.2f, 1.7f, 3.0f }, 0.20f));
    body.segments.push_back(spherePart(HitPart::Head, Vector3f{ 0.0f, 1.7f, 2.5f }, 0.15f));
    world.bodies.push_back(body);

    WeaponHit hit{};
    const Vector3f start{ 0.0f, 1.7f, 0.0f };
    ASSERT_TRUE(weaponSweepClosest(world.query(), start, Vector3f{ 0.0f, 0.0f, 4.0f }, 0.05f, hit));
    EXPECT_TRUE(hit.hitTarget);
    EXPECT_EQ(hit.part, HitPart::Head);
    expectOnCapsule(hit.point, world.bodies[0].segments[1]);
}

TEST(WeaponSweep, MissesCapsulesAndHitsLaterBox)
{
    PartWorld world;
    PartBody  human;
    human.center       = Vector3f{ 0.0f, 1.0f, 3.0f };
    human.half         = Vector3f{ 1.0f, 1.0f, 1.0f };
    human.listCapsules = true;
    human.segments.push_back(spherePart(HitPart::Chest, Vector3f{ 2.0f, 1.0f, 3.0f }, 0.20f));
    world.bodies.push_back(human);

    PartBody wolf;
    wolf.center = Vector3f{ 0.0f, 1.0f, 8.0f };
    wolf.half   = Vector3f{ 0.4f, 1.0f, 0.4f };
    world.bodies.push_back(wolf);

    WeaponHit hit{};
    ASSERT_TRUE(weaponSweepClosest(world.query(), Vector3f{ 0.0f, 1.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 10.0f }, 0.05f, hit));
    EXPECT_EQ(hit.targetIndex, 1);
    EXPECT_EQ(hit.part, HitPart::None);
    EXPECT_TRUE(hit.hitTarget);
    EXPECT_GT(hit.point.z, 4.0f);
}

TEST(MeleeWeapon, NullCapsuleListKeepsCenterAndNoPart)
{
    PartWorld world;
    PartBody  body;
    body.center = Vector3f{ 0.0f, 1.0f, 2.0f };
    world.bodies.push_back(body);

    HitLog log;
    MeleeWeapon w;
    w.setHitListener(&HitLog::onHit, &log);
    ASSERT_TRUE(w.fire(aim(Vector3f{ 0, 1, 0 }, Vector3f{ 0, 0, 1 }, Vector3f{ 0, 0.5f, 0 }), world.query()));
    ASSERT_EQ(log.hits.size(), 1u);
    EXPECT_EQ(log.hits[0].part, HitPart::None);
    EXPECT_NEAR(log.hits[0].point.x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(log.hits[0].point.y, 1.0f, 1.0e-4f);
    EXPECT_NEAR(log.hits[0].point.z, 2.0f, 1.0e-4f);
    EXPECT_NEAR(log.hits[0].direction.z, 1.0f, 1.0e-4f);
}

TEST(MeleeWeapon, InactiveCapsulesKeepCenter)
{
    PartWorld world;
    PartBody  body;
    body.center       = Vector3f{ 0.0f, 1.0f, 2.0f };
    body.listCapsules = true;
    HitSegment head   = spherePart(HitPart::Head, Vector3f{ 0.0f, 1.0f, 0.4f }, 0.20f);
    head.active       = false;
    body.segments.push_back(head);
    world.bodies.push_back(body);

    HitLog log;
    MeleeWeapon w;
    w.setHitListener(&HitLog::onHit, &log);
    ASSERT_TRUE(w.fire(aim(Vector3f{ 0, 1, 0 }, Vector3f{ 0, 0, 1 }, Vector3f{ 0, 0.5f, 0 }), world.query()));
    ASSERT_EQ(log.hits.size(), 1u);
    EXPECT_EQ(log.hits[0].part, HitPart::None);
    EXPECT_NEAR(log.hits[0].point.z, 2.0f, 1.0e-4f);
}

TEST(MeleeWeapon, RightArmInReachReportsSurfacePoint)
{
    PartWorld world;
    PartBody  body;
    body.center       = Vector3f{ 0.0f, 1.0f, 2.0f };
    body.listCapsules = true;
    HitSegment ignored = spherePart(HitPart::Head, Vector3f{ 0.0f, 1.0f, 0.35f }, 0.20f);
    ignored.active     = false;
    body.segments.push_back(ignored);
    body.segments.push_back(spherePart(HitPart::Chest, Vector3f{ 0.0f, 1.3f, 2.2f }, 0.14f));
    body.segments.push_back(spherePart(HitPart::RightArm, Vector3f{ 0.40f, 1.05f, 1.05f }, 0.08f));
    world.bodies.push_back(body);

    const Vector3f owner{ 0.0f, 0.5f, 0.0f };
    HitLog log;
    MeleeWeapon w;
    w.setHitListener(&HitLog::onHit, &log);
    ASSERT_TRUE(w.fire(aim(owner, Vector3f{ 0, 0, 1 }, owner), world.query()));
    ASSERT_EQ(log.hits.size(), 1u);
    EXPECT_EQ(log.hits[0].part, HitPart::RightArm);
    EXPECT_NEAR(log.hits[0].direction.z, 1.0f, 1.0e-4f);
    expectOnCapsule(log.hits[0].point, world.bodies[0].segments[2]);
    expectNormalToward(log.hits[0].normal, log.hits[0].point, owner);
}

TEST(MeleeWeapon, CenterInsideConeMissesWhenCapsulesAreOutsideReach)
{
    PartWorld world;
    PartBody  body;
    body.center       = Vector3f{ 0.0f, 1.0f, 2.0f };
    body.listCapsules = true;
    body.segments.push_back(spherePart(HitPart::Chest, Vector3f{ 0.0f, 1.0f, 5.0f }, 0.10f));
    world.bodies.push_back(body);

    HitLog log;
    MeleeWeapon w;
    w.setHitListener(&HitLog::onHit, &log);
    ASSERT_TRUE(w.fire(aim(Vector3f{ 0, 1, 0 }, Vector3f{ 0, 0, 1 }, Vector3f{ 0, 0.5f, 0 }), world.query()));
    EXPECT_TRUE(log.hits.empty());
}

TEST(WeaponTargetList, RayUsesGatheredCapsules)
{
    World  world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = Vector3f{ 0.0f, 1.7f, 3.0f };
    world.emplace<TransformComponent>(e, xf);
    HittableComponent box{};
    box.halfExtents = Vector3f{ 1.0f, 1.0f, 1.0f };
    world.emplace<HittableComponent>(e, box);
    world.emplace<HealthComponent>(e);

    HitVolumesComponent volumes;
    volumes.segment[hitPartIndex(HitPart::Chest)] = spherePart(HitPart::Chest, Vector3f{ 0.9f, 1.5f, 3.0f }, 0.20f);
    volumes.segment[hitPartIndex(HitPart::Head)]  = spherePart(HitPart::Head, Vector3f{ 0.0f, 1.7f, 3.0f }, 0.15f);
    world.emplace<HitVolumesComponent>(e, volumes);

    WeaponTargetList list;
    list.gather(world, {});
    WeaponWorldQuery query;
    list.bind(query);

    WeaponHit hit{};
    const Vector3f origin{ 0.0f, 1.7f, 0.0f };
    ASSERT_TRUE(weaponRaycastClosest(query, Ray3f{ origin, Vector3f{ 0.0f, 0.0f, 1.0f } }, 40.0f, hit));
    EXPECT_EQ(hit.part, HitPart::Head);
    EXPECT_EQ(hit.targetEntity, e);
    expectOnCapsule(hit.point, volumes.segment[hitPartIndex(HitPart::Head)]);
}
