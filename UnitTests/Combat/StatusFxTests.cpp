#include <gtest/gtest.h>

#include "Character/HealthComponent.h"
#include "Combat/CombatSystem.h"
#include "Combat/DamageEvent.h"
#include "Combat/JumpAttackDef.h"
#include "Combat/StatusDef.h"
#include "Combat/StatusEffectComponent.h"
#include "Combat/StatusFxEvent.h"
#include "Combat/StatusId.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Particles/ParticleComponents.h"
#include "Particles/ParticleTick.h"
#include "Particles/StatusFxDriver.h"
#include "Particles/StatusFxTag.h"
#include "Particles/StatusParticlePresets.h"

using namespace Dark;
using namespace Dark::Combat;
using namespace Dark::Math;

namespace
{
    int countStatusFx(World& world)
    {
        int n = 0;
        world.each<StatusFxTag>([&](Entity, StatusFxTag&) { ++n; });
        return n;
    }

    int countEmitters(World& world)
    {
        int n = 0;
        world.each<ParticleEmitterComponent>([&](Entity, ParticleEmitterComponent&) { ++n; });
        return n;
    }

    Entity firstStatusFx(World& world)
    {
        Entity found{};
        world.each<StatusFxTag>([&](Entity e, StatusFxTag&) {
            if (!found.valid())
                found = e;
        });
        return found;
    }

    Entity makePawn(World& world, const Vector3f& pos = Vector3f{ 1.0f, 0.0f, 2.0f })
    {
        Entity pawn = world.createEntity();
        TransformComponent xf{};
        xf.position = pos;
        world.emplace<TransformComponent>(pawn, xf);
        world.emplace<StatusEffectComponent>(pawn);
        HealthComponent hp{};
        hp.health = Health{ HealthSettings{ 100.0f, 0.0f, 99.0f } };
        world.emplace<HealthComponent>(pawn, std::move(hp));
        return pawn;
    }

    struct FxRecorder
    {
        StatusFxEvent evs[StatusEffectComponent::kMaxFxEvents]{};
        Entity        targets[StatusEffectComponent::kMaxFxEvents]{};
        int           n = 0;

        static void fn(void* user, Entity target, const StatusFxEvent& ev)
        {
            auto* rec = static_cast<FxRecorder*>(user);
            if (rec->n >= StatusEffectComponent::kMaxFxEvents)
                return;
            rec->targets[rec->n] = target;
            rec->evs[rec->n]     = ev;
            ++rec->n;
        }
    };
} // namespace

TEST(StatusFx_NullAudioAssets_SpawnsCpuEmitterOnPoisonApplied, MatAssetZero)
{
    World  world;
    Entity pawn = makePawn(world);
    ASSERT_GT(world.get<StatusEffectComponent>(pawn)->applyStatus(StatusId::Poison, 8.0f, 1.0f), 0.0f);

    FxRecorder rec{};
    IStatusFx  extra{ &FxRecorder::fn, &rec };
    tickStatusFx(world, nullptr, nullptr, extra);

    EXPECT_EQ(rec.n, 1);
    EXPECT_EQ(rec.evs[0].op, StatusFxOp::Applied);
    EXPECT_EQ(rec.evs[0].statusId, static_cast<uint8_t>(StatusId::Poison));
    EXPECT_EQ(countStatusFx(world), 1);
    EXPECT_EQ(countEmitters(world), 1);

    const Entity fx = firstStatusFx(world);
    ASSERT_TRUE(fx.valid());
    const StatusFxTag* tag = world.get<StatusFxTag>(fx);
    ASSERT_NE(tag, nullptr);
    EXPECT_EQ(tag->follow, pawn);
    EXPECT_EQ(tag->statusId, static_cast<uint8_t>(StatusId::Poison));

    ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(fx);
    ASSERT_NE(pe, nullptr);
    ASSERT_NE(pe->runtime, nullptr);
    EXPECT_EQ(pe->matAssetID, NULL_ASSET);
    EXPECT_TRUE(pe->runtime->isPlaying());
    EXPECT_STREQ(pe->desc.name.c_str(), "PoisonCloud");

    const TransformComponent* fxf = world.get<TransformComponent>(fx);
    ASSERT_NE(fxf, nullptr);
    EXPECT_NEAR(fxf->position.x, 1.0f, 1.0e-4f);
    EXPECT_NEAR(fxf->position.y, 1.1f, 1.0e-4f);
    EXPECT_NEAR(fxf->position.z, 2.0f, 1.0e-4f);
}

