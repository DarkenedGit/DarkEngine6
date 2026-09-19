#include <gtest/gtest.h>

#include "AI/AiSystem.h"
#include "AI/Brain.h"
#include "Assets/AssetManager.h"
#include "Character/Health.h"
#include "Character/HealthComponent.h"
#include "Combat/DamageEvent.h"
#include "Combat/JumpAttack.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/PoiseComponent.h"
#include "Combat/StatusEffectComponent.h"
#include "Core/AssetPinTable.h"
#include "ECS/World.h"
#include "Math/AABox3f.h"
#include "Math/MathHelper.h"
#include "Terrain/HeightMap.h"
#include "Terrain/Terrain.h"
#include "Weapons/HittableComponent.h"
#include "Weapons/Weapon.h"

using Dark::AiAgentComponent;
using Dark::AiSystem;
using Dark::AssetManager;
using Dark::AssetPinTable;
using Dark::BrainComponent;
using Dark::Entity;
using Dark::HealthComponent;
using Dark::HitReactionComponent;
using Dark::HittableComponent;
using Dark::JumpAttackComponent;
using Dark::PathAgentComponent;
using Dark::TransformComponent;
using Dark::World;
using Dark::Combat::JumpAttackCancel;
using Dark::Combat::JumpAttackPhase;
using Dark::Math::AABox3f;
using Dark::Math::Vector3f;

namespace
{
    constexpr float kDt = 1.0f / 60.0f;

    Dark::Terrain::HeightMap makeFlat(int samples, float y)
    {
        Dark::Terrain::HeightMap hm;
        EXPECT_TRUE(hm.create(static_cast<uint32_t>(samples), static_cast<uint32_t>(samples), 1.0f, 1.0f));
        hm.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
        for (int z = 0; z < samples; ++z)
        {
            for (int x = 0; x < samples; ++x)
                hm.setHeight(x, z, y);
        }
        return hm;
    }

    struct Harness
    {
        World                       world;
        AssetManager                assets;
        AssetPinTable               pins;
        AiSystem                    ai;
        Dark::Terrain::TerrainWorld terrain;
        Entity                      player{};

        bool setup(float waterY = -100.0f, const AABox3f* cubes = nullptr, int cubeCount = 0)
        {
            Dark::Terrain::TerrainDesc desc;
            desc.heightMap  = makeFlat(17, 0.0f);
            desc.chunkCells = 16;
            if (!terrain.create(std::move(desc)))
                return false;
            Dark::AI::WalkabilityDesc walk;
            walk.heightMap   = &terrain.heightMap();
            walk.waterLevel  = waterY;
            walk.agentRadius = 0.8f;
            walk.cubes       = cubes;
            walk.cubeCount   = cubeCount;
            if (!ai.bake(walk))
                return false;
            player = spawnPlayer(14.0f, 8.0f);
            return player.valid();
        }

        Entity spawnPlayer(float x, float z)
        {
            Entity e = world.createEntity();
            TransformComponent xf{};
            xf.position = Vector3f{ x, 1.0f, z };
            world.emplace<TransformComponent>(e, xf);
            HealthComponent hc{};
            hc.health = Dark::Health{};
            world.emplace<HealthComponent>(e, std::move(hc));
            world.emplace<HitReactionComponent>(e);
            HittableComponent hit{};
            hit.halfExtents = Vector3f{ 0.35f, 0.7f, 0.35f };
            world.emplace<HittableComponent>(e, hit);
            world.emplace<Dark::Combat::StatusEffectComponent>(e);
            world.emplace<Dark::Combat::PoiseComponent>(e);
            return e;
        }

        Entity spawnHunterAt(float x, float z)
        {
            TransformComponent xf{};
            xf.position = Vector3f{ x, 1.0f, z };
            Entity e    = ai.spawnHunter(world, pins, assets, xf);
            if (!e.valid())
                return {};
            if (AiAgentComponent* agent = world.get<AiAgentComponent>(e))
            {
                const TransformComponent* px = world.get<TransformComponent>(player);
                Vector3f                  look{ px->position.x - x, 0.0f, px->position.z - z };
                if (look.MagnitudeSqrd() > 1.0e-6f)
                    look.Normalize();
                else
                    look = Vector3f{ 1.0f, 0.0f, 0.0f };
                agent->forward = look;
            }
            return e;
        }

