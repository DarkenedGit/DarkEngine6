#include <gtest/gtest.h>

#include "AI/AiSystem.h"
#include "AI/AttackPattern.h"
#include "AI/HunterJump.h"
#include "AI/Pathfinder.h"
#include "AI/Walkability.h"
#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
#include "Character/DamageEntity.h"
#include "Character/Health.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Combat/CombatSystem.h"
#include "Combat/DefenseComponent.h"
#include "Combat/PlayerMode.h"
#include "Combat/PoiseComponent.h"
#include "Combat/StatusEffectComponent.h"
#include "Core/AssetPinTable.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Input/Input.h"
#include "Math/AABox3f.h"
#include "Math/Ray3f.h"
#include "Math/Sphere3f.h"
#include "Play/ContactGrind.h"
#include "Play/Facility.h"
#include "Play/Interaction.h"
#include "Play/PlaySession.h"
#include "Play/PlayerCombat.h"
#include "Play/WeaponTargets.h"
#include "Scene/Building.h"
#include "Scene/EntityCombat.h"
#include "Terrain/HeightMap.h"
#include "Terrain/TerrainGrid.h"
#include "Weapons/HittableComponent.h"
#include "Weapons/MeleeWeapon.h"
#include "Weapons/ProjectileWeapon.h"

#include <cmath>
#include <vector>

using namespace Dark;
using namespace Dark::Math;

namespace
{

    Terrain::HeightMap makeFlat(int samples, float y)
    {
        Terrain::HeightMap hm;
        EXPECT_TRUE(hm.create(static_cast<uint32_t>(samples), static_cast<uint32_t>(samples), 1.0f, 1.0f));
        hm.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
        for (int z = 0; z < samples; ++z)
        {
            for (int x = 0; x < samples; ++x)
                hm.setHeight(x, z, y);
        }
        return hm;
    }

    int countAlive(const std::vector<LiveProjectile>& shots)
    {
        int n = 0;
        for (const LiveProjectile& shot : shots)
        {
            if (shot.alive)
                ++n;
        }
        return n;
    }

    struct FlatRig
    {
        World              world;
        AssetManager       assets;
        AssetPinTable      pins;
        AiSystem           ai;
        Terrain::TerrainGrid terrain;
        Entity             player{};

        bool setup()
        {
            if (!terrain.createFromHeightMap(makeFlat(17, 0.0f), 16))
                return false;
            AI::WalkabilityDesc walk;
            walk.heightMap   = &terrain.coarse();
            walk.waterLevel  = -100.0f;
            walk.agentRadius = 0.8f;
            if (!ai.bake(walk))
                return false;
            ai.setAwarenessEnabled(false);
            player = world.createEntity();
            TransformComponent xf{};
            xf.position = Vector3f{ 4.0f, 1.0f, 4.0f };
            world.emplace<TransformComponent>(player, xf);
            world.emplace<HealthComponent>(player);
            return player.valid();
        }

        Entity spawnHunterAt(float x, float z)
        {
            TransformComponent xf{};
            xf.position = Vector3f{ x, 1.0f, z };
            return ai.spawnHunter(world, pins, assets, xf);
        }
    };

    void onHitCount(void* user, const WeaponHit&)
    {
        *static_cast<int*>(user) += 1;
    }

} // namespace

TEST(GameplayReview, EmptyPoolRestoreStillFires)
{
    ProjectileWeaponDesc desc;
    desc.instant  = false;
    desc.speed    = 42.0f;
    desc.cooldown = 0.0f;
    desc.maxLive  = 4;
    ProjectileWeapon weapon(desc);

    weapon.restoreShots({});
    EXPECT_EQ(weapon.live().size(), 4u);
    EXPECT_EQ(countAlive(weapon.live()), 0);

    WeaponFireRequest req;
    req.origin    = Vector3f{ 0.0f, 1.0f, 0.0f };
    req.direction = Vector3f{ 0.0f, 0.0f, 1.0f };
    ASSERT_TRUE(weapon.fire(req, {}));
    EXPECT_EQ(weapon.live().size(), 4u);
    EXPECT_EQ(countAlive(weapon.live()), 1);
}

