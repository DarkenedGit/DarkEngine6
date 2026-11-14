#include <gtest/gtest.h>

#include <cmath>

#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Physics/PhysicsBind.h"
#include "Physics/PhysicsComponent.h"
#include "Physics/PhysicsWorld.h"
#include "Character/PlayerMotor.h"
#include "Character/SkillComponent.h"
#include "Combat/JumpAttack.h"
#include "Combat/DefenseComponent.h"
#include "Combat/StatusEffectComponent.h"
#include "Play/CharacterAttach.h"
#include "Play/ContactGrind.h"
#include "Play/HunterTick.h"
#include "Play/PhysicsGround.h"
#include "Play/PlayLook.h"
#include "Collision/StaticCollision.h"
#include "Play/PlayMotor.h"
#include "Play/ShoulderCamera.h"
#include "Play/WeaponFire.h"
#include "Play/WeaponTargets.h"
#include "Terrain/HeightMap.h"
#include "Terrain/TerrainGround.h"
#include "Weapons/HittableComponent.h"
#include "Weapons/Weapon.h"

using namespace Dark;
using namespace Dark::Math;

TEST(HunterTick, PreyGroundAndBakeObstacles)
{
    AiSystem ai;
    PlayerStealthSettings stealth;
    applyPreyFromMotor(ai, stealth, nullptr, true);
    EXPECT_FLOAT_EQ(ai.preySense().hearRange, stealth.hearStand);
    EXPECT_FLOAT_EQ(ai.preySense().sightRangeScale, 1.0f);

    PlayerMotor motor;
    motor.setHorizontalVelocity(2.0f, 0.0f);
    applyPreyFromMotor(ai, stealth, &motor, true);
    EXPECT_FLOAT_EQ(ai.preySense().hearRange, stealth.hearSprint);
    EXPECT_FLOAT_EQ(ai.preySense().targetHeight, stealth.standTargetHeight);

    motor.restoreProgress(PlayerMoveState::Crouch, Vector3f{ 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f, false, false, 0.0f, Vector3f{ 0.0f, 0.0f, 0.0f });
    applyPreyFromMotor(ai, stealth, &motor, true);
    EXPECT_FLOAT_EQ(ai.preySense().hearRange, stealth.hearCrouchStill);
    EXPECT_FLOAT_EQ(ai.preySense().sightRangeScale, stealth.sightCrouchStill);
    EXPECT_FLOAT_EQ(ai.preySense().standoff, stealth.standoffCrouchStill);

    const HunterGroundView empty = hunterGroundView(nullptr);
    EXPECT_EQ(empty.height, nullptr);
    EXPECT_EQ(empty.splat, nullptr);

    Terrain::TerrainGrid grid;
    const HunterGroundView fallback = hunterGroundView(&grid);
    EXPECT_EQ(fallback.height, &grid.coarse());
    EXPECT_EQ(fallback.splat, nullptr);

    AI::WalkabilityDesc desc;
    const AABox3f cube = AABox3f::FromCenterExtents(Vector3f{ 0.0f, 0.0f, 0.0f }, Vector3f{ 0.5f, 0.5f, 0.5f });
    const Sphere3f sphere{ Vector3f{ 2.0f, 0.0f, 0.0f }, 0.4f };
    setHunterBakeObstacles(desc, &cube, 1, &sphere, 1);
    EXPECT_EQ(desc.cubeCount, 1);
    EXPECT_EQ(desc.sphereCount, 1);
    EXPECT_EQ(desc.cubes, &cube);
    EXPECT_EQ(desc.spheres, &sphere);
    setHunterBakeObstacles(desc, &cube, 0, nullptr, 0);
    EXPECT_EQ(desc.cubes, nullptr);
    EXPECT_EQ(desc.cubeCount, 0);
    EXPECT_EQ(desc.spheres, nullptr);
    EXPECT_EQ(desc.sphereCount, 0);
}

TEST(HunterTick, SceneStealthOverridesHearingAndAwareness)
{
    AiSystem ai;
    PlayerStealthSettings stealth;
    stealth.hearWalk = 1.0f;
    StealthSceneDesc scene{};
    applySceneStealth(stealth, ai, scene);
    EXPECT_FALSE(ai.awarenessEnabled());
    EXPECT_FLOAT_EQ(stealth.hearStand, 4.0f);
    EXPECT_FLOAT_EQ(stealth.hearWalk, 14.0f);
    EXPECT_FLOAT_EQ(stealth.hearSprint, 24.0f);
    EXPECT_FLOAT_EQ(stealth.hearCrouchWalk, 5.5f);

    scene.present           = true;
    scene.awareness         = true;
    scene.hasHearStand      = true;
    scene.hearStand         = 0.0f;
    scene.hasHearWalk       = true;
    scene.hearWalk          = 6.0f;
    scene.hasHearSprint     = true;
    scene.hearSprint        = 16.0f;
    scene.hasHearCrouchWalk = true;
    scene.hearCrouchWalk    = 2.5f;
    applySceneStealth(stealth, ai, scene);
    EXPECT_TRUE(ai.awarenessEnabled());
    EXPECT_FLOAT_EQ(stealth.hearStand, 0.0f);
    EXPECT_FLOAT_EQ(stealth.hearWalk, 6.0f);
    EXPECT_FLOAT_EQ(stealth.hearSprint, 16.0f);
    EXPECT_FLOAT_EQ(stealth.hearCrouchWalk, 2.5f);
    EXPECT_FLOAT_EQ(stealth.hearCrouchStill, 1.25f);
    EXPECT_FLOAT_EQ(stealth.sightCrouchStill, PlayerStealthSettings{}.sightCrouchStill);
    EXPECT_NEAR(ai.awarenessSettings().alertShareRadius, 30.0f, 1.0e-3f);

    scene.hasSettings = true;
    scene.settings.alertShareRadius = 3.5f;
    scene.settings.wallScale        = -1.0f;
    scene.settingsMask              = (1ull << 27) | (1ull << 30);
    applySceneStealth(stealth, ai, scene);
    EXPECT_NEAR(ai.awarenessSettings().alertShareRadius, 3.5f, 1.0e-4f);
    EXPECT_FLOAT_EQ(ai.awarenessSettings().wallScale, 0.0f);
    EXPECT_NEAR(ai.awarenessSettings().fillNear, 1.8f, 1.0e-3f);
    EXPECT_FLOAT_EQ(stealth.hearWalk, 6.0f);
}

TEST(WeaponTargetList, GatherSkipsSelfAndBodiesWithoutTransform)
{
    World  world;
    Entity self = world.createEntity();
    world.emplace<TransformComponent>(self, Vector3f(0.0f, 0.0f, 0.0f));
    world.emplace<HittableComponent>(self, Vector3f(1.0f, 1.0f, 1.0f));
    world.emplace<HealthComponent>(self);

    Entity other = world.createEntity();
    world.emplace<TransformComponent>(other, Vector3f(3.0f, 1.0f, 4.0f));
    world.emplace<HittableComponent>(other, Vector3f(0.2f, 0.3f, 0.4f));
    world.emplace<HealthComponent>(other);

    Entity dead = world.createEntity();
    world.emplace<TransformComponent>(dead, Vector3f(8.0f, 0.0f, 0.0f));
    world.emplace<HittableComponent>(dead, Vector3f(0.5f, 0.5f, 0.5f));
    world.emplace<HealthComponent>(dead).health.applyDamage(1000.0f);

    Entity noTransform = world.createEntity();
    world.emplace<HittableComponent>(noTransform, Vector3f(1.0f, 1.0f, 1.0f));

    WeaponTargetList list;
    list.gather(world, self);
    ASSERT_EQ(list.targets().size(), 2u);
    EXPECT_EQ(list.targets()[0].entity, other);
    EXPECT_TRUE(list.targets()[0].alive);
    EXPECT_NEAR(list.targets()[0].center.x, 3.0f, 1.0e-5f);
    EXPECT_NEAR(list.targets()[0].halfExtents.y, 0.3f, 1.0e-5f);
    EXPECT_EQ(list.targets()[1].entity, dead);
    EXPECT_FALSE(list.targets()[1].alive);
    EXPECT_FALSE(list.entityAt(-1).valid());
    EXPECT_FALSE(list.entityAt(2).valid());
    EXPECT_EQ(list.entityAt(1), dead);

    WeaponWorldQuery query;
    list.bind(query);
    EXPECT_EQ(weaponTargetCount(query), 2);
    EXPECT_TRUE(weaponTargetAlive(query, 0));
    EXPECT_FALSE(weaponTargetAlive(query, 1));
    EXPECT_FALSE(weaponTargetAlive(query, 4));
    Vector3f center;
    ASSERT_TRUE(weaponTargetCenter(query, 0, center));
    EXPECT_NEAR(center.z, 4.0f, 1.0e-5f);
    const AABox3f box = weaponTargetBox(query, 0);
    EXPECT_NEAR(box.Extents().x, 0.2f, 1.0e-5f);
    EXPECT_EQ(query.targetEntityAt(query.targetUser, 0), other);
    EXPECT_FALSE(query.targetEntityAt(query.targetUser, -1).valid());
}

TEST(CharacterAttach, HumanoidBodySetsHpOnce)
{
    World world;
    Entity fresh = world.createEntity();
    ensureHumanoidBody(world, fresh, 200.0f, Vector3f{ 0.35f, 0.7f, 0.35f });

    HealthComponent* health = world.get<HealthComponent>(fresh);
    ASSERT_NE(health, nullptr);
    EXPECT_FLOAT_EQ(health->health.maxHp(), 200.0f);
    EXPECT_FLOAT_EQ(health->health.hp(), 200.0f);
    HittableComponent* hit = world.get<HittableComponent>(fresh);
    ASSERT_NE(hit, nullptr);
    EXPECT_NEAR(hit->halfExtents.x, 0.35f, 1.0e-5f);
    EXPECT_NEAR(hit->halfExtents.y, 0.7f, 1.0e-5f);
    HitReactionComponent* reaction = world.get<HitReactionComponent>(fresh);
    ASSERT_NE(reaction, nullptr);
    EXPECT_FLOAT_EQ(reaction->hit.settings().stunSeconds, 0.28f);

    health->health.applyDamage(40.0f);
    hit->halfExtents.y = 3.0f;
    ensureHumanoidBody(world, fresh, 50.0f, Vector3f{ 9.0f, 9.0f, 9.0f });
    EXPECT_FLOAT_EQ(health->health.hp(), 160.0f);
    EXPECT_FLOAT_EQ(health->health.maxHp(), 200.0f);
    EXPECT_NEAR(hit->halfExtents.y, 3.0f, 1.0e-5f);

    Entity kept = world.createEntity();
    world.emplace<HealthComponent>(kept);
    ensureHumanoidBody(world, kept, 200.0f, Vector3f{ 0.35f, 0.7f, 0.35f });
    ASSERT_NE(world.get<HealthComponent>(kept), nullptr);
    EXPECT_FLOAT_EQ(world.get<HealthComponent>(kept)->health.maxHp(), 100.0f);
    EXPECT_NEAR(world.get<HittableComponent>(kept)->halfExtents.y, 0.7f, 1.0e-5f);

    ensureHumanoidBody(world, Entity{}, 200.0f, Vector3f{ 1.0f, 1.0f, 1.0f });
}

TEST(PhysicsGround, InvalidWorldKeepsTerrainAndRejectsHeightField)
{
    Physics::PhysicsWorld physics;
    Terrain::HeightMap empty;
    EXPECT_FALSE(physics.valid());
    EXPECT_EQ(bindHeightFieldGround(physics, empty, "Test"), Physics::kNullPhysicsBody);
    EXPECT_FLOAT_EQ(raisedGroundY(physics, Physics::kNullPhysicsBody, Physics::kNullPhysicsBody, 4.5f, 1.0f, 2.0f, 10.0f), 4.5f);
}

TEST(PhysicsGround, HeightFieldAndRaisedSurface)
{
    Physics::PhysicsWorld physics;
    Physics::PhysicsWorldDesc desc;
    desc.enabled = true;
    desc.gravity = 24.0f;
    ASSERT_TRUE(physics.create(desc));

    Terrain::HeightMap empty;
    EXPECT_EQ(bindHeightFieldGround(physics, empty, "Test"), Physics::kNullPhysicsBody);

    Terrain::HeightMap coarse;
    ASSERT_TRUE(coarse.create(2, 2, 1.0f, 1.0f));
    const Physics::PhysicsBodyId field = bindHeightFieldGround(physics, coarse, "Test");
    EXPECT_NE(field, Physics::kNullPhysicsBody);

    Physics::PhysicsBoxDesc box;
    box.position    = Vector3f(0.0f, 2.0f, 0.0f);
    box.halfExtents = Vector3f(1.0f, 1.0f, 1.0f);
    box.dynamic     = false;
    const Physics::PhysicsBodyId deck = physics.createBox(box);
    ASSERT_NE(deck, Physics::kNullPhysicsBody);

    const float raised = raisedGroundY(physics, Physics::kNullPhysicsBody, field, 0.0f, 0.0f, 0.0f, 10.0f);
    EXPECT_GT(raised, 0.0f);
    EXPECT_NEAR(raised, 3.0f, 0.35f);
    EXPECT_FLOAT_EQ(raisedGroundY(physics, Physics::kNullPhysicsBody, deck, 0.0f, 0.0f, 0.0f, 10.0f), 0.0f);
    EXPECT_FLOAT_EQ(raisedGroundY(physics, Physics::kNullPhysicsBody, field, 4.0f, 0.0f, 0.0f, 10.0f), 4.0f);
}

TEST(TerrainGround, NullGridContactIsGrass)
{
    Terrain::TerrainGround ground;
    const Terrain::GroundContact contact = Terrain::groundContactAt(ground, nullptr, 1.0f, 2.0f);
    EXPECT_STREQ(contact.id, "grass");
    EXPECT_STREQ(contact.cue, "step_grass");
}

namespace
{

    Collision::RayHit3D shortBoomRay(void*, const Ray3f&, float)
    {
        Collision::RayHit3D hit;
        hit.hit = true;
        hit.t   = 0.5f;
        return hit;
    }

    // Closer than the 0.12 m skin, so the solver stops on its 0.28 m minimum.
    Collision::RayHit3D minBoomRay(void*, const Ray3f&, float)
    {
        Collision::RayHit3D hit;
        hit.hit = true;
        hit.t   = 0.10f;
        return hit;
    }

    struct LateralPlane
    {
        float x = 0.0f;
    };

    Collision::RayHit3D lateralPlaneRay(void* user, const Ray3f& ray, float maxDistance)
    {
        Collision::RayHit3D hit;
        const LateralPlane* plane = static_cast<const LateralPlane*>(user);
        if (!plane)
            return hit;
        const float dx = ray.Direction.x;
        if (dx == 0.0f)
            return hit;
        const float t = (plane->x - ray.Origin.x) / dx;
        if (t < 0.0f || t > maxDistance)
            return hit;
        hit.hit    = true;
        hit.t      = t;
        hit.point  = ray.PointAt(t);
        hit.normal = Vector3f{ dx > 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f };
        return hit;
    }

} // namespace

TEST(PlayLook, MouseGateClampAndWish)
{
    float yaw   = 0.2f;
    float pitch = 0.1f;
    accumulatePlayLook(yaw, pitch, 1000.0f, -1000.0f, false, 0.0f, 0.0f, 1.0f);
    EXPECT_FLOAT_EQ(yaw, 0.2f);
    EXPECT_FLOAT_EQ(pitch, 0.1f);

    accumulatePlayLook(yaw, pitch, 0.0f, 10000.0f, true, 0.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(pitch, 0.96f);

    yaw = 3.1f;
    accumulatePlayLook(yaw, pitch, 1000.0f, 0.0f, true, 0.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(yaw, Math::WrapPi(3.1f + 1000.0f * 0.0045f));
    EXPECT_GT(std::fabs(yaw), 0.0f);
    EXPECT_LE(std::fabs(yaw), 3.14159274f);

    const PlayBasis forward = playBasis(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(forward.look.z, 1.0f, 1.0e-5f);
    EXPECT_NEAR(forward.wish.z, 1.0f, 1.0e-5f);
    EXPECT_NEAR(forward.wish.x, 0.0f, 1.0e-5f);

    const PlayBasis diagonal = playBasis(0.0f, 0.0f, 1.0f, 1.0f);
    EXPECT_NEAR(diagonal.wish.Magnitude(), 1.0f, 1.0e-5f);
}

TEST(PlayMotor, InputLatchAirAndClip)
{
    const PlayBasis basis = playBasis(0.0f, 0.0f, 0.0f, 1.0f);
    PlayMotorEdges edges;
    edges.sprintDown    = true;
    edges.crouchPressed = true;
    edges.jumpPressed   = true;
    edges.forward       = true;

    PlayerMotorInput motorIn{};
    bool             latch = true;
    Combat::JumpAttack jump;
    buildPlayMotorInput(motorIn, latch, basis.wish, basis.flat, basis.right, true, false, false, true, &jump, false, edges);
    EXPECT_FALSE(motorIn.sprint);
    EXPECT_FALSE(latch);
    EXPECT_FALSE(motorIn.crouch);
    EXPECT_TRUE(motorIn.jumpPressed);
    EXPECT_FALSE(motorIn.allowDoubleJump);
    EXPECT_FLOAT_EQ(motorIn.airControlScale, 0.35f);
    EXPECT_EQ(motorIn.dodgeTap, MoveCardinal::Forward);
    EXPECT_NEAR(motorIn.dodgeTapWish.z, 1.0f, 1.0e-5f);

    PlayerMotorInput blocked{};
    bool             held = false;
    edges.crouchDown      = true;
    buildPlayMotorInput(blocked, held, basis.wish, basis.flat, basis.right, true, false, true, false, nullptr, false, edges);
    EXPECT_FALSE(blocked.sprint);
    EXPECT_FALSE(blocked.crouch);
    EXPECT_FALSE(blocked.jumpPressed);
    EXPECT_FALSE(blocked.allowDodge);
    EXPECT_EQ(blocked.dodgeTap, MoveCardinal::None);
    EXPECT_FLOAT_EQ(blocked.airControlScale, 1.0f);

    PlayerMotorInput stopped{};
    bool             stoppedLatch = false;
    buildPlayMotorInput(stopped, stoppedLatch, basis.wish, basis.flat, basis.right, false, false, false, false, nullptr, false, edges);
    EXPECT_FLOAT_EQ(stopped.wish.x, 0.0f);
    EXPECT_FLOAT_EQ(stopped.wish.y, 0.0f);
    EXPECT_FLOAT_EQ(stopped.wish.z, 0.0f);
}

namespace
{
    PlanarContact wallAt(float t, const Vector3f& normal)
    {
        PlanarContact contact;
        contact.hit       = true;
        contact.t         = t;
        contact.hasNormal = true;
        contact.normal    = normal;
        return contact;
    }
}

TEST(PlayShared, HeadOnSlideStopsIntoVelocity)
{
    PlayerMotor motor;
    motor.setHorizontalVelocity(3.84f, 1.0f);
    Vector3f       position{ 0.0f, 5.0f, 0.0f };
    const Vector3f before = position;
    Vector3f       delta{ 2.0f, 0.0f, 0.0f };
    auto           sweep = [](const Vector3f&, const Vector3f&) { return wallAt(0.5f, Vector3f{ -1.0f, 0.0f, 0.0f }); };
    slidePlanar(position, before, delta, 0.25f, &motor, 5.0f, 0.5f, sweep);
    EXPECT_NEAR(position.x, 0.96f, 1.0e-4f);
    EXPECT_FLOAT_EQ(position.y, 5.0f);
    EXPECT_FLOAT_EQ(position.z, 0.0f);
    EXPECT_NEAR(delta.x, 0.96f, 1.0e-4f);
    EXPECT_NEAR(motor.velocity().x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(motor.velocity().z, 0.0f, 1.0e-4f);
}

TEST(PlayShared, DiagonalSlideKeepsTangent)
{
    PlayerMotor motor;
    motor.setHorizontalVelocity(1.0f, 1.0f);
    Vector3f       position{ 0.0f, 5.0f, 0.0f };
    const Vector3f before = position;
    Vector3f       delta{ 2.0f, 0.0f, 2.0f };
    auto           sweep = [](const Vector3f&, const Vector3f& step) {
        PlanarContact contact;
        if (step.x > 1.0e-3f)
            contact = wallAt(0.5f, Vector3f{ -1.0f, 0.0f, 0.0f });
        return contact;
    };
    slidePlanar(position, before, delta, 0.25f, &motor, 5.0f, 0.5f, sweep);
    EXPECT_NEAR(position.x, 0.96f, 1.0e-4f);
    EXPECT_NEAR(position.z, 2.0f, 1.0e-4f);
    EXPECT_FLOAT_EQ(position.y, 5.0f);
    EXPECT_NEAR(motor.velocity().x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(motor.velocity().z, 8.0f, 1.0e-3f);
}

TEST(PlayShared, ClearStepDoesNotWriteMotor)
{
    PlayerMotor motor;
    motor.setHorizontalVelocity(3.0f, 4.0f);
    Vector3f       position{ 1.0f, 5.0f, 2.0f };
    const Vector3f before = position;
    Vector3f       delta{ 2.0f, 0.0f, 1.0f };
    auto           sweep = [](const Vector3f&, const Vector3f&) { return PlanarContact{}; };
    slidePlanar(position, before, delta, 0.25f, &motor, 5.0f, 0.5f, sweep);
    EXPECT_NEAR(position.x, 3.0f, 1.0e-5f);
    EXPECT_FLOAT_EQ(position.y, 5.0f);
    EXPECT_NEAR(position.z, 3.0f, 1.0e-5f);
    EXPECT_NEAR(delta.x, 2.0f, 1.0e-5f);
    EXPECT_NEAR(delta.z, 1.0f, 1.0e-5f);
    EXPECT_FLOAT_EQ(motor.velocity().x, 3.0f);
    EXPECT_FLOAT_EQ(motor.velocity().z, 4.0f);
}

TEST(PlayShared, MinWallXZHardStopsOrProjects)
{
    const Vector3f normal{ -0.4f, std::sqrt(1.0f - 0.16f), 0.0f };
    EXPECT_NEAR(normal.Magnitude(), 1.0f, 1.0e-5f);
    EXPECT_NEAR(std::sqrt(normal.x * normal.x + normal.z * normal.z), 0.4f, 1.0e-5f);

    auto sweep = [&](const Vector3f&, const Vector3f& step) {
        PlanarContact contact;
        if (step.x > 1.0e-3f)
            contact = wallAt(0.5f, normal);
        return contact;
    };

    PlayerMotor steep;
    steep.setHorizontalVelocity(4.0f, 4.0f);
    Vector3f       steepPos{ 0.0f, 5.0f, 0.0f };
    const Vector3f steepBefore = steepPos;
    Vector3f       steepDelta{ 2.0f, 0.0f, 2.0f };
    slidePlanar(steepPos, steepBefore, steepDelta, 0.25f, &steep, 5.0f, 0.5f, sweep);
    EXPECT_NEAR(steepPos.x, 0.96f, 1.0e-4f);
    EXPECT_NEAR(steepPos.z, 0.96f, 1.0e-4f);
    EXPECT_FLOAT_EQ(steepPos.y, 5.0f);
    EXPECT_NEAR(steep.velocity().x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(steep.velocity().z, 0.0f, 1.0e-4f);

    PlayerMotor shallow;
    shallow.setHorizontalVelocity(4.0f, 4.0f);
    Vector3f       shallowPos{ 0.0f, 5.0f, 0.0f };
    const Vector3f shallowBefore = shallowPos;
    Vector3f       shallowDelta{ 2.0f, 0.0f, 2.0f };
    slidePlanar(shallowPos, shallowBefore, shallowDelta, 0.25f, &shallow, 5.0f, 0.25f, sweep);
    EXPECT_NEAR(shallowPos.x, 0.96f, 1.0e-4f);
    EXPECT_NEAR(shallowPos.z, 2.0f, 1.0e-4f);
    EXPECT_FLOAT_EQ(shallowPos.y, 5.0f);
    EXPECT_NEAR(shallow.velocity().z, 8.0f, 1.0e-3f);
}

TEST(PlayShared, OutsideCornerSlidesPast)
{
    const AABox3f  box{ Vector3f{ 1.0f, 0.0f, 1.0f }, Vector3f{ 3.0f, 8.0f, 3.0f } };
    const Sphere3f startBall{ Vector3f{ 0.0f, 5.0f, 0.0f }, kPlayBodyRadius };
    const Vector3f step{ 2.0f, 0.0f, 1.2f };
    const PlanarContact corner = contactSphereSolid(startBall, step, box);
    ASSERT_TRUE(corner.hit);
    ASSERT_TRUE(corner.hasNormal);
    EXPECT_GT(std::fabs(corner.normal.x), 1.0e-3f);
    EXPECT_GT(std::fabs(corner.normal.z), 1.0e-3f);
    EXPECT_NEAR(corner.normal.Magnitude(), 1.0f, 1.0e-4f);

    const float    skinX = step.x * Math::Max(0.0f, corner.t - kPlanarSkinFraction);
    Vector3f       position{ 0.0f, 5.0f, 0.0f };
    const Vector3f before = position;
    Vector3f       delta  = step;
    auto           sweep  = [&](const Vector3f& origin, const Vector3f& sub) {
        const Sphere3f ball{ origin, kPlayBodyRadius };
        return contactSphereSolid(ball, sub, box);
    };
    slidePlanar(position, before, delta, 0.25f, nullptr, 5.0f, 0.5f, sweep);
    EXPECT_GT(position.x, skinX + 0.05f);
    EXPECT_FLOAT_EQ(position.y, 5.0f);
    const Sphere3f endBall{ Vector3f{ position.x, 5.0f, position.z }, kPlayBodyRadius };
    EXPECT_FALSE(Collision::Intersects(endBall, box));
}

TEST(PlayShared, SphereSolidNormalIsUnit)
{
    const Vector3f n{ 0.5f, std::sqrt(1.0f - 0.25f), 0.0f };
    const float    sep = 0.45f + 0.45f;
    const Sphere3f solid{ Vector3f{ 0.0f, 0.0f, 0.0f }, 0.45f };
    const Vector3f contactCenter = n * sep;
    const Sphere3f mover{ contactCenter + n, 0.45f };
    const PlanarContact hit = contactSphereSolid(mover, n * -2.0f, solid);
    ASSERT_TRUE(hit.hit);
    ASSERT_TRUE(hit.hasNormal);
    EXPECT_NEAR(hit.normal.Magnitude(), 1.0f, 1.0e-4f);
    const float xz = std::sqrt(hit.normal.x * hit.normal.x + hit.normal.z * hit.normal.z);
    EXPECT_GE(xz, 0.5f - 1.0e-4f);
}

TEST(PlayShared, PhysicsRejectDoesNotAddRest)
{
    PlanarContact host{};
    Physics::PhysicsWorld::MoverHit phys;
    phys.fraction  = 0.4f;
    phys.hasNormal = false;
    const PlanarContact rejected = preferPlanarContact(host, phys, 2.0f);
    EXPECT_TRUE(rejected.hit);
    EXPECT_FLOAT_EQ(rejected.t, 0.4f);
    EXPECT_FALSE(rejected.hasNormal);

    Vector3f       position{ 0.0f, 5.0f, 0.0f };
    const Vector3f before = position;
    Vector3f       delta{ 2.0f, 0.0f, 0.0f };
    auto           sweep = [&](const Vector3f&, const Vector3f&) { return rejected; };
    slidePlanar(position, before, delta, 0.25f, nullptr, 5.0f, 0.5f, sweep);
    EXPECT_NEAR(position.x, 0.76f, 1.0e-4f);
    EXPECT_LT(position.x, 1.0f);
    EXPECT_FLOAT_EQ(position.y, 5.0f);

    const PlanarContact hostHit = wallAt(0.8f, Vector3f{ -1.0f, 0.0f, 0.0f });
    Physics::PhysicsWorld::MoverHit closer;
    closer.fraction  = 0.47f;
    closer.hasNormal = false;
    const PlanarContact stopped = preferPlanarContact(hostHit, closer, 1.0f);
    EXPECT_TRUE(stopped.hit);
    EXPECT_FALSE(stopped.hasNormal);
    EXPECT_FLOAT_EQ(stopped.t, 0.47f);
}

TEST(PlayShared, InsideCornerDropsThirdRemainder)
{
    Vector3f       position{ 0.0f, 5.0f, 0.0f };
    const Vector3f before = position;
    Vector3f       delta{ 2.0f, 0.0f, 2.0f };
    int            hits = 0;
    auto           sweep = [&](const Vector3f&, const Vector3f&) {
        PlanarContact contact;
        contact.hit       = true;
        contact.t         = 0.5f;
        contact.hasNormal = true;
        if (hits == 0)
            contact.normal = Vector3f{ -1.0f, 0.0f, 0.0f };
        else if (hits == 1)
            contact.normal = Vector3f{ -0.6f, 0.0f, -0.8f };
        else
            contact.normal = Vector3f{ 0.0f, 0.0f, -1.0f };
        ++hits;
        return contact;
    };
    slidePlanar(position, before, delta, 0.25f, nullptr, 5.0f, 0.5f, sweep);
    EXPECT_EQ(hits, 3);
    EXPECT_NEAR(position.x, 0.8354f, 1.0e-3f);
    EXPECT_NEAR(position.z, 1.55265f, 1.0e-3f);
    EXPECT_GT(position.x, 0.78f);
    EXPECT_FLOAT_EQ(position.y, 5.0f);

    const AABox3f  wallX{ Vector3f{ 1.40f, 4.0f, 0.0f }, Vector3f{ 2.2f, 6.0f, 3.0f } };
    const AABox3f  wallZ{ Vector3f{ 0.0f, 4.0f, 2.20f }, Vector3f{ 2.0f, 6.0f, 3.0f } };
    const Sphere3f body{ Vector3f{ position.x, position.y, position.z }, kPlayBodyRadius };
    EXPECT_FALSE(Collision::Intersects(body, wallX));
    EXPECT_FALSE(Collision::Intersects(body, wallZ));
}

TEST(PlayShared, DuplicateSolidKeepsHostCorner)
{
    const AABox3f  box{ Vector3f{ 1.0f, 0.0f, 1.0f }, Vector3f{ 3.0f, 8.0f, 3.0f } };
    const Vector3f step{ 2.0f, 0.0f, 1.2f };
    const Sphere3f startBall{ Vector3f{ 0.0f, 5.0f, 0.0f }, kPlayBodyRadius };
    const PlanarContact host = contactSphereSolid(startBall, step, box);
    ASSERT_TRUE(host.hit);
    ASSERT_TRUE(host.hasNormal);

    Physics::PhysicsWorld::MoverHit phys;
    phys.hasNormal = false;
    phys.fraction  = host.t - (0.01f / step.Magnitude());
    const PlanarContact merged = preferPlanarContact(host, phys, step.Magnitude());
    EXPECT_TRUE(merged.hit);
    EXPECT_TRUE(merged.hasNormal);
    EXPECT_NEAR(merged.normal.x, host.normal.x, 1.0e-5f);
    EXPECT_NEAR(merged.normal.z, host.normal.z, 1.0e-5f);
    EXPECT_NEAR(merged.t, phys.fraction, 1.0e-5f);

    const PlanarContact laterHost = wallAt(0.40f, Vector3f{ -1.0f, 0.0f, 0.0f });
    Physics::PhysicsWorld::MoverHit later;
    later.fraction  = 0.405f;
    later.hasNormal = false;
    const PlanarContact shared = preferPlanarContact(laterHost, later, 1.0f);
    EXPECT_TRUE(shared.hasNormal);
    EXPECT_FLOAT_EQ(shared.t, 0.40f);
    EXPECT_FLOAT_EQ(shared.normal.x, -1.0f);

    const float    skinX = step.x * Math::Max(0.0f, host.t - kPlanarSkinFraction);
    Vector3f       position{ 0.0f, 5.0f, 0.0f };
    const Vector3f before = position;
    Vector3f       delta  = step;
    auto           sweep  = [&](const Vector3f& origin, const Vector3f& sub) {
        const Sphere3f      ball{ origin, kPlayBodyRadius };
        const PlanarContact face = contactSphereSolid(ball, sub, box);
        Physics::PhysicsWorld::MoverHit cast;
        cast.fraction  = 1.0f;
        cast.hasNormal = false;
        if (face.hit && face.t > 0.0f && face.t < 1.0f)
        {
            const float len = sub.Magnitude();
            if (len > 1.0e-6f)
                cast.fraction = Math::Max(0.0f, face.t - (0.01f / len));
        }
        return preferPlanarContact(face, cast, sub.Magnitude());
    };
    slidePlanar(position, before, delta, 0.25f, nullptr, 5.0f, 0.5f, sweep);
    EXPECT_GT(position.x, skinX + 0.05f);
    EXPECT_FLOAT_EQ(position.y, 5.0f);
    const Sphere3f endBall{ Vector3f{ position.x, 5.0f, position.z }, kPlayBodyRadius };
    EXPECT_FALSE(Collision::Intersects(endBall, box));
}

TEST(PlayShared, InvalidPhysicsKeepsHostHit)
{
    Physics::PhysicsWorld physics;
    Physics::PhysicsWorld::MoverCast cast;
    cast.origin      = Vector3f{ 0.0f, 0.0f, 0.0f };
    cast.radius      = kPlayBodyRadius;
    cast.bottom      = -kPlayBodyRadius;
    cast.top         = 1.35f;
    cast.translation = Vector3f{ 1.0f, 0.0f, 0.0f };
    cast.ignore      = Physics::kNullPhysicsBody;
    cast.ignore2     = Physics::kNullPhysicsBody;
    cast.staticOnly  = true;
    const Physics::PhysicsWorld::MoverHit phys = physics.clipMoverHit(cast);
    EXPECT_FLOAT_EQ(phys.fraction, 1.0f);
    EXPECT_FALSE(phys.hasNormal);

    const PlanarContact host = wallAt(0.4f, Vector3f{ -1.0f, 0.0f, 0.0f });
    const PlanarContact kept = preferPlanarContact(host, phys, cast.translation.Magnitude());
    EXPECT_TRUE(kept.hit);
    EXPECT_FLOAT_EQ(kept.t, 0.4f);
    EXPECT_TRUE(kept.hasNormal);
    EXPECT_FLOAT_EQ(kept.normal.x, -1.0f);
}

TEST(PlayShared, MixedHardStopZerosVelocity)
{
    PlayerMotor motor;
    motor.setHorizontalVelocity(5.0f, 5.0f);
    Vector3f       position{ 0.0f, 5.0f, 0.0f };
    const Vector3f before = position;
    Vector3f       delta{ 2.0f, 0.0f, 2.0f };
    int            hits = 0;
    auto           sweep = [&](const Vector3f&, const Vector3f&) {
        PlanarContact contact;
        contact.hit = true;
        contact.t   = 0.5f;
        if (hits == 0)
        {
            contact.hasNormal = true;
            contact.normal    = Vector3f{ -1.0f, 0.0f, 0.0f };
        }
        ++hits;
        return contact;
    };
    slidePlanar(position, before, delta, 0.25f, &motor, 5.0f, 0.5f, sweep);
    EXPECT_GE(hits, 2);
    EXPECT_FLOAT_EQ(position.y, 5.0f);
    EXPECT_NEAR(motor.velocity().x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(motor.velocity().z, 0.0f, 1.0e-4f);
}

// Body radius 0.45, face at x = 1. The center at 0.70 is outside the solid and 0.15 m inside the expanded box.
TEST(PlayShared, OverlappingWallStillMovesAway)
{
    const AABox3f  wall{ Vector3f{ 1.0f, 0.0f, -4.0f }, Vector3f{ 3.0f, 8.0f, 4.0f } };
    const Vector3f start{ 0.70f, 5.0f, 0.0f };
    const PlanarContact overlap = contactSphereSolid(Sphere3f{ start, kPlayBodyRadius }, Vector3f{ -1.0f, 0.0f, 0.0f }, wall);
    ASSERT_TRUE(overlap.hit);
    EXPECT_LE(overlap.t, 0.0f);
    ASSERT_TRUE(overlap.hasNormal);
    EXPECT_LT(overlap.normal.x, -0.5f);

    PlayerMotor motor;
    motor.setHorizontalVelocity(-4.0f, 1.5f);
    Vector3f       position = start;
    const Vector3f before   = position;
    Vector3f       delta{ -1.0f, 0.0f, 0.0f };
    auto           sweep = [&](const Vector3f& origin, const Vector3f& step) {
        return contactSphereSolid(Sphere3f{ origin, kPlayBodyRadius }, step, wall);
    };
    slidePlanar(position, before, delta, 0.25f, &motor, 5.0f, 0.5f, sweep);
    EXPECT_NEAR(position.x, -0.30f, 1.0e-3f);
    EXPECT_NEAR(position.z, 0.0f, 1.0e-3f);
    EXPECT_FLOAT_EQ(position.y, 5.0f);
    EXPECT_NEAR(motor.velocity().x, -4.0f, 1.0e-3f);
    EXPECT_NEAR(motor.velocity().z, 1.5f, 1.0e-3f);
    EXPECT_FALSE(Collision::Intersects(Sphere3f{ Vector3f{ position.x, position.y, position.z }, kPlayBodyRadius }, wall));
}

TEST(PlayShared, OverlappingWallKeepsTheTangent)
{
    const AABox3f wall{ Vector3f{ 1.0f, 0.0f, -4.0f }, Vector3f{ 3.0f, 8.0f, 4.0f } };
    PlayerMotor   motor;
    motor.setHorizontalVelocity(1.0f, 6.0f);
    Vector3f       position{ 0.70f, 5.0f, 0.0f };
    const Vector3f before = position;
    Vector3f       delta{ 0.25f, 0.0f, 1.5f };
    auto           sweep = [&](const Vector3f& origin, const Vector3f& step) {
        return contactSphereSolid(Sphere3f{ origin, kPlayBodyRadius }, step, wall);
    };
    slidePlanar(position, before, delta, 0.25f, &motor, 5.0f, 0.5f, sweep);
    EXPECT_NEAR(position.x, 0.70f, 1.0e-3f);
    EXPECT_NEAR(position.z, 1.5f, 1.0e-3f);
    EXPECT_FLOAT_EQ(position.y, 5.0f);
    EXPECT_NEAR(motor.velocity().x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(motor.velocity().z, 6.0f, 1.0e-3f);
}

TEST(PlayShared, OverlappingWallHeadOnDoesNotSink)
{
    const AABox3f wall{ Vector3f{ 1.0f, 0.0f, -4.0f }, Vector3f{ 3.0f, 8.0f, 4.0f } };
    PlayerMotor   motor;
    motor.setHorizontalVelocity(4.0f, 2.0f);
    Vector3f       position{ 0.70f, 5.0f, 0.0f };
    const Vector3f before = position;
    Vector3f       delta{ 1.0f, 0.0f, 0.0f };
    auto           sweep = [&](const Vector3f& origin, const Vector3f& step) {
        return contactSphereSolid(Sphere3f{ origin, kPlayBodyRadius }, step, wall);
    };
    slidePlanar(position, before, delta, 0.25f, &motor, 5.0f, 0.5f, sweep);
    EXPECT_NEAR(position.x, 0.70f, 1.0e-3f);
    EXPECT_NEAR(position.z, 0.0f, 1.0e-3f);
    EXPECT_FLOAT_EQ(position.y, 5.0f);
    EXPECT_NEAR(motor.velocity().x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(motor.velocity().z, 0.0f, 1.0e-3f);
}

// Door half-thickness 0.09 m. The center at z = 1.98 is inside [1.91, 2.09]. The near face is -Z.
TEST(PlayShared, CenterInsideADoorCanStepOut)
{
    const AABox3f  door = AABox3f::FromCenterExtents(Vector3f{ 0.0f, 1.3f, 2.0f }, Vector3f{ 2.0f, 1.3f, 0.09f });
    const Vector3f start{ 0.0f, 1.3f, 1.98f };
    const PlanarContact inside = contactSphereSolid(Sphere3f{ start, kPlayBodyRadius }, Vector3f{ 0.0f, 0.0f, -1.0f }, door);
    ASSERT_TRUE(inside.hit);
    EXPECT_LE(inside.t, 0.0f);
    ASSERT_TRUE(inside.hasNormal);
    EXPECT_LT(inside.normal.z, -0.5f);

    PlayerMotor motor;
    Vector3f    position = start;
    Vector3f    delta{ 0.0f, 0.0f, -1.0f };
    auto        sweep = [&](const Vector3f& origin, const Vector3f& step) {
        return contactSphereSolid(Sphere3f{ origin, kPlayBodyRadius }, step, door);
    };
    slidePlanar(position, start, delta, 0.25f, &motor, start.y, 0.5f, sweep);
    EXPECT_NEAR(position.z, 0.98f, 1.0e-3f);
    EXPECT_NEAR(position.x, 0.0f, 1.0e-3f);

    position = start;
    delta    = Vector3f{ 0.0f, 0.0f, 1.0f };
    slidePlanar(position, start, delta, 0.25f, &motor, start.y, 0.5f, sweep);
    EXPECT_NEAR(position.z, start.z, 1.0e-3f);
}

TEST(WeaponFire, CameraOriginAndShootSkill)
{
    const WeaponFireRequest shot = makeCameraFireRequest(Vector3f{ 0.0f, 1.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 2.0f }, nullptr, 1.5f);
    EXPECT_NEAR(shot.direction.z, 1.0f, 1.0e-5f);
    EXPECT_NEAR(shot.origin.y, 1.0f, 1.0e-5f);
    EXPECT_NEAR(shot.origin.z, 2.2f, 1.0e-5f);
    EXPECT_NEAR(shot.ownerPos.y, 1.0f, 1.0e-5f);
    EXPECT_FLOAT_EQ(shot.damageScale, 1.5f);

    const Vector3f owner{ 4.0f, 0.0f, 0.0f };
    const WeaponFireRequest zero = makeCameraFireRequest(Vector3f{ 0.0f, 0.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 0.0f }, &owner, 1.0f);
    EXPECT_NEAR(zero.direction.z, 1.0f, 1.0e-5f);
    EXPECT_NEAR(zero.origin.z, 2.2f, 1.0e-5f);
    EXPECT_FLOAT_EQ(zero.ownerPos.x, 4.0f);

    WeaponFireRequest untouched{};
    untouched.recoilScale = 0.25f;
    applyShootSkill(untouched, nullptr);
    EXPECT_FLOAT_EQ(untouched.recoilScale, 0.25f);

    SkillComponent skill;
    WeaponFireRequest skilled{};
    applyShootSkill(skilled, &skill);
    const int level = skill.level(SkillId::Shoot);
    EXPECT_FLOAT_EQ(skilled.recoilScale, skillScalar(SkillId::Shoot, SkillScalar::RecoilScale, level));
    EXPECT_FLOAT_EQ(skilled.cooldownScale, skillScalar(SkillId::Shoot, SkillScalar::CooldownScale, level));
}

TEST(ShoulderCamera, BoomShortenAndWetClamp)
{
    const Vector3f feet{ 0.0f, 0.0f, 0.0f };
    const ShoulderView stand = solveShoulderCamera(feet, 0.0f, 0.0f, false, 0.0f, 0.0f, false, 0.0f, nullptr, nullptr);
    EXPECT_NEAR(stand.eye.x, -0.42f, 1.0e-4f);
    EXPECT_NEAR(stand.eye.y, 1.43f, 1.0e-4f);
    EXPECT_NEAR(stand.eye.z, -2.4f, 1.0e-4f);
    EXPECT_NEAR(stand.aim.z, 16.0f, 1.0e-4f);
    EXPECT_NEAR(stand.aim.y, 1.15f, 1.0e-4f);

    const ShoulderView crouch = solveShoulderCamera(feet, 0.0f, 0.0f, true, 0.0f, 0.0f, false, 0.0f, nullptr, nullptr);
    EXPECT_NEAR(crouch.eye.y, 1.43f, 1.0e-4f);
    EXPECT_NEAR(crouch.aim.y, 1.15f, 1.0e-4f);

    const ShoulderView wet = solveShoulderCamera(feet, 0.0f, 0.0f, false, 0.0f, 0.0f, true, 5.0f, nullptr, nullptr);
    EXPECT_NEAR(wet.eye.y, 5.45f, 1.0e-4f);

    const ShoulderView shaken = solveShoulderCamera(feet, 0.0f, 0.0f, false, 0.2f, 0.1f, false, 0.0f, nullptr, nullptr);
    EXPECT_GT((shaken.eye - stand.eye).MagnitudeSqrd(), 1.0e-6f);

    const ShoulderView blocked = solveShoulderCamera(feet, 0.0f, 0.0f, false, 0.0f, 0.0f, false, 0.0f, &shortBoomRay, nullptr);
    const Vector3f focus{ 0.0f, 1.15f, 0.0f };
    EXPECT_NEAR((blocked.eye - focus).Magnitude(), 0.38f, 1.0e-4f);

    const Collision::RayHit3D miss = shoulderTerrainRay(nullptr, Ray3f{}, 2.0f);
    EXPECT_FALSE(miss.hit);
}

TEST(ShoulderCamera, BoxAndCharacterShortenBoom)
{
    const Vector3f feet{ 0.0f, 0.0f, 0.0f };
    const Vector3f focus{ 0.0f, 1.15f, 0.0f };
    const ShoulderView open = solveShoulderCamera(feet, 0.0f, 0.0f, false, 0.0f, 0.0f, false, 0.0f, nullptr, nullptr);
    const float openLen = (open.eye - focus).Magnitude();

    Physics::PhysicsWorld phys;
    Physics::PhysicsWorldDesc desc;
    desc.enabled = true;
    ASSERT_TRUE(phys.create(desc));

    World ecs;
    Entity player = ecs.createEntity();
    TransformComponent playerXf;
    playerXf.position = Vector3f(0.0f, 0.5f, 0.0f);
    ecs.emplace<TransformComponent>(player, playerXf);
    PhysicsComponent pawn;
    pawn.mode          = PhysicsBodyMode::Kinematic;
    pawn.shape         = PhysicsShapeKind::Capsule;
    pawn.fixedRotation = true;
    ecs.emplace<PhysicsComponent>(player, pawn);
    ASSERT_TRUE(Physics::bindPhysicsEntity(phys, ecs, player));

    ShoulderRayQuery query;
    query.physics = &phys;
    query.ignore  = phys.bodyOf(player);
    const ShoulderView self = solveShoulderCamera(feet, 0.0f, 0.0f, false, 0.0f, 0.0f, false, 0.0f, &shoulderBoomRay, &query);
    EXPECT_NEAR(self.eye.x, open.eye.x, 1.0e-3f);
    EXPECT_NEAR(self.eye.y, open.eye.y, 1.0e-3f);
    EXPECT_NEAR(self.eye.z, open.eye.z, 1.0e-3f);

    Physics::PhysicsBoxDesc wall;
    wall.position    = Vector3f(-0.21f, 1.29f, -1.2f);
    wall.halfExtents = Vector3f(2.0f, 2.0f, 0.15f);
    const Physics::PhysicsBodyId wallId = phys.createBox(wall);
    ASSERT_NE(wallId, Physics::kNullPhysicsBody);
    const ShoulderView blocked = solveShoulderCamera(feet, 0.0f, 0.0f, false, 0.0f, 0.0f, false, 0.0f, &shoulderBoomRay, &query);
    const float blockedLen = (blocked.eye - focus).Magnitude();
    EXPECT_LT(blockedLen, openLen - 0.4f);
    EXPECT_GT(blockedLen, 0.27f);

    phys.destroyBody(wallId);
    Entity npc = ecs.createEntity();
    TransformComponent npcXf;
    npcXf.position = wall.position;
    npcXf.scale    = Vector3f(4.0f, 4.0f, 0.3f);
    ecs.emplace<TransformComponent>(npc, npcXf);
    PhysicsComponent npcBody;
    npcBody.mode          = PhysicsBodyMode::Kinematic;
    npcBody.shape         = PhysicsShapeKind::Box;
    npcBody.fixedRotation = true;
    ecs.emplace<PhysicsComponent>(npc, npcBody);
    ASSERT_TRUE(Physics::bindPhysicsEntity(phys, ecs, npc));
    const ShoulderView character = solveShoulderCamera(feet, 0.0f, 0.0f, false, 0.0f, 0.0f, false, 0.0f, &shoulderBoomRay, &query);
    EXPECT_LT((character.eye - focus).Magnitude(), openLen - 0.4f);
}

TEST(ShoulderCamera, RightLeanClearsTheBody)
{
    const Vector3f     feet{ 0.0f, 0.0f, 0.0f };
    const Vector3f     pivot{ feet.x, feet.y + 1.15f, feet.z };
    const Vector3f     playRight = playBasis(0.0f, 0.0f, 0.0f, 0.0f).right;
    const ShoulderView stand     = solveShoulderCamera(feet, 0.0f, 0.0f, false, 0.0f, 0.0f, false, 0.0f, nullptr, nullptr);
    EXPECT_NEAR(stand.eye.x, feet.x - 0.42f, 1.0e-4f);
    EXPECT_NEAR(stand.eye.z, feet.z - 2.4f, 1.0e-4f);

    float              applied = 1.0f;
    const ShoulderView held    = leanShoulderEye(stand, pivot, playRight, 0.0f, nullptr, nullptr, applied);
    EXPECT_FLOAT_EQ(applied, 0.0f);
    EXPECT_FLOAT_EQ(held.eye.x, stand.eye.x);
    EXPECT_FLOAT_EQ(held.eye.y, stand.eye.y);
    EXPECT_FLOAT_EQ(held.eye.z, stand.eye.z);
    EXPECT_FLOAT_EQ(held.aim.x, stand.aim.x);
    EXPECT_FLOAT_EQ(held.aim.y, stand.aim.y);
    EXPECT_FLOAT_EQ(held.aim.z, stand.aim.z);

    const ShoulderView right = leanShoulderEye(stand, pivot, playRight, 1.0f, nullptr, nullptr, applied);
    EXPECT_NEAR(right.eye.x, feet.x + 0.70f, 1.0e-4f);
    EXPECT_FLOAT_EQ(right.eye.y, stand.eye.y);
    EXPECT_NEAR(right.eye.z, feet.z - 2.4f, 1.0e-4f);
    EXPECT_NEAR(right.aim.x - stand.aim.x, right.eye.x - stand.eye.x, 1.0e-5f);
    EXPECT_FLOAT_EQ(right.aim.y, stand.aim.y);
    EXPECT_FLOAT_EQ(right.aim.z, stand.aim.z);
    EXPECT_FLOAT_EQ(applied, 1.0f);

    const ShoulderView half = leanShoulderEye(stand, pivot, playRight, 0.5f, nullptr, nullptr, applied);
    EXPECT_NEAR(half.eye.x, feet.x + 0.14f, 1.0e-4f);
    EXPECT_FLOAT_EQ(half.eye.y, stand.eye.y);
    EXPECT_FLOAT_EQ(half.eye.z, stand.eye.z);
    EXPECT_FLOAT_EQ(applied, 0.5f);
}

TEST(ShoulderCamera, LeftLeanPassesTheUnleanedEye)
{
    const Vector3f     feet{ 0.0f, 0.0f, 0.0f };
    const Vector3f     pivot{ feet.x, feet.y + 1.15f, feet.z };
    const Vector3f     playRight = playBasis(0.0f, 0.0f, 0.0f, 0.0f).right;
    const ShoulderView stand     = solveShoulderCamera(feet, 0.0f, 0.0f, false, 0.0f, 0.0f, false, 0.0f, nullptr, nullptr);

    float              applied = 0.0f;
    const ShoulderView left    = leanShoulderEye(stand, pivot, playRight, -1.0f, nullptr, nullptr, applied);
    EXPECT_NEAR(left.eye.x, feet.x - 0.70f, 1.0e-4f);
    EXPECT_LT(left.eye.x, stand.eye.x);
    EXPECT_LT(left.eye.x, feet.x - kPlayBodyRadius);
    EXPECT_FLOAT_EQ(left.eye.y, stand.eye.y);
    EXPECT_NEAR(left.eye.z, feet.z - 2.4f, 1.0e-4f);
    EXPECT_NEAR(left.aim.x - stand.aim.x, left.eye.x - stand.eye.x, 1.0e-5f);
    EXPECT_FLOAT_EQ(left.aim.y, stand.aim.y);
    EXPECT_FLOAT_EQ(left.aim.z, stand.aim.z);
    EXPECT_FLOAT_EQ(applied, -1.0f);
}

TEST(ShoulderCamera, PlaneStopsAtSkin)
{
    const Vector3f     feet{ 0.0f, 0.0f, 0.0f };
    const Vector3f     pivot{ feet.x, feet.y + 1.15f, feet.z };
    const Vector3f     playRight = playBasis(0.0f, 0.0f, 0.0f, 0.0f).right;
    const ShoulderView stand     = solveShoulderCamera(feet, 0.0f, 0.0f, false, 0.0f, 0.0f, false, 0.0f, nullptr, nullptr);

    LateralPlane plane;
    plane.x = feet.x + 0.45f;
    float              applied = 0.0f;
    const ShoulderView leaned  = leanShoulderEye(stand, pivot, playRight, 1.0f, &lateralPlaneRay, &plane, applied);
    EXPECT_NEAR(leaned.eye.x, feet.x + 0.33f, 1.0e-4f);
    EXPECT_FLOAT_EQ(leaned.eye.y, stand.eye.y);
    EXPECT_FLOAT_EQ(leaned.eye.z, stand.eye.z);
    EXPECT_NEAR(leaned.eye.z, feet.z - 2.4f, 1.0e-4f);
    EXPECT_NEAR(leaned.aim.x - stand.aim.x, leaned.eye.x - stand.eye.x, 1.0e-5f);
    EXPECT_FLOAT_EQ(leaned.aim.z, stand.aim.z);
    EXPECT_NEAR(applied, 0.75f / 1.12f, 1.0e-3f);
    EXPECT_GT(std::fabs(applied - 1.0f), 0.2f);
}

TEST(ShoulderCamera, LeftPlaneDoesNotRetract)
{
    const Vector3f     feet{ 0.0f, 0.0f, 0.0f };
    const Vector3f     pivot{ feet.x, feet.y + 1.15f, feet.z };
    const Vector3f     playRight = playBasis(0.0f, 0.0f, 0.0f, 0.0f).right;
    const ShoulderView stand     = solveShoulderCamera(feet, 0.0f, 0.0f, false, 0.0f, 0.0f, false, 0.0f, nullptr, nullptr);

    // Skin would sit at -0.33, inward of the unleaned eye. The eye stays on the boom.
    LateralPlane plane;
    plane.x = feet.x - 0.45f;
    float              applied = 1.0f;
    const ShoulderView leaned  = leanShoulderEye(stand, pivot, playRight, -1.0f, &lateralPlaneRay, &plane, applied);
    EXPECT_FLOAT_EQ(leaned.eye.x, stand.eye.x);
    EXPECT_FLOAT_EQ(leaned.eye.y, stand.eye.y);
    EXPECT_FLOAT_EQ(leaned.eye.z, stand.eye.z);
    EXPECT_NEAR(leaned.eye.x, feet.x - 0.42f, 1.0e-4f);
    EXPECT_NEAR(leaned.eye.z, feet.z - 2.4f, 1.0e-4f);
    EXPECT_FLOAT_EQ(leaned.aim.x, stand.aim.x);
    EXPECT_FLOAT_EQ(leaned.aim.y, stand.aim.y);
    EXPECT_FLOAT_EQ(leaned.aim.z, stand.aim.z);
    EXPECT_FLOAT_EQ(applied, 0.0f);
}

TEST(ShoulderCamera, ShortenedBoomLeftLeanStillPasses)
{
    const Vector3f     feet{ 0.0f, 0.0f, 0.0f };
    const Vector3f     pivot{ feet.x, feet.y + 1.15f, feet.z };
    const Vector3f     playRight  = playBasis(0.0f, 0.0f, 0.0f, 0.0f).right;
    const ShoulderView shortened = solveShoulderCamera(feet, 0.0f, 0.0f, false, 0.0f, 0.0f, false, 0.0f, &minBoomRay, nullptr);
    EXPECT_NEAR((shortened.eye - pivot).Magnitude(), 0.28f, 1.0e-4f);

    float              applied = 0.0f;
    const ShoulderView leaned  = leanShoulderEye(shortened, pivot, playRight, -1.0f, nullptr, nullptr, applied);
    EXPECT_NEAR(leaned.eye.x, feet.x - 0.70f, 1.0e-4f);
    EXPECT_LT(leaned.eye.x, shortened.eye.x);
    EXPECT_GT(std::fabs(leaned.eye.x - feet.x), std::fabs(shortened.eye.x - feet.x));
    EXPECT_FLOAT_EQ(leaned.eye.y, shortened.eye.y);
    EXPECT_FLOAT_EQ(leaned.eye.z, shortened.eye.z);
    EXPECT_FLOAT_EQ(applied, -1.0f);
}

TEST(ContactGrind, ProximityDoesNotSpendParry)
{
    World world;
    Entity player = world.createEntity();
    world.emplace<TransformComponent>(player, Vector3f{ 0.0f, 0.0f, 0.0f });
    HealthComponent playerHp{};
    playerHp.health = Health{ HealthSettings{ 100.0f, 0.0f, 99.0f } };
    world.emplace<HealthComponent>(player, std::move(playerHp));

    Combat::DefenseComponent defense{};
    defense.blockArcDeg = 120.0f;
    defense.facingYawRad = 0.0f;
    defense.beginParryWindow(0.22f);
    defense.chargedParry = true;
    defense.parryStunSeconds = 0.90f;
    defense.chargedParryStunSeconds = 1.80f;
    world.emplace<Combat::DefenseComponent>(player, defense);

    Entity hunter = world.createEntity();
    world.emplace<AiAgentComponent>(hunter);
    world.emplace<TransformComponent>(hunter, Vector3f{ 0.0f, 0.0f, 1.0f });
    HealthComponent hunterHp{};
    hunterHp.health = Health{ HealthSettings{ 40.0f, 0.0f, 99.0f } };
    world.emplace<HealthComponent>(hunter, std::move(hunterHp));
    world.emplace<Combat::StatusEffectComponent>(hunter);
    world.emplace<HitReactionComponent>(hunter);

    Entity distant = world.createEntity();
    world.emplace<AiAgentComponent>(distant);
    world.emplace<TransformComponent>(distant, Vector3f{ 0.0f, 0.0f, 4.0f });
    HealthComponent distantHp{};
    distantHp.health = Health{ HealthSettings{ 40.0f, 0.0f, 99.0f } };
    world.emplace<HealthComponent>(distant, std::move(distantHp));

    Entity dead = world.createEntity();
    world.emplace<AiAgentComponent>(dead);
    world.emplace<TransformComponent>(dead, Vector3f{ 0.5f, 0.0f, 0.0f });
    HealthComponent deadHp{};
    deadHp.health = Health{ HealthSettings{ 40.0f, 0.0f, 99.0f } };
    deadHp.health.applyDamage(40.0f);
    world.emplace<HealthComponent>(dead, std::move(deadHp));

    Combat::CombatSystem combat;
    const float dt = 0.1f;
    const ContactGrindResult grind = applyContactGrind(world, combat, player, Vector3f{ 0.0f, 0.0f, 0.0f }, dt);

    HealthComponent* hp = world.get<HealthComponent>(player);
    ASSERT_NE(hp, nullptr);
    EXPECT_TRUE(grind.damaged);
    EXPECT_FALSE(grind.killed);
    EXPECT_NEAR(hp->health.hp(), 100.0f - kContactDps * dt, 1.0e-4f);
    EXPECT_NEAR(grind.away.x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(grind.away.z, -1.0f, 1.0e-4f);

    Combat::DefenseComponent* live = world.get<Combat::DefenseComponent>(player);
    ASSERT_NE(live, nullptr);
    EXPECT_TRUE(live->inParryWindow());
    EXPECT_TRUE(live->chargedParry);

    Combat::StatusEffectComponent* st = world.get<Combat::StatusEffectComponent>(hunter);
    ASSERT_NE(st, nullptr);
    EXPECT_FALSE(st->has(Combat::StatusId::Stun));
    EXPECT_FALSE(st->knockedDown());
    HitReactionComponent* hit = world.get<HitReactionComponent>(hunter);
    ASSERT_NE(hit, nullptr);
    EXPECT_FALSE(hit->hit.stunned());
}