        void placePlayer(float x, float z)
        {
            if (TransformComponent* xf = world.get<TransformComponent>(player))
                xf->position = Vector3f{ x, 1.0f, z };
        }

        void tick(float dt = kDt, bool playerInWater = false)
        {
            ai.tickHunters(world, terrain, playerInWater, dt, player);
        }

        void tickFor(float seconds, bool playerInWater = false)
        {
            float t = 0.0f;
            while (t + 1.0e-8f < seconds)
            {
                const float step = Dark::Math::Min(kDt, seconds - t);
                tick(step, playerInWater);
                t += step;
            }
        }

        JumpAttackComponent* jac(Entity e) { return world.get<JumpAttackComponent>(e); }

        Dark::Combat::JumpAttack* jump(Entity e)
        {
            JumpAttackComponent* j = jac(e);
            return j ? &j->jump : nullptr;
        }

        bool tickUntilAir(Entity e, float maxSeconds = 1.0f)
        {
            float t = 0.0f;
            while (t < maxSeconds)
            {
                tick();
                if (jump(e) && jump(e)->inAirCommit())
                    return true;
                t += kDt;
            }
            return false;
        }

        void resetHunterPose(Entity e, float x, float z)
        {
            if (TransformComponent* xf = world.get<TransformComponent>(e))
                xf->position = Vector3f{ x, 1.0f, z };
            if (AiAgentComponent* agent = world.get<AiAgentComponent>(e))
            {
                const TransformComponent* px = world.get<TransformComponent>(player);
                Vector3f                  look{ px->position.x - x, 0.0f, px->position.z - z };
                if (look.MagnitudeSqrd() > 1.0e-6f)
                    look.Normalize();
                else
                    look = Vector3f{ 1.0f, 0.0f, 0.0f };
                agent->forward = look;
            }
            if (PathAgentComponent* path = world.get<PathAgentComponent>(e))
            {
                path->path.points.clear();
                path->waypoint = 0;
            }
        }
    };
} // namespace

TEST(HunterJumpAttack, SpawnUsesHunterDef)
{
    Harness h;
    ASSERT_TRUE(h.setup());
    Entity e = h.spawnHunterAt(8.0f, 8.0f);
    ASSERT_TRUE(e.valid());
    ASSERT_NE(h.jump(e), nullptr);
    const auto& def = h.jump(e)->def();
    EXPECT_NEAR(def.telegraphSeconds, 0.40f, 1.0e-4f);
    EXPECT_NEAR(def.cooldown, 3.5f, 1.0e-4f);
    EXPECT_NEAR(def.groundOffset, 1.0f, 1.0e-4f);
    EXPECT_NEAR(def.connectDamage, 22.0f, 1.0e-4f);
    EXPECT_NEAR(def.poundDamage, 14.0f, 1.0e-4f);
}

TEST(HunterJumpAttack, RangeBandInclusive)
{
    auto tryAt = [](float hunterX, float playerX, bool wantBegin) {
        Harness h;
        ASSERT_TRUE(h.setup());
        h.placePlayer(playerX, 8.0f);
        Entity e = h.spawnHunterAt(hunterX, 8.0f);
        ASSERT_TRUE(e.valid());
        h.tick();
        if (wantBegin)
        {
            EXPECT_EQ(h.jump(e)->phase(), JumpAttackPhase::Telegraph) << "dist " << (playerX - hunterX);
            EXPECT_EQ(h.ai.jumpAttackToken().id(), e.id());
        }
        else
        {
            EXPECT_EQ(h.jump(e)->phase(), JumpAttackPhase::Idle) << "dist " << (playerX - hunterX);
            EXPECT_FALSE(h.ai.jumpAttackToken().valid());
        }
    };
    tryAt(8.0f, 12.0f, true);  // 4.0
    tryAt(5.0f, 14.0f, true);  // 9.0
    tryAt(8.0f, 11.5f, false); // 3.5
    tryAt(4.0f, 14.0f, false); // 10.0
}