TEST(GameplayReview, CombatFilesKeepSeedsAndLoadSidecars)
{
    EntityCombat bad = entityCombatDefault("player");
    EXPECT_FALSE(parseEntityCombat("{", bad));
    EXPECT_FLOAT_EQ(bad.healthMax, 100.0f);
    EXPECT_EQ(bad.team, 1);

    EntityCombat partial = entityCombatDefault("hunter");
    partial.team = 7;
    ASSERT_TRUE(parseEntityCombat(R"({"version":1,"type":"hunter","healthMax":11})", partial));
    EXPECT_FLOAT_EQ(partial.healthMax, 11.0f);
    EXPECT_EQ(partial.team, 7);

    EntityCombat hunter = entityCombatDefault("hunter");
    ASSERT_TRUE(loadEntityCombatType("hunter", hunter));
    EXPECT_FLOAT_EQ(hunter.healthMax, 48.0f);
    EXPECT_FLOAT_EQ(hunter.respawnSeconds, 8.0f);
    EXPECT_EQ(hunter.team, 2);
    EXPECT_FLOAT_EQ(hunter.jumpKnockdown, 0.8f);
    EXPECT_FLOAT_EQ(hunter.jumpConnectPoise, 28.0f);

    EntityCombat wolf = entityCombatDefault("wolf");
    ASSERT_TRUE(loadEntityCombatType("wolf", wolf));
    EXPECT_EQ(wolf.team, 2);
    EXPECT_FLOAT_EQ(wolf.jumpKnockdown, 0.8f);
    EXPECT_FLOAT_EQ(wolf.jumpConnectPoise, 28.0f);

    EntityCombat player = entityCombatDefault("player");
    ASSERT_TRUE(loadEntityCombatType("player", player));
    EXPECT_EQ(player.team, 1);
    EXPECT_FLOAT_EQ(player.healthMax, 100.0f);
    EXPECT_FLOAT_EQ(player.drownDps, 8.0f);
    EXPECT_FLOAT_EQ(player.jumpConnectDamage, 32.0f);
    EXPECT_FLOAT_EQ(player.weaponRange, 90.0f);
}

TEST(GameplayReview, WolfPounceKeepsCombatKnockdownAndPoise)
{
    const AI::AttackPattern pattern = AI::loadAttackPattern("ai/wolf.attacks.json");
    ASSERT_TRUE(pattern.loaded);
    EXPECT_EQ(pattern.id, "wolf");

    const Combat::JumpAttackDef def = hunterJumpDefFromPattern(&pattern);
    EXPECT_FLOAT_EQ(def.telegraphSeconds, 0.28f);
    EXPECT_FLOAT_EQ(def.connectDamage, 16.0f);
    EXPECT_FLOAT_EQ(def.knockdownDuration, 0.8f);
    EXPECT_FLOAT_EQ(def.connectPoise, 28.0f);
    EXPECT_EQ(def.teamFilter, 2);
}

TEST(GameplayReview, PoiseRegensAfterTheDelayAndResetHolds)
{
    Combat::PoiseComponent poise;
    poise.maxPoise    = 100.0f;
    poise.poise       = 40.0f;
    poise.regenPerSec = 25.0f;
    poise.regenDelay  = 1.5f;
    poise.sinceDamage = 0.0f;

    poise.tick(1.0f);
    EXPECT_FLOAT_EQ(poise.poise, 40.0f);
    // The step that reaches the delay regens for the whole step.
    poise.tick(0.5f);
    EXPECT_NEAR(poise.poise, 52.5f, 1.0e-3f);
    poise.tick(0.1f);
    EXPECT_NEAR(poise.poise, 55.0f, 1.0e-3f);

    poise.reset();
    EXPECT_FLOAT_EQ(poise.poise, poise.maxPoise);
    EXPECT_FLOAT_EQ(poise.sinceDamage, poise.regenDelay);
    poise.tick(0.2f);
    EXPECT_FLOAT_EQ(poise.poise, poise.maxPoise);
}