TEST(StatusFx_NullAudioAssets_SpawnsCpuEmitterOnStunApplied, StunStars)
{
    World  world;
    Entity pawn = makePawn(world);
    ASSERT_GT(world.get<StatusEffectComponent>(pawn)->applyStatus(StatusId::Stun, 1.2f, 1.0f), 0.0f);
    tickStatusFx(world, nullptr, nullptr);

    EXPECT_EQ(countStatusFx(world), 1);
    const Entity fx = firstStatusFx(world);
    ASSERT_TRUE(fx.valid());
    EXPECT_EQ(world.get<StatusFxTag>(fx)->statusId, static_cast<uint8_t>(StatusId::Stun));
    ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(fx);
    ASSERT_NE(pe, nullptr);
    ASSERT_NE(pe->runtime, nullptr);
    EXPECT_STREQ(pe->desc.name.c_str(), "StunStars");
}

TEST(StatusFx_Expiry_DestroysEntityAfterHarvest, UserHardRule)
{
    World  world;
    Entity pawn = makePawn(world);
    StatusEffectComponent* st = world.get<StatusEffectComponent>(pawn);
    ASSERT_GT(st->applyStatus(StatusId::Poison, 8.0f, 1.0f), 0.0f);
    tickStatusFx(world, nullptr, nullptr);
    ASSERT_EQ(countStatusFx(world), 1);
    const Entity fx = firstStatusFx(world);
    ASSERT_TRUE(world.alive(fx));

    st->tick(8.1f);
    EXPECT_FALSE(st->has(StatusId::Poison));
    EXPECT_FLOAT_EQ(st->remaining(StatusId::Poison), 0.0f);
    DamageEvent dots[StatusEffectComponent::kMaxStatus]{};
    st->harvestDot(dots, StatusEffectComponent::kMaxStatus, pawn);
    tickStatusFx(world, nullptr, nullptr);

    EXPECT_EQ(countStatusFx(world), 0);
    EXPECT_EQ(countEmitters(world), 0);
    EXPECT_FALSE(world.alive(fx));
    EXPECT_FALSE(world.has<StatusFxTag>(fx));
    EXPECT_FALSE(world.has<ParticleEmitterComponent>(fx));
}

TEST(StatusFx_PoundHardCc_SpawnsStunStars, FlagPath)
{
    World  world;
    Entity pawn = makePawn(world);
    JumpAttackDef def{};
    DamageEvent ev{};
    ev.target         = pawn;
    ev.amount         = def.poundDamage;
    ev.type           = def.poundType;
    ev.flags          = def.poundFlags;
    ev.statusId       = 0;
    ev.statusDuration = def.poundStunDuration;

    CombatSystem sys;
    sys.resolve(world, ev);
    EXPECT_TRUE(world.get<StatusEffectComponent>(pawn)->has(StatusId::Stun));
    tickStatusFx(world, nullptr, nullptr);

    EXPECT_EQ(countStatusFx(world), 1);
    const Entity fx = firstStatusFx(world);
    ASSERT_TRUE(fx.valid());
    EXPECT_EQ(world.get<StatusFxTag>(fx)->statusId, static_cast<uint8_t>(StatusId::Stun));
    ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(fx);
    ASSERT_NE(pe, nullptr);
    EXPECT_STREQ(pe->desc.name.c_str(), "StunStars");
}