TEST(HunterJumpAttack, PackTokenOneJumper)
{
    Harness h;
    ASSERT_TRUE(h.setup());
    h.placePlayer(14.0f, 8.0f);
    Entity a = h.spawnHunterAt(8.0f, 8.0f);
    Entity b = h.spawnHunterAt(8.0f, 10.0f);
    ASSERT_TRUE(a.valid());
    ASSERT_TRUE(b.valid());
    h.tick();
    EXPECT_EQ(h.jump(a)->phase(), JumpAttackPhase::Telegraph);
    EXPECT_EQ(h.jump(b)->phase(), JumpAttackPhase::Idle);
    EXPECT_EQ(h.ai.jumpAttackToken().id(), a.id());

    h.tickFor(0.40f);
    EXPECT_TRUE(h.jump(a)->busy());
    EXPECT_NE(h.jump(a)->phase(), JumpAttackPhase::Idle);
    h.tick();
    EXPECT_EQ(h.jump(b)->phase(), JumpAttackPhase::Idle);
    EXPECT_EQ(h.ai.jumpAttackToken().id(), a.id());
}

TEST(HunterJumpAttack, RecoverHoldsTokenUntilIdle)
{
    Harness h;
    ASSERT_TRUE(h.setup());
    Entity a = h.spawnHunterAt(8.0f, 8.0f);
    Entity b = h.spawnHunterAt(8.0f, 10.0f);
    ASSERT_TRUE(a.valid() && b.valid());
    h.tick();
    ASSERT_EQ(h.jump(a)->phase(), JumpAttackPhase::Telegraph);
    ASSERT_TRUE(h.tickUntilAir(a));

    h.jump(a)->onLanded(h.world.get<TransformComponent>(a)->position);
    Dark::Combat::DamageEvent buf{};
    Dark::WeaponWorldQuery    empty{};
    h.jump(a)->tryPound(empty, h.world.get<TransformComponent>(a)->position, &buf, 1);
    EXPECT_EQ(h.jump(a)->phase(), JumpAttackPhase::Recover);
    EXPECT_TRUE(h.jump(a)->busy());

    h.tick();
    EXPECT_EQ(h.jump(a)->phase(), JumpAttackPhase::Recover);
    EXPECT_EQ(h.jump(b)->phase(), JumpAttackPhase::Idle);
    EXPECT_EQ(h.ai.jumpAttackToken().id(), a.id());
}

TEST(HunterJumpAttack, CooldownBlocksSecondBegin)
{
    Harness h;
    ASSERT_TRUE(h.setup());
    Entity e = h.spawnHunterAt(8.0f, 8.0f);
    ASSERT_TRUE(e.valid());
    h.tick();
    ASSERT_EQ(h.jump(e)->phase(), JumpAttackPhase::Telegraph);
    EXPECT_NEAR(h.jump(e)->cooldownLeft(), 3.5f, 0.02f);
    h.jump(e)->cancel(JumpAttackCancel::NoPound);
    EXPECT_EQ(h.jump(e)->phase(), JumpAttackPhase::Idle);

    h.tick(0.10f);
    EXPECT_EQ(h.jump(e)->phase(), JumpAttackPhase::Idle);
    EXPECT_GT(h.jump(e)->cooldownLeft(), 3.0f);
    EXPECT_FALSE(h.ai.jumpAttackToken().valid());

    h.tickFor(3.6f);
    EXPECT_LE(h.jump(e)->cooldownLeft(), 0.0f);
    h.resetHunterPose(e, 8.0f, 8.0f);
    h.tick();
    EXPECT_EQ(h.jump(e)->phase(), JumpAttackPhase::Telegraph);
    EXPECT_EQ(h.ai.jumpAttackToken().id(), e.id());
}