TEST(GameplayReview, RespawnReturnsHomeAndResetsPoise)
{
    FlatRig rig;
    ASSERT_TRUE(rig.setup());
    const Entity hunter = rig.spawnHunterAt(4.0f, 4.0f);
    ASSERT_TRUE(hunter.valid());

    AiAgentComponent* agent = rig.world.get<AiAgentComponent>(hunter);
    ASSERT_NE(agent, nullptr);
    EXPECT_TRUE(agent->hasHome);
    EXPECT_NEAR(agent->home.x, 4.0f, 1.0e-3f);
    EXPECT_NEAR(agent->home.z, 4.0f, 1.0e-3f);

    Combat::PoiseComponent* poise = rig.world.get<Combat::PoiseComponent>(hunter);
    ASSERT_NE(poise, nullptr);
    poise->applyPoiseDamage(80.0f);
    EXPECT_LT(poise->poise, poise->maxPoise);

    TransformComponent* xf = rig.world.get<TransformComponent>(hunter);
    ASSERT_NE(xf, nullptr);
    xf->position = Vector3f{ 12.0f, 1.0f, 4.0f };
    ASSERT_TRUE(rig.ai.applyHunterDamage(rig.world, hunter, 1000.0f));

    rig.ai.tickHunters(rig.world, rig.terrain, false, 8.0f, rig.player);
    EXPECT_TRUE(rig.world.get<HealthComponent>(hunter)->health.alive());
    EXPECT_NEAR(xf->position.x, 4.0f, 1.5f);
    EXPECT_NEAR(xf->position.z, 4.0f, 1.5f);
    EXPECT_FLOAT_EQ(poise->poise, poise->maxPoise);
    EXPECT_FLOAT_EQ(poise->sinceDamage, poise->regenDelay);
}

TEST(GameplayReview, PoundSkipsATeammateHitsACrowAndPackHitsDoNotAlert)
{
    FlatRig rig;
    ASSERT_TRUE(rig.setup());
    Audio::AudioSystem audio;

    const Entity victim = rig.spawnHunterAt(0.0f, 1.0f);
    const Entity witness = rig.spawnHunterAt(1.0f, 1.0f);
    ASSERT_TRUE(victim.valid());
    ASSERT_TRUE(witness.valid());

    const Entity crow = rig.world.createEntity();
    TransformComponent crowXf{};
    crowXf.position = Vector3f{ 2.0f, 1.0f, 1.0f };
    rig.world.emplace<TransformComponent>(crow, crowXf);
    rig.world.emplace<HittableComponent>(crow);
    rig.world.emplace<HealthComponent>(crow);

    const Entity packmate = rig.world.createEntity();
    rig.world.emplace<TransformComponent>(packmate);

    PlayHost host;
    host.world  = &rig.world;
    host.assets = &rig.assets;
    host.audio  = &audio;
    host.ai     = &rig.ai;
    host.player = rig.player;
    PlayRuntime          runtime;
    Combat::CombatSystem combat;

    HealthComponent* victimHp = rig.world.get<HealthComponent>(victim);
    HealthComponent* crowHp   = rig.world.get<HealthComponent>(crow);
    ASSERT_NE(victimHp, nullptr);
    ASSERT_NE(crowHp, nullptr);
    const float mateBefore = victimHp->health.hp();
    const float crowBefore = crowHp->health.hp();

    Combat::DamageEvent ev{};
    ev.source     = packmate;
    ev.target     = victim;
    ev.amount     = 14.0f;
    ev.teamSource = 2;
    ev.hitPoint   = Vector3f{ 0.0f, 1.0f, 1.0f };
    ev.hitDir     = Vector3f{ 0.0f, 0.0f, 1.0f };
    resolvePlayerJumpHits(host, runtime, combat, &ev, 1);
    EXPECT_FLOAT_EQ(victimHp->health.hp(), mateBefore);
    EXPECT_FLOAT_EQ(rig.world.get<AiAgentComponent>(witness)->assistLeft, 0.0f);

    ev.target = crow;
    ev.source = rig.player;
    resolvePlayerJumpHits(host, runtime, combat, &ev, 1);
    EXPECT_LT(crowHp->health.hp(), crowBefore);
    EXPECT_FLOAT_EQ(rig.world.get<AiAgentComponent>(witness)->assistLeft, 0.0f);

    rig.world.get<Combat::DefenseComponent>(victim)->team = 0;
    ev.target     = victim;
    ev.source     = packmate;
    ev.teamSource = 1;
    ev.amount     = 10.0f;
    const float openBefore = victimHp->health.hp();
    resolvePlayerJumpHits(host, runtime, combat, &ev, 1);
    EXPECT_LT(victimHp->health.hp(), openBefore);
    EXPECT_FLOAT_EQ(rig.world.get<AiAgentComponent>(witness)->assistLeft, 0.0f);

    victimHp->health.revive();
    ev.source = rig.player;
    const float alertBefore = victimHp->health.hp();
    resolvePlayerJumpHits(host, runtime, combat, &ev, 1);
    EXPECT_LT(victimHp->health.hp(), alertBefore);
    EXPECT_GT(rig.world.get<AiAgentComponent>(witness)->assistLeft, 0.0f);
}