TEST(StatusFx_KnockdownFlags_NoParticle, JumpAttackShaped)
{
    World  world;
    Entity pawn = makePawn(world);
    JumpAttackDef def{};
    DamageEvent ev{};
    ev.target          = pawn;
    ev.amount          = def.connectDamage;
    ev.type            = def.connectType;
    ev.flags           = def.connectFlags;
    ev.statusId        = 0;
    ev.statusDuration  = def.knockdownDuration;
    ev.statusMagnitude = def.knockdownForce;

    CombatSystem sys;
    sys.resolve(world, ev);
    StatusEffectComponent* st = world.get<StatusEffectComponent>(pawn);
    EXPECT_TRUE(st->knockedDown());
    EXPECT_TRUE(st->has(StatusId::Knockdown));
    tickStatusFx(world, nullptr, nullptr);

    EXPECT_EQ(countStatusFx(world), 0);
    EXPECT_EQ(countEmitters(world), 0);
}

TEST(StatusFx_Cleared_CollectThenDestroy, TwoPresetsGone)
{
    World  world;
    Entity pawn = makePawn(world);
    StatusEffectComponent* st = world.get<StatusEffectComponent>(pawn);
    ASSERT_GT(st->applyStatus(StatusId::Poison, 8.0f, 1.0f), 0.0f);
    ASSERT_GT(st->applyStatus(StatusId::Stun, 1.2f, 1.0f), 0.0f);
    tickStatusFx(world, nullptr, nullptr);
    ASSERT_EQ(countStatusFx(world), 2);

    st->reset();
    tickStatusFx(world, nullptr, nullptr);
    EXPECT_EQ(countStatusFx(world), 0);
    EXPECT_EQ(countEmitters(world), 0);
}

TEST(StatusFx_ClearAll_ZeroTags, HostTeardown)
{
    World  world;
    Entity a = makePawn(world, Vector3f{ 0.0f, 0.0f, 0.0f });
    Entity b = makePawn(world, Vector3f{ 4.0f, 0.0f, 0.0f });
    ASSERT_GT(world.get<StatusEffectComponent>(a)->applyStatus(StatusId::Bleed, 0.0f, 0.0f), 0.0f);
    ASSERT_GT(world.get<StatusEffectComponent>(b)->applyStatus(StatusId::Ignite, 0.0f, 0.0f), 0.0f);
    tickStatusFx(world, nullptr, nullptr);
    ASSERT_EQ(countStatusFx(world), 2);

    clearAllStatusFx(world, nullptr);
    EXPECT_EQ(countStatusFx(world), 0);
    EXPECT_EQ(countEmitters(world), 0);
}

TEST(StatusFx_MissingFollow_Destroys, CollectThenDestroy)
{
    World  world;
    Entity pawn = makePawn(world);
    ASSERT_GT(world.get<StatusEffectComponent>(pawn)->applyStatus(StatusId::Chill, 0.0f, 0.0f), 0.0f);
    tickStatusFx(world, nullptr, nullptr);
    ASSERT_EQ(countStatusFx(world), 1);

    world.remove<TransformComponent>(pawn);
    tickStatusFx(world, nullptr, nullptr);
    EXPECT_EQ(countStatusFx(world), 0);
}

TEST(StatusFx_HealthDead_StopsUntilRevive, EmitterRemains)
{
    World  world;
    Entity pawn = makePawn(world);
    ASSERT_GT(world.get<StatusEffectComponent>(pawn)->applyStatus(StatusId::Shock, 0.0f, 0.0f), 0.0f);
    tickStatusFx(world, nullptr, nullptr);
    ASSERT_EQ(countStatusFx(world), 1);
    ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(firstStatusFx(world));
    ASSERT_NE(pe, nullptr);
    ASSERT_NE(pe->runtime, nullptr);
    EXPECT_TRUE(pe->runtime->isPlaying());

    world.get<HealthComponent>(pawn)->health.applyDamage(1000.0f);
    EXPECT_TRUE(world.get<HealthComponent>(pawn)->health.dead());
    tickStatusFx(world, nullptr, nullptr);
    EXPECT_EQ(countStatusFx(world), 1);
    EXPECT_FALSE(pe->runtime->isPlaying());

    world.get<HealthComponent>(pawn)->health.revive();
    tickStatusFx(world, nullptr, nullptr);
    EXPECT_EQ(countStatusFx(world), 1);
    EXPECT_TRUE(pe->runtime->isPlaying());
}