TEST(HunterJumpAttack, DeathClearsToken)
{
    Harness h;
    ASSERT_TRUE(h.setup());
    Entity e = h.spawnHunterAt(8.0f, 8.0f);
    ASSERT_TRUE(e.valid());
    h.tick();
    ASSERT_EQ(h.jump(e)->phase(), JumpAttackPhase::Telegraph);
    EXPECT_EQ(h.ai.jumpAttackToken().id(), e.id());

    EXPECT_TRUE(h.world.get<HealthComponent>(e)->health.applyDamage(200.0f));
    EXPECT_TRUE(h.world.get<HealthComponent>(e)->health.dead());
    EXPECT_EQ(h.jump(e)->phase(), JumpAttackPhase::Telegraph);

    h.tick();
    EXPECT_EQ(h.jump(e)->phase(), JumpAttackPhase::Idle);
    EXPECT_FALSE(h.ai.jumpAttackToken().valid());
}

TEST(HunterJumpAttack, ApplyHunterDamageDeathClearsToken)
{
    Harness h;
    ASSERT_TRUE(h.setup());
    Entity e = h.spawnHunterAt(8.0f, 8.0f);
    ASSERT_TRUE(e.valid());
    h.tick();
    ASSERT_EQ(h.jump(e)->phase(), JumpAttackPhase::Telegraph);
    EXPECT_TRUE(h.ai.applyHunterDamage(h.world, e, 200.0f));
    EXPECT_EQ(h.jump(e)->phase(), JumpAttackPhase::Idle);
    EXPECT_FALSE(h.ai.jumpAttackToken().valid());
}

TEST(HunterJumpAttack, RecoverToIdleWhileCcClearsToken)
{
    Harness h;
    ASSERT_TRUE(h.setup());
    Entity e = h.spawnHunterAt(8.0f, 8.0f);
    ASSERT_TRUE(e.valid());
    h.tick();
    ASSERT_EQ(h.jump(e)->phase(), JumpAttackPhase::Telegraph);
    ASSERT_TRUE(h.tickUntilAir(e));

    h.jump(e)->onLanded(h.world.get<TransformComponent>(e)->position);
    Dark::Combat::DamageEvent buf{};
    Dark::WeaponWorldQuery    empty{};
    h.jump(e)->tryPound(empty, h.world.get<TransformComponent>(e)->position, &buf, 1);
    ASSERT_EQ(h.jump(e)->phase(), JumpAttackPhase::Recover);

    Dark::Combat::StatusEffectComponent* st = h.world.get<Dark::Combat::StatusEffectComponent>(e);
    ASSERT_NE(st, nullptr);
    EXPECT_GT(st->applyCc(Dark::Combat::CcCategory::Knockdown, 1.4f, true, 0, 0.0f), 0.0f);
    EXPECT_TRUE(st->hasHardCc());
    EXPECT_EQ(h.ai.jumpAttackToken().id(), e.id());

    h.tickFor(0.45f);
    EXPECT_EQ(h.jump(e)->phase(), JumpAttackPhase::Idle);
    EXPECT_FALSE(h.ai.jumpAttackToken().valid());
    EXPECT_TRUE(st->hasHardCc());
    EXPECT_EQ(h.jump(e)->phase(), JumpAttackPhase::Idle);
}

TEST(HunterJumpAttack, WalkableBackStepOnLand)
{
    const AABox3f cube = AABox3f::FromCenterExtents(Vector3f{ 12.0f, 0.5f, 8.0f }, Vector3f{ 0.5f, 0.5f, 0.5f });
    Harness       h;
    ASSERT_TRUE(h.setup(-100.0f, &cube, 1));
    h.placePlayer(12.0f, 8.0f);
    Entity e = h.spawnHunterAt(8.0f, 8.0f);
    ASSERT_TRUE(e.valid());
    auto def         = h.jump(e)->def();
    def.homingRate   = 0.0f;
    h.jump(e)->setDef(def);

    EXPECT_FALSE(h.ai.walkability().walkableWorld(12.0f, 8.0f));
    h.tick();
    ASSERT_EQ(h.jump(e)->phase(), JumpAttackPhase::Telegraph);
    h.tickFor(1.5f);

    const TransformComponent* xf = h.world.get<TransformComponent>(e);
    ASSERT_NE(xf, nullptr);
    EXPECT_NE(h.jump(e)->phase(), JumpAttackPhase::Leap);
    EXPECT_NE(h.jump(e)->phase(), JumpAttackPhase::Connected);
    EXPECT_TRUE(h.ai.walkability().walkableWorld(xf->position.x, xf->position.z));
}