TEST(GameplayReview, CubeSphereAndClosedDoorBlockTheRifle)
{
    World world;
    const Entity target = world.createEntity();
    TransformComponent xf{};
    xf.position = Vector3f{ 0.0f, 1.0f, 6.0f };
    world.emplace<TransformComponent>(target, xf);
    HittableComponent box{};
    box.halfExtents = Vector3f{ 0.4f, 0.8f, 0.4f };
    world.emplace<HittableComponent>(target, box);
    world.emplace<HealthComponent>(target);

    WeaponTargetList list;
    list.gather(world, {});
    WeaponWorldQuery query;
    list.bind(query);
    const Ray3f ray{ Vector3f{ 0.0f, 1.0f, 0.0f }, Vector3f{ 0.0f, 0.0f, 1.0f } };

    WeaponHit hit{};
    ASSERT_TRUE(weaponRaycastClosest(query, ray, 40.0f, hit));
    EXPECT_TRUE(hit.hitTarget);
    EXPECT_EQ(hit.targetEntity, target);

    const AABox3f cube = AABox3f::FromCenterExtents(Vector3f{ 0.0f, 1.0f, 3.0f }, Vector3f{ 0.6f, 0.6f, 0.2f });
    query.solids     = &cube;
    query.solidCount = 1;
    hit              = {};
    ASSERT_TRUE(weaponRaycastClosest(query, ray, 40.0f, hit));
    EXPECT_FALSE(hit.hitTarget);

    query.solids      = nullptr;
    query.solidCount  = 0;
    const Sphere3f sphere{ Vector3f{ 0.0f, 1.0f, 3.0f }, 0.6f };
    query.spheres     = &sphere;
    query.sphereCount = 1;
    hit               = {};
    ASSERT_TRUE(weaponRaycastClosest(query, ray, 40.0f, hit));
    EXPECT_FALSE(hit.hitTarget);

    query.spheres     = nullptr;
    query.sphereCount = 0;
    const Entity door = world.createEntity();
    DoorComponent leaf{};
    leaf.closedPos = Vector3f{ 0.0f, 1.0f, 3.0f };
    leaf.size      = Vector3f{ 2.0f, 2.0f, 0.4f };
    leaf.open      = 0.0f;
    world.emplace<DoorComponent>(door, leaf);

    FacilityState state;
    FacilityHost  facilityHost;
    facilityHost.world = &world;
    publishDoorBlocks(state, facilityHost);
    ASSERT_GT(state.blockCount, 0);
    query.solids     = state.blocks;
    query.solidCount = state.blockCount;
    hit              = {};
    ASSERT_TRUE(weaponRaycastClosest(query, ray, 40.0f, hit));
    EXPECT_FALSE(hit.hitTarget);

    world.get<DoorComponent>(door)->open = 0.9f;
    publishDoorBlocks(state, facilityHost);
    query.solids     = state.blocks;
    query.solidCount = state.blockCount;
    hit              = {};
    ASSERT_TRUE(weaponRaycastClosest(query, ray, 40.0f, hit));
    EXPECT_TRUE(hit.hitTarget);
    EXPECT_EQ(hit.targetEntity, target);
}