TEST(StatusFx_Ticked_BleedBurstSix, IgniteBurstFour)
{
    World  world;
    Entity pawn = makePawn(world);
    StatusEffectComponent* st = world.get<StatusEffectComponent>(pawn);
    ASSERT_GT(st->applyStatus(StatusId::Bleed, 0.0f, 0.0f), 0.0f);
    tickStatusFx(world, nullptr, nullptr);
    ParticleEmitterComponent* bleedPe = world.get<ParticleEmitterComponent>(firstStatusFx(world));
    ASSERT_NE(bleedPe, nullptr);
    ASSERT_NE(bleedPe->runtime, nullptr);
    EXPECT_EQ(bleedPe->runtime->aliveCount(), 0u);

    st->tick(1.0f);
    DamageEvent dots[4]{};
    EXPECT_EQ(st->harvestDot(dots, 4, pawn), 1);
    tickStatusFx(world, nullptr, nullptr);
    EXPECT_EQ(bleedPe->runtime->aliveCount(), 6u);

    Entity ignitePawn = makePawn(world, Vector3f{ 8.0f, 0.0f, 0.0f });
    StatusEffectComponent* igniteSt = world.get<StatusEffectComponent>(ignitePawn);
    ASSERT_GT(igniteSt->applyStatus(StatusId::Ignite, 0.0f, 0.0f), 0.0f);
    tickStatusFx(world, nullptr, nullptr);
    Entity igniteFx{};
    world.each<StatusFxTag>([&](Entity e, StatusFxTag& tag) {
        if (tag.follow == ignitePawn)
            igniteFx = e;
    });
    ASSERT_TRUE(igniteFx.valid());
    ParticleEmitterComponent* ignitePe = world.get<ParticleEmitterComponent>(igniteFx);
    ASSERT_NE(ignitePe, nullptr);
    igniteSt->tick(1.0f);
    EXPECT_EQ(igniteSt->harvestDot(dots, 4, ignitePawn), 1);
    tickStatusFx(world, nullptr, nullptr);
    EXPECT_EQ(ignitePe->runtime->aliveCount(), 4u);
}

TEST(StatusFx_Refreshed_KeepsEmitterNoSecondSpawn, OneTag)
{
    World  world;
    Entity pawn = makePawn(world);
    StatusEffectComponent* st = world.get<StatusEffectComponent>(pawn);
    ASSERT_GT(st->applyStatus(StatusId::Poison, 8.0f, 1.0f), 0.0f);
    tickStatusFx(world, nullptr, nullptr);
    const Entity fx = firstStatusFx(world);
    ASSERT_TRUE(fx.valid());

    ASSERT_GT(st->applyStatus(StatusId::Poison, 3.0f, 1.0f), 0.0f);
    tickStatusFx(world, nullptr, nullptr);
    EXPECT_EQ(countStatusFx(world), 1);
    EXPECT_EQ(firstStatusFx(world), fx);
}