TEST(HunterJumpAttack, WetCancelsAndClearsToken)
{
    Harness h;
    ASSERT_TRUE(h.setup());
    Entity e = h.spawnHunterAt(8.0f, 8.0f);
    ASSERT_TRUE(e.valid());
    h.tick();
    ASSERT_EQ(h.jump(e)->phase(), JumpAttackPhase::Telegraph);
    h.tick(kDt, true);
    EXPECT_EQ(h.jump(e)->phase(), JumpAttackPhase::Idle);
    EXPECT_FALSE(h.ai.jumpAttackToken().valid());
}

TEST(HunterJumpAttack, FleeCancelsAndClearsToken)
{
    Harness h;
    ASSERT_TRUE(h.setup());
    Entity jumper = h.spawnHunterAt(8.0f, 8.0f);
    Entity dummy  = h.spawnHunterAt(10.0f, 8.0f);
    ASSERT_TRUE(jumper.valid() && dummy.valid());
    h.tick();
    ASSERT_EQ(h.jump(jumper)->phase(), JumpAttackPhase::Telegraph);

    h.world.get<HealthComponent>(dummy)->health.applyDamage(200.0f);
    h.ai.onHunterKilled(h.world, dummy);
    EXPECT_EQ(h.world.get<BrainComponent>(jumper)->brain->leaf(), Dark::AI::Leaf::Flee);
    EXPECT_EQ(h.jump(jumper)->phase(), JumpAttackPhase::Idle);
    EXPECT_FALSE(h.ai.jumpAttackToken().valid());
}

TEST(HunterJumpAttack, SplashCancelsNoPound)
{
    Harness h;
    ASSERT_TRUE(h.setup(10.0f));
    h.placePlayer(14.0f, 8.0f);
    Entity e = h.spawnHunterAt(8.0f, 8.0f);
    ASSERT_TRUE(e.valid());
    const float playerHp = h.world.get<HealthComponent>(h.player)->health.hp();
    h.tick();
    ASSERT_EQ(h.jump(e)->phase(), JumpAttackPhase::Telegraph);
    h.tickFor(0.50f);
    EXPECT_EQ(h.jump(e)->phase(), JumpAttackPhase::Idle);
    EXPECT_FALSE(h.ai.jumpAttackToken().valid());
    EXPECT_NEAR(h.world.get<HealthComponent>(h.player)->health.hp(), playerHp, 1.0e-3f);
}

TEST(HunterJumpAttack, StunnedOrHardCcDoesNotBegin)
{
    Harness h;
    ASSERT_TRUE(h.setup());
    Entity stunned = h.spawnHunterAt(8.0f, 8.0f);
    ASSERT_TRUE(stunned.valid());
    h.ai.applyHunterHitReaction(h.world, stunned, Vector3f{ 1.0f, 0.0f, 0.0f });
    EXPECT_TRUE(h.world.get<HitReactionComponent>(stunned)->hit.stunned());
    h.tick();
    EXPECT_EQ(h.jump(stunned)->phase(), JumpAttackPhase::Idle);
    EXPECT_FALSE(h.ai.jumpAttackToken().valid());

    Harness h2;
    ASSERT_TRUE(h2.setup());
    Entity cc = h2.spawnHunterAt(8.0f, 8.0f);
    ASSERT_TRUE(cc.valid());
    h2.world.get<Dark::Combat::StatusEffectComponent>(cc)->applyCc(Dark::Combat::CcCategory::Stun, 1.0f, true, 0, 0.0f);
    h2.tick();
    EXPECT_EQ(h2.jump(cc)->phase(), JumpAttackPhase::Idle);
    EXPECT_FALSE(h2.ai.jumpAttackToken().valid());
}

