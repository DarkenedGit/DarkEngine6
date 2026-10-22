#include <gtest/gtest.h>

#include <cmath>

#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
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
    EXPECT_NEAR(ai.awarenessSettings().alertShareRadius, 12.0f, 1.0e-3f);

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

    Physics::PhysicsWorld physics;
    PlayerMotorInput stopped{};
    bool             stoppedLatch = false;
    buildPlayMotorInput(stopped, stoppedLatch, basis.wish, basis.flat, basis.right, false, false, false, false, nullptr, false, edges);
    EXPECT_FLOAT_EQ(stopped.wish.x, 0.0f);
    EXPECT_FLOAT_EQ(stopped.wish.y, 0.0f);
    EXPECT_FLOAT_EQ(stopped.wish.z, 0.0f);

    EXPECT_FLOAT_EQ(tightenPlanarClip(physics, Physics::kNullPhysicsBody, Physics::kNullPhysicsBody, Vector3f{ 0.0f, 0.0f, 0.0f }, Vector3f{ 1.0f, 0.0f, 0.0f }, 0.4f), 0.4f);

    Vector3f position{ 0.0f, 0.0f, 0.0f };
    Vector3f before{ 0.0f, 0.0f, 0.0f };
    Vector3f delta{ 2.0f, 0.0f, 0.0f };
    commitPlanarClip(position, before, delta, 0.5f, 1.0f, nullptr);
    EXPECT_NEAR(position.x, 0.96f, 1.0e-5f);
    EXPECT_FLOAT_EQ(position.z, 0.0f);

    PlayerMotor motor;
    Vector3f    moved{ 0.0f, 5.0f, 0.0f };
    Vector3f    start{ 1.0f, 5.0f, 2.0f };
    Vector3f    step{ 2.0f, 0.0f, 0.0f };
    commitPlanarClip(moved, start, step, 0.5f, 0.25f, &motor);
    EXPECT_NEAR(moved.x, 1.96f, 1.0e-5f);
    EXPECT_FLOAT_EQ(moved.y, 5.0f);
    EXPECT_FLOAT_EQ(moved.z, 2.0f);
    EXPECT_NEAR(motor.velocity().x, 3.84f, 1.0e-4f);
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
    EXPECT_NEAR(crouch.eye.y, 1.06f, 1.0e-4f);
    EXPECT_NEAR(crouch.aim.y, 0.78f, 1.0e-4f);

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