TEST(GameplayReview, MeleeAndGrindMissATargetFourMetersUp)
{
    World world;
    const Entity high = world.createEntity();
    TransformComponent xf{};
    xf.position = Vector3f{ 0.0f, 4.0f, 1.2f };
    world.emplace<TransformComponent>(high, xf);
    HittableComponent box{};
    box.halfExtents = Vector3f{ 0.3f, 0.3f, 0.3f };
    world.emplace<HittableComponent>(high, box);
    world.emplace<HealthComponent>(high);
    world.emplace<AiAgentComponent>(high);

    WeaponTargetList list;
    list.gather(world, {});
    WeaponWorldQuery query;
    list.bind(query);

    MeleeWeapon melee;
    int         hits = 0;
    melee.setHitListener(&onHitCount, &hits);
    WeaponFireRequest req;
    req.origin    = Vector3f{ 0.0f, 0.0f, 0.0f };
    req.ownerPos  = req.origin;
    req.direction = Vector3f{ 0.0f, 0.0f, 1.0f };
    ASSERT_TRUE(melee.fire(req, query));
    EXPECT_EQ(hits, 0);

    MeleeWeaponDesc desc = melee.desc();
    desc.maxVertical     = -1.0f;
    melee.setDesc(desc);
    melee.clear();
    ASSERT_TRUE(melee.fire(req, query));
    EXPECT_EQ(hits, 1);

    const Entity player = world.createEntity();
    world.emplace<HealthComponent>(player);
    Combat::CombatSystem combat;
    const Vector3f       playerPos{ 0.0f, 0.0f, 0.0f };
    ContactGrindResult   highGrind = applyContactGrind(world, combat, player, playerPos, 1.0f);
    EXPECT_FALSE(highGrind.damaged);
    EXPECT_FLOAT_EQ(world.get<HealthComponent>(player)->health.hp(), 100.0f);

    world.get<TransformComponent>(high)->position.y = 0.0f;
    ContactGrindResult level = applyContactGrind(world, combat, player, playerPos, 1.0f);
    EXPECT_TRUE(level.damaged);
    EXPECT_NEAR(world.get<HealthComponent>(player)->health.hp(), 100.0f - kContactDps, 1.0e-3f);

    world.get<HealthComponent>(player)->health.revive();
    HitReactionComponent hr{};
    hr.hit.apply(Vector3f{ 0.0f, 0.0f, 1.0f });
    world.emplace<HitReactionComponent>(high, std::move(hr));
    ContactGrindResult stunned = applyContactGrind(world, combat, player, playerPos, 1.0f);
    EXPECT_FALSE(stunned.damaged);
    EXPECT_FLOAT_EQ(world.get<HealthComponent>(player)->health.hp(), 100.0f);
}

TEST(GameplayReview, TakedownFailsWhileThePlayerIsStunned)
{
    struct Rig
    {
        World       world;
        Input       input;
        Entity      player{};
        PlaySession play;

        Rig()
        {
            player = world.createEntity();
            TransformComponent xf{};
            xf.position = Vector3f{ 0.0f, 0.0f, 0.0f };
            world.emplace<TransformComponent>(player, xf);
            world.emplace<HealthComponent>(player);
            world.emplace<PlayerMotorComponent>(player);
            world.emplace<HitReactionComponent>(player);
            play.bind(&Rig::fill, this);
        }

        static void fill(void* user, PlayHost& host)
        {
            Rig* self        = static_cast<Rig*>(user);
            host.world       = &self->world;
            host.input       = &self->input;
            host.player      = self->player;
            host.chaseReady  = true;
        }
    };

    Rig rig;
    const Entity guard = rig.world.createEntity();
    TransformComponent gxf{};
    gxf.position = Vector3f{ 0.0f, 0.0f, 1.5f };
    rig.world.emplace<TransformComponent>(guard, gxf);
    rig.world.emplace<HealthComponent>(guard);
    AiAgentComponent ai{};
    ai.forward = Vector3f{ 0.0f, 0.0f, 1.0f };
    rig.world.emplace<AiAgentComponent>(guard, ai);
    setInteractable(rig.world, guard, InteractKind::Guard, kInteractBodyRange);

    EXPECT_EQ(rig.play.takedownTarget(), guard);
    rig.world.get<HitReactionComponent>(rig.player)->hit.apply(Vector3f{ 0.0f, 0.0f, -1.0f });
    EXPECT_FALSE(rig.play.takedownTarget().valid());
    rig.world.get<HitReactionComponent>(rig.player)->hit.reset();
    EXPECT_EQ(rig.play.takedownTarget(), guard);

    Combat::StatusEffectComponent& status = rig.world.emplace<Combat::StatusEffectComponent>(rig.player);
    status.applyCc(Combat::CcCategory::Knockdown, 1.0f, true, 1, 1.0f);
    EXPECT_FALSE(rig.play.takedownTarget().valid());
    status.reset();
    EXPECT_EQ(rig.play.takedownTarget(), guard);

    const AABox3f wall = AABox3f::FromCenterExtents(Vector3f{ 0.0f, 1.2f, 0.75f }, Vector3f{ 0.6f, 1.2f, 0.2f });
    EXPECT_FALSE(findTakedownTarget(rig.world, rig.player, true, 2.5f, 1.2f, -0.2f, &wall, 1).valid());
    EXPECT_EQ(findTakedownTarget(rig.world, rig.player, true), guard);
}