TEST(HunterJumpAttack, NoSeeDoesNotBegin)
{
    Harness h;
    ASSERT_TRUE(h.setup());
    Entity e = h.spawnHunterAt(8.0f, 8.0f);
    ASSERT_TRUE(e.valid());
    h.world.get<AiAgentComponent>(e)->forward = Vector3f{ -1.0f, 0.0f, 0.0f };
    h.tick();
    EXPECT_EQ(h.jump(e)->phase(), JumpAttackPhase::Idle);
    EXPECT_EQ(h.world.get<BrainComponent>(e)->brain->leaf(), Dark::AI::Leaf::Wander);
    EXPECT_FALSE(h.ai.jumpAttackToken().valid());
}

TEST(HunterJumpAttack, MissingComponentWarnsAndSkips)
{
    Harness h;
    ASSERT_TRUE(h.setup());
    Entity e = h.spawnHunterAt(8.0f, 8.0f);
    ASSERT_TRUE(e.valid());
    h.world.remove<JumpAttackComponent>(e);
    EXPECT_FALSE(h.world.has<JumpAttackComponent>(e));
    h.tick();
    EXPECT_FALSE(h.ai.jumpAttackToken().valid());
}

TEST(HunterJumpAttack, TelegraphSkipsFollowKeepsHitReaction)
{
    Harness h;
    ASSERT_TRUE(h.setup());
    Entity e = h.spawnHunterAt(8.0f, 8.0f);
    ASSERT_TRUE(e.valid());
    h.tick();
    ASSERT_EQ(h.jump(e)->phase(), JumpAttackPhase::Telegraph);
    const Vector3f start = h.world.get<TransformComponent>(e)->position;
    h.tickFor(0.30f);
    EXPECT_EQ(h.jump(e)->phase(), JumpAttackPhase::Telegraph);
    const Vector3f mid = h.world.get<TransformComponent>(e)->position;
    EXPECT_NEAR(mid.x, start.x, 0.05f);
    EXPECT_NEAR(mid.z, start.z, 0.05f);

    h.ai.applyHunterHitReaction(h.world, e, Vector3f{ 1.0f, 0.0f, 0.0f });
    EXPECT_TRUE(h.world.get<HitReactionComponent>(e)->hit.stunned());
}

TEST(HunterJumpAttack, InAirCommitSkipsHitReactionAndYSnap)
{
    Harness h;
    ASSERT_TRUE(h.setup());
    Entity e = h.spawnHunterAt(8.0f, 8.0f);
    ASSERT_TRUE(e.valid());
    h.tick();
    ASSERT_TRUE(h.tickUntilAir(e));
    const float y = h.world.get<TransformComponent>(e)->position.y;
    EXPECT_GT(y, 1.05f);

    h.ai.applyHunterHitReaction(h.world, e, Vector3f{ 1.0f, 0.0f, 0.0f });
    EXPECT_FALSE(h.world.get<HitReactionComponent>(e)->hit.stunned());
    h.tick();
    EXPECT_GT(h.world.get<TransformComponent>(e)->position.y, 1.05f);
    EXPECT_TRUE(h.world.get<Dark::Combat::PoiseComponent>(e)->hyperArmor);
}

TEST(HunterJumpAttack, LeapResolvesThroughCombatNotApplyHunterDamage)
{
    Harness h;
    ASSERT_TRUE(h.setup());
    Entity e = h.spawnHunterAt(8.0f, 8.0f);
    ASSERT_TRUE(e.valid());
    const float hunterHp = h.world.get<HealthComponent>(e)->health.hp();
    const float playerHp = h.world.get<HealthComponent>(h.player)->health.hp();
    h.tick();
    h.tickFor(1.5f);
    EXPECT_LT(h.world.get<HealthComponent>(h.player)->health.hp(), playerHp);
    EXPECT_NEAR(h.world.get<HealthComponent>(e)->health.hp(), hunterHp, 1.0e-3f);
    EXPECT_FALSE(h.world.get<HealthComponent>(e)->health.dead());
}