TEST(StatusParticlePresets_FrozenLooks, RatesColorsGravity)
{
    const ParticleEmitterDesc stun = statusParticlePreset(StatusParticlePreset::StunStars);
    EXPECT_STREQ(stun.name.c_str(), "StunStars");
    EXPECT_FLOAT_EQ(stun.emissionRate, 12.0f);
    EXPECT_TRUE(stun.additiveBlend);
    EXPECT_FLOAT_EQ(stun.gravity.y, 0.0f);
    EXPECT_FLOAT_EQ(stun.startColor[0], 1.0f);
    EXPECT_FLOAT_EQ(stun.startColor[1], 0.95f);
    EXPECT_FLOAT_EQ(stun.startColor[2], 0.45f);
    EXPECT_FLOAT_EQ(stun.endColor[3], 0.0f);

    const ParticleEmitterDesc poison = statusParticlePreset(StatusParticlePreset::PoisonCloud);
    EXPECT_FLOAT_EQ(poison.emissionRate, 18.0f);
    EXPECT_FLOAT_EQ(poison.gravity.y, 0.4f);
    EXPECT_EQ(poison.shape, ParticleEmitterDesc::Shape::Sphere);
    EXPECT_FLOAT_EQ(poison.shapeSize.x, 0.35f);

    const ParticleEmitterDesc bleed = statusParticlePreset(StatusParticlePreset::BleedDrip);
    EXPECT_FLOAT_EQ(bleed.emissionRate, 8.0f);
    EXPECT_FALSE(bleed.additiveBlend);
    EXPECT_FLOAT_EQ(bleed.gravity.y, -11.0f);
    EXPECT_FLOAT_EQ(bleed.startColor[0], 0.72f);

    const ParticleEmitterDesc ignite = statusParticlePreset(StatusParticlePreset::IgniteFlames);
    EXPECT_FLOAT_EQ(ignite.emissionRate, 16.0f);
    EXPECT_FLOAT_EQ(ignite.gravity.y, 1.2f);
    EXPECT_FLOAT_EQ(ignite.shapeSize.x, 0.30f);

    const ParticleEmitterDesc chill = statusParticlePreset(StatusParticlePreset::ChillMist);
    EXPECT_FLOAT_EQ(chill.emissionRate, 14.0f);
    EXPECT_FLOAT_EQ(chill.gravity.y, 0.15f);
    EXPECT_FLOAT_EQ(chill.shapeSize.x, 0.40f);

    const ParticleEmitterDesc shock = statusParticlePreset(StatusParticlePreset::ShockSparks);
    EXPECT_FLOAT_EQ(shock.emissionRate, 20.0f);
    EXPECT_FLOAT_EQ(shock.gravity.y, 0.0f);
    EXPECT_GT(shock.spreadDegrees, 90.0f);
}

TEST(ParticleTick_AuthoredNonOrigin_SimulatesThere, MergeGate)
{
    World world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = Vector3f{ 4.0f, 2.0f, 8.0f };
    world.emplace<TransformComponent>(e, xf);
    ParticleEmitterComponent pe{};
    pe.desc.emissionRate = 100.0f;
    pe.desc.lifetime     = { 2.0f, 2.0f };
    pe.desc.startSpeed   = { 0.0f, 0.0f };
    pe.playing           = true;
    world.emplace<ParticleEmitterComponent>(e, std::move(pe));

    tickParticleEmitters(world, 0.1f);
    ParticleEmitterComponent* c = world.get<ParticleEmitterComponent>(e);
    ASSERT_NE(c, nullptr);
    ASSERT_NE(c->runtime, nullptr);
    EXPECT_NEAR(c->runtime->position().x, 4.0f, 1.0e-4f);
    EXPECT_NEAR(c->runtime->position().y, 2.0f, 1.0e-4f);
    EXPECT_NEAR(c->runtime->position().z, 8.0f, 1.0e-4f);
    EXPECT_GT(c->runtime->aliveCount(), 0u);
    bool nearOrigin = false;
    bool nearAuthored = false;
    for (const Particle& p : c->runtime->particles())
    {
        if (!p.alive)
            continue;
        const float d0 = p.position.x * p.position.x + p.position.y * p.position.y + p.position.z * p.position.z;
        if (d0 < 0.25f)
            nearOrigin = true;
        const float dx = p.position.x - 4.0f;
        const float dy = p.position.y - 2.0f;
        const float dz = p.position.z - 8.0f;
        if (dx * dx + dy * dy + dz * dz < 0.25f)
            nearAuthored = true;
    }
    EXPECT_FALSE(nearOrigin);
    EXPECT_TRUE(nearAuthored);
}