TEST(GameplayReview, AgentInTheOnlyCorridorIsACostNotAWall)
{
    Terrain::HeightMap hm = makeFlat(11, 0.0f);
    for (int x = 0; x < 11; ++x)
    {
        hm.setHeight(x, 4, 10.0f);
        hm.setHeight(x, 5, 10.0f);
    }
    AI::Walkability walk;
    AI::WalkabilityDesc desc;
    desc.heightMap   = &hm;
    desc.waterLevel  = 5.0f;
    desc.agentRadius = 0.8f;
    ASSERT_TRUE(walk.bake(desc));
    ASSERT_TRUE(walk.walkableWorld(1.5f, 4.5f));
    ASSERT_TRUE(walk.walkableWorld(8.5f, 4.5f));
    ASSERT_FALSE(walk.walkableWorld(1.5f, 2.5f));

    AI::Pathfinder finder;
    ASSERT_TRUE(finder.bind(&walk));
    AI::AgentStamp body;
    body.x      = 5.5f;
    body.z      = 4.5f;
    body.radius = 0.8f;
    AI::PathRequest req;
    req.startX     = 1.5f;
    req.startZ     = 4.5f;
    req.destX      = 8.5f;
    req.destZ      = 4.5f;
    req.others     = &body;
    req.otherCount = 1;
    AI::PathResult out;
    ASSERT_TRUE(finder.find(req, out));
    ASSERT_FALSE(out.points.empty());

    bool passedTheBody = false;
    for (size_t i = 1; i < out.points.size(); ++i)
    {
        const Vector3f& a = out.points[i - 1];
        const Vector3f& b = out.points[i];
        const float     abx = b.x - a.x;
        const float     abz = b.z - a.z;
        const float     ab2 = abx * abx + abz * abz;
        float           t   = ab2 > 1.0e-8f ? ((body.x - a.x) * abx + (body.z - a.z) * abz) / ab2 : 0.0f;
        if (t < 0.0f)
            t = 0.0f;
        if (t > 1.0f)
            t = 1.0f;
        const float dx = a.x + abx * t - body.x;
        const float dz = a.z + abz * t - body.z;
        if (dx * dx + dz * dz < 1.2f * 1.2f)
            passedTheBody = true;
        EXPECT_NEAR(a.z, 4.5f, 0.6f);
    }
    EXPECT_TRUE(passedTheBody);
}

TEST(GameplayReview, DeadBodyDoesNotSealTheCorridor)
{
    Terrain::HeightMap hm = makeFlat(17, 0.0f);
    for (int x = 0; x < 17; ++x)
    {
        hm.setHeight(x, 8, 10.0f);
        hm.setHeight(x, 9, 10.0f);
    }
    Terrain::TerrainGrid terrain;
    ASSERT_TRUE(terrain.createFromHeightMap(std::move(hm), 16));

    World         world;
    AssetManager  assets;
    AssetPinTable pins;
    AiSystem      ai;
    AI::WalkabilityDesc walk;
    walk.heightMap   = &terrain.coarse();
    walk.waterLevel  = 5.0f;
    walk.agentRadius = 0.8f;
    ASSERT_TRUE(ai.bake(walk));
    ai.setAwarenessEnabled(false);

    const Entity player = world.createEntity();
    TransformComponent playerXf{};
    playerXf.position = Vector3f{ 14.5f, 11.0f, 8.5f };
    world.emplace<TransformComponent>(player, playerXf);
    world.emplace<HealthComponent>(player);

    TransformComponent chaserXf{};
    chaserXf.position = Vector3f{ 2.5f, 11.0f, 8.5f };
    const Entity chaser = ai.spawnHunter(world, pins, assets, chaserXf);
    ASSERT_TRUE(chaser.valid());
    if (AiAgentComponent* agent = world.get<AiAgentComponent>(chaser))
        agent->forward = Vector3f{ 1.0f, 0.0f, 0.0f };

    TransformComponent bodyXf{};
    bodyXf.position = Vector3f{ 8.5f, 11.0f, 8.5f };
    const Entity body = ai.spawnHunter(world, pins, assets, bodyXf);
    ASSERT_TRUE(body.valid());
    ASSERT_TRUE(ai.applyHunterDamage(world, body, 1000.0f));
    EXPECT_FALSE(world.get<HealthComponent>(body)->health.alive());

    const float startX = world.get<TransformComponent>(chaser)->position.x;
    for (int i = 0; i < 90; ++i)
        ai.tickHunters(world, terrain, false, 1.0f / 30.0f, player);

    const float endX = world.get<TransformComponent>(chaser)->position.x;
    EXPECT_GT(endX, startX + 1.0f);
    EXPECT_FALSE(world.get<AiAgentComponent>(chaser)->givenUp);
}

TEST(GameplayReview, GodModeBlocksWeaponPoundGrindAndDrowning)
{
    World              world;
    AssetManager       assets;
    Audio::AudioSystem audio;
    const Entity       player = world.createEntity();
    world.emplace<HealthComponent>(player);
    const Entity hunter = world.createEntity();
    TransformComponent hxf{};
    hxf.position = Vector3f{ 0.0f, 0.0f, 1.0f };
    world.emplace<TransformComponent>(hunter, hxf);
    world.emplace<HealthComponent>(hunter);
    world.emplace<AiAgentComponent>(hunter);

    Combat::CombatSystem combat;
    const Vector3f       playerPos{ 0.0f, 0.0f, 0.0f };
    ContactGrindResult   open = applyContactGrind(world, combat, player, playerPos, 1.0f);
    EXPECT_TRUE(open.damaged);
    EXPECT_LT(world.get<HealthComponent>(player)->health.hp(), 100.0f);
    world.get<HealthComponent>(player)->health.revive();

    Combat::PlayerModeComponent& mode = world.emplace<Combat::PlayerModeComponent>(player);
    mode.godMode = true;

    ContactGrindResult grind = applyContactGrind(world, combat, player, playerPos, 1.0f);
    EXPECT_FALSE(grind.damaged);

    PlayHost host;
    host.world  = &world;
    host.assets = &assets;
    host.audio  = &audio;
    host.player = player;
    PlayRuntime runtime;
    WeaponHit   hit;
    hit.hitTarget    = true;
    hit.targetEntity = player;
    hit.damage       = 25.0f;
    hit.weapon       = WeaponKind::Projectile;
    onPlayerWeaponHit(host, runtime, combat, hit);

    Combat::DamageEvent pound{};
    pound.source     = hunter;
    pound.target     = player;
    pound.amount     = 20.0f;
    pound.teamSource = 2;
    const Combat::ResolveResult resolved = combat.resolve(world, pound);
    EXPECT_TRUE(resolved.iframe);

    bool killed = true;
    EXPECT_FALSE(damageEntity(world, player, 8.0f, &killed));
    EXPECT_FALSE(killed);
    EXPECT_FLOAT_EQ(world.get<HealthComponent>(player)->health.hp(), 100.0f);
}
