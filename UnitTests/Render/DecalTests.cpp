#include <gtest/gtest.h>

#include "Animation/AnimGraphComponent.h"
#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/Box3f.h"
#include "Math/Color.h"
#include "Render/Camera3D.h"
#include "Render/DecalBasis.h"
#include "Render/DecalPool.h"
#include "Render/SceneBuffers.h"

#include <cmath>

using Dark::AnimGraphComponent;
using Dark::Camera3D;
using Dark::decalAngleFade;
using Dark::decalBakeBurn;
using Dark::DecalBakedNormal;
using Dark::decalBakeNormalTexel;
using Dark::decalBakeNormalUv;
using Dark::decalBlendAlbedo;
using Dark::decalBlendAttrib;
using Dark::decalBuildAxes;
using Dark::DecalBurnBake;
using Dark::DecalChannel_Albedo;
using Dark::DecalChannel_Emissive;
using Dark::DecalChannel_Normal;
using Dark::DecalChannel_Roughness;
using Dark::decalClipLocal;
using Dark::decalClipWorld;
using Dark::DecalDef;
using Dark::decalDef;
using Dark::DecalDefId;
using Dark::DecalFade;
using Dark::decalFade;
using Dark::DecalGpuInstance;
using Dark::DecalId;
using Dark::decalImpactAlpha;
using Dark::DecalKind;
using Dark::decalMapNormal;
using Dark::DecalNormalRecipe;
using Dark::decalPackBytes;
using Dark::decalPackUnorm;
using Dark::DecalPool;
using Dark::decalSelectDef;
using Dark::decalShellBiasedPosition;
using Dark::DecalSpace;
using Dark::DecalSpawnDesc;
using Dark::decalTangentBasis;
using Dark::decalUnpackBytes;
using Dark::decalWeightZeroNormalBytes;
using Dark::decalWorldMatrix;
using Dark::Entity;
using Dark::Frustum3f;
using Dark::HealthComponent;
using Dark::kDecalAngleFadeRange;
using Dark::kDecalAngleFadeStart;
using Dark::kDecalFootAbovePlaneMeters;
using Dark::kDecalNormalMapSize;
using Dark::kDecalShellBiasMeters;
using Dark::SceneBuffers;
using Dark::TransformComponent;
using Dark::WeaponKind;
using Dark::World;
using Dark::Math::Box3f;
using Dark::Math::Matrix4f;
using Dark::Math::Vector3f;

namespace
{

    struct WideView
    {
        Frustum3f frustum;
        Vector3f  camera{ 0.0f, 50.0f, -2000.0f };
    };

    WideView makeWideView()
    {
        Camera3D cam;
        cam.SetOrthographic(4000.0f, 4000.0f, 0.1f, 8000.0f);
        cam.LookAt(Vector3f(0.0f, 50.0f, -2000.0f), Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));
        WideView view;
        view.frustum = Frustum3f(cam.GetCullViewProj());
        view.camera  = Vector3f(0.0f, 50.0f, -2000.0f);
        return view;
    }

    struct Visible
    {
        DecalGpuInstance outside[8]{};
        DecalGpuInstance inside[8]{};
        uint32_t         outsideCount = 0;
        uint32_t         insideCount  = 0;
    };

    Visible gather(const DecalPool& pool, const Vector3f& camera)
    {
        const WideView view = makeWideView();
        Visible        vis;
        pool.buildVisible(view.frustum, camera, vis.outside, 8, &vis.outsideCount, vis.inside, 8, &vis.insideCount);
        return vis;
    }

    DecalSpawnDesc descAt(DecalKind kind, float x)
    {
        DecalSpawnDesc desc;
        desc.kind     = kind;
        desc.position = Vector3f(x, 0.0f, 0.0f);
        return desc;
    }

    bool nearly(const Vector3f& a, const Vector3f& b, float eps)
    {
        return (a - b).Magnitude() <= eps;
    }

} // namespace

TEST(Decal_Clip_Inside, SymmetricCube)
{
    EXPECT_TRUE(decalClipLocal(0.9f, 0.1f, -0.9f, -1.0f, 1.0f));
}

TEST(Decal_Clip_Outside, PastLocalX)
{
    EXPECT_FALSE(decalClipLocal(1.01f, 0.0f, 0.0f, -1.0f, 1.0f));
}

TEST(Decal_Clip_FootmarkAbove, FiveCentimeters)
{
    const DecalDef& foot = decalDef(DecalDefId::Footmark);
    const DecalDef& hit  = decalDef(DecalDefId::BloodHit);
    EXPECT_NEAR(foot.halfExtents.y, 0.08f, 1.0e-6f);
    EXPECT_NEAR(hit.halfExtents.y, 0.10f, 1.0e-6f);
    EXPECT_NEAR(foot.clipLocalYMax, kDecalFootAbovePlaneMeters / foot.halfExtents.y, 1.0e-5f);

    Vector3f axisX;
    Vector3f axisY;
    Vector3f axisZ;
    ASSERT_TRUE(decalBuildAxes(Vector3f(0.0f, 1.0f, 0.0f), Vector3f(1.0f, 0.0f, 0.0f), axisX, axisY, axisZ));

    const Vector3f center(0.0f, 1.0f, 0.0f);
    const Vector3f above = center + Vector3f(0.0f, 0.05f, 0.0f);
    const Vector3f air   = center + Vector3f(0.0f, kDecalFootAbovePlaneMeters, 0.0f);

    const Matrix4f footWorld = decalWorldMatrix(center, axisX, axisY, axisZ, foot.halfExtents);
    const Matrix4f hitWorld  = decalWorldMatrix(center, axisX, axisY, axisZ, hit.halfExtents);
    EXPECT_FALSE(decalClipWorld(above, footWorld.Inverse(), foot.clipLocalYMin, foot.clipLocalYMax));
    EXPECT_TRUE(decalClipWorld(air, footWorld.Inverse(), foot.clipLocalYMin, foot.clipLocalYMax));
    EXPECT_TRUE(decalClipWorld(above, hitWorld.Inverse(), hit.clipLocalYMin, hit.clipLocalYMax));
}

TEST(Decal_Fade_Linear_End, Endpoints)
{
    EXPECT_NEAR(decalFade(DecalFade::Linear, 1.0f, 0.0f), 0.0f, 1.0e-6f);
    EXPECT_NEAR(decalFade(DecalFade::Linear, 0.0f, 0.0f), 1.0f, 1.0e-6f);
}

TEST(Decal_Fade_Smoothstep_Mid, Half)
{
    EXPECT_NEAR(decalFade(DecalFade::Smoothstep, 0.5f, 0.0f), 0.5f, 1.0e-6f);
}

TEST(Decal_Fade_Hold_Mid, HoldThenDrop)
{
    EXPECT_NEAR(decalFade(DecalFade::HoldThenLinear, 0.5f, 0.70f), 1.0f, 1.0e-6f);
    EXPECT_NEAR(decalFade(DecalFade::HoldThenLinear, 1.0f, 0.70f), 0.0f, 1.0e-6f);
}

TEST(Decal_Angle_GrazingReject, BelowStart)
{
    float fade = 1.0f;
    EXPECT_FALSE(decalAngleFade(0.19f, kDecalAngleFadeStart, kDecalAngleFadeRange, fade));
}

TEST(Decal_Angle_FadePartial, MidRange)
{
    float fade = 0.0f;
    EXPECT_TRUE(decalAngleFade(0.30f, kDecalAngleFadeStart, kDecalAngleFadeRange, fade));
    EXPECT_NEAR(fade, 0.4f, 1.0e-5f);
}

TEST(Decal_Basis_Perpendicular, RightHanded)
{
    Vector3f T;
    Vector3f B;
    Vector3f N;
    decalTangentBasis(Vector3f(0.0f, 0.0f, 1.0f), Vector3f(1.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f), T, B, N);
    EXPECT_NEAR(T.Dot(N), 0.0f, 1.0e-5f);
    EXPECT_NEAR(B.Dot(N), 0.0f, 1.0e-5f);
    EXPECT_NEAR(T.Dot(B), 0.0f, 1.0e-5f);
    EXPECT_GT(T.Cross(B).Dot(N), 0.99f);
}

TEST(Decal_Basis_FlatNormal_IsGeomN, CopiesGeom)
{
    Vector3f geomN(0.2f, 0.5f, 0.8f);
    geomN.Normalize();
    const Vector3f mapped = decalMapNormal(geomN, Vector3f(1.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 0.0f, 1.0f), 1.0f);
    EXPECT_NEAR(mapped.x, geomN.x, 1.0e-4f);
    EXPECT_NEAR(mapped.y, geomN.y, 1.0e-4f);
    EXPECT_NEAR(mapped.z, geomN.z, 1.0e-4f);
}

TEST(Decal_Basis_SlopeBends, NotAxisY)
{
    Vector3f geomN(0.0f, 0.2f, 1.0f);
    geomN.Normalize();
    const Vector3f axisY(0.0f, 1.0f, 0.0f);
    EXPECT_FALSE(nearly(geomN, axisY, 0.05f));
    const Vector3f mapped = decalMapNormal(geomN, Vector3f(1.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.6f, 0.0f, 0.8f), 1.0f);
    EXPECT_NEAR(mapped.Magnitude(), 1.0f, 1.0e-4f);
    EXPECT_FALSE(nearly(mapped, axisY, 0.2f));
}

TEST(Decal_PackAttrib_Clear, HighByteAlpha)
{
    EXPECT_EQ(decalPackUnorm(0.5f, 0.5f, 1.0f, 0.0f), 0x00FF8080u);
}

TEST(Decal_Pack_ZeroWeightCopiesBytes, AlbedoAndAttrib)
{
    const uint32_t raw = 0x44332211u;
    const Vector3f src(0.2f, 0.4f, 0.6f);
    const uint32_t mask = DecalChannel_Albedo | DecalChannel_Emissive | DecalChannel_Normal | DecalChannel_Roughness;
    EXPECT_EQ(decalBlendAlbedo(raw, mask, 0.0f, 0.0f, src, 1.0f), raw);
    EXPECT_EQ(decalBlendAttrib(raw, mask, 0.0f, Vector3f(0.0f, 0.0f, 1.0f), 0.0f, 0.2f, 0.0f, 1.0f), raw);
}

TEST(Decal_Albedo_SrgbRoundTrip, WithinOne)
{
    const uint8_t bytes[3] = { 150, 6, 10 };
    for (uint8_t byte : bytes)
    {
        const uint8_t back  = Dark::Color::linearToSrgb8(Dark::Color::srgb8ToLinear(byte));
        const int     delta = static_cast<int>(back) - static_cast<int>(byte);
        EXPECT_LE(delta < 0 ? -delta : delta, 1);
    }
}

TEST(Decal_ChannelMask_SkipsRough, AlbedoStillWrites)
{
    const uint32_t raw     = decalPackBytes(10, 20, 30, 40);
    const uint32_t skipped = decalBlendAttrib(raw, DecalChannel_Normal, 1.0f, Vector3f(0.0f, 0.0f, 1.0f), 1.0f, 0.1f, 0.0f, 0.0f);
    uint8_t        r       = 0;
    uint8_t        g       = 0;
    uint8_t        b       = 0;
    uint8_t        a       = 0;
    decalUnpackBytes(skipped, r, g, b, a);
    EXPECT_EQ(b, 30);

    const uint32_t roughOnly = decalBlendAttrib(raw, DecalChannel_Roughness, 0.0f, Vector3f(0.0f, 0.0f, 1.0f), 1.0f, 0.1f, 0.0f, 0.0f);
    decalUnpackBytes(roughOnly, r, g, b, a);
    EXPECT_NE(b, 30);

    const uint32_t albedoIn  = decalPackBytes(200, 180, 160, 0);
    const uint32_t albedoOut = decalBlendAlbedo(albedoIn, DecalChannel_Albedo, 1.0f, 0.0f, Vector3f(0.04f, 0.04f, 0.04f), 0.0f);
    decalUnpackBytes(albedoOut, r, g, b, a);
    EXPECT_NE(r, 200);
    EXPECT_NE(g, 180);
    EXPECT_NE(b, 160);
}

TEST(Decal_Pool_RecycleOldest, DropsSerialOne)
{
    DecalPool pool;
    DecalId   ids[DecalPool::kCapacity + 1]{};
    for (uint32_t i = 0; i < DecalPool::kCapacity + 1; ++i)
    {
        const DecalSpawnDesc desc = descAt(DecalKind::Footmark, static_cast<float>(i) * 0.01f);
        ASSERT_TRUE(pool.spawn(desc, &ids[i]));
    }
    EXPECT_EQ(pool.aliveCount(), DecalPool::kCapacity);
    EXPECT_FALSE(pool.alive(ids[0]));
    for (uint32_t i = 1; i < DecalPool::kCapacity; ++i)
        EXPECT_TRUE(pool.alive(ids[i])) << i;
    EXPECT_GE(pool.recycleCount(), 1u);
}

TEST(Decal_Recycle_NewSerial, AboveSurvivors)
{
    DecalPool pool;
    DecalId   ids[DecalPool::kCapacity + 1]{};
    for (uint32_t i = 0; i < DecalPool::kCapacity + 1; ++i)
    {
        const DecalSpawnDesc desc = descAt(DecalKind::Footmark, static_cast<float>(i) * 0.01f);
        ASSERT_TRUE(pool.spawn(desc, &ids[i]));
    }
    uint32_t maxSurvivor = 0;
    for (uint32_t i = 1; i < DecalPool::kCapacity; ++i)
    {
        ASSERT_TRUE(pool.alive(ids[i]));
        if (ids[i].serial > maxSurvivor)
            maxSurvivor = ids[i].serial;
    }
    EXPECT_GT(ids[DecalPool::kCapacity].serial, maxSurvivor);
    EXPECT_NE(ids[DecalPool::kCapacity].serial, 0u);
    EXPECT_TRUE(pool.alive(ids[DecalPool::kCapacity]));
}

TEST(Decal_Serial_Wrap, CompactsThenAssigns)
{
    DecalPool pool;
    pool.setNextSerialForTest(100u);
    DecalId first[3]{};
    for (int i = 0; i < 3; ++i)
    {
        const DecalSpawnDesc desc = descAt(DecalKind::Burn, static_cast<float>(i));
        ASSERT_TRUE(pool.spawn(desc, &first[i]));
    }
    pool.setNextSerialForTest(0xFFFFFFFFu);
    DecalId              fresh{};
    const DecalSpawnDesc newest = descAt(DecalKind::Burn, 3.0f);
    ASSERT_TRUE(pool.spawn(newest, &fresh));

    const Visible vis = gather(pool, makeWideView().camera);
    ASSERT_EQ(vis.outsideCount, 4u);
    EXPECT_NEAR(vis.outside[0].worldFromDecal[12], 0.0f, 1.0e-4f);
    EXPECT_NEAR(vis.outside[1].worldFromDecal[12], 1.0f, 1.0e-4f);
    EXPECT_NEAR(vis.outside[2].worldFromDecal[12], 2.0f, 1.0e-4f);
    EXPECT_NEAR(vis.outside[3].worldFromDecal[12], 3.0f, 1.0e-4f);
    EXPECT_EQ(fresh.serial, pool.aliveCount());
    EXPECT_EQ(fresh.serial, 4u);
    EXPECT_NE(fresh.serial, 0u);
}

TEST(Decal_DrawOrder_FootAfterBlood, SerialNotKind)
{
    DecalPool pool;
    ASSERT_TRUE(pool.spawn(descAt(DecalKind::Blood, 0.0f), nullptr));
    ASSERT_TRUE(pool.spawn(descAt(DecalKind::Footmark, 2.0f), nullptr));

    const Visible vis = gather(pool, makeWideView().camera);
    ASSERT_EQ(vis.outsideCount, 2u);
    EXPECT_EQ(vis.outside[0].channelMask, static_cast<uint32_t>(DecalChannel_Albedo));
    EXPECT_NEAR(vis.outside[0].tint[0], 1.0f, 1.0e-5f);
    EXPECT_EQ(vis.outside[1].channelMask, static_cast<uint32_t>(DecalChannel_Albedo | DecalChannel_Normal));
    EXPECT_NEAR(vis.outside[1].tint[0], 0.25f, 1.0e-5f);
    EXPECT_LT(vis.outside[0].worldFromDecal[12], vis.outside[1].worldFromDecal[12]);
}

TEST(Decal_Pool_Expire, PastLifetime)
{
    DecalPool pool;
    World     world;
    DecalId   id{};
    ASSERT_TRUE(pool.spawn(descAt(DecalKind::Footmark, 0.0f), &id));
    pool.tick(world, 3.0f);
    EXPECT_TRUE(pool.alive(id));
    pool.tick(world, 5.0f);
    EXPECT_FALSE(pool.alive(id));
    EXPECT_EQ(pool.aliveCount(), 0u);
}

TEST(Decal_Pool_DeadEntityFrees, DestroyDrops)
{
    World  world;
    Entity entity = world.createEntity();
    world.emplace<TransformComponent>(entity);
    world.emplace<HealthComponent>(entity);

    DecalSpawnDesc desc = descAt(DecalKind::Blood, 0.0f);
    desc.space          = DecalSpace::Entity;
    desc.entity         = entity;

    DecalPool pool;
    DecalId   id{};
    ASSERT_TRUE(pool.spawn(desc, &id));
    world.destroyEntity(entity);
    pool.tick(world, 0.05f);
    EXPECT_FALSE(pool.alive(id));
}

TEST(Decal_Pool_CorpseKeeps, SmallMoveStays)
{
    World               world;
    Entity              entity = world.createEntity();
    TransformComponent& xf     = world.emplace<TransformComponent>(entity);
    HealthComponent&    hp     = world.emplace<HealthComponent>(entity);
    hp.health.applyDamage(1000.0f);

    DecalSpawnDesc desc = descAt(DecalKind::Blood, 0.0f);
    desc.space          = DecalSpace::Entity;
    desc.entity         = entity;

    DecalPool pool;
    DecalId   id{};
    ASSERT_TRUE(pool.spawn(desc, &id));
    pool.tick(world, 0.05f);
    ASSERT_TRUE(pool.alive(id));

    xf.position = Vector3f(0.04f, 0.0f, 0.0f);
    pool.tick(world, 0.05f);
    EXPECT_TRUE(world.alive(entity));
    EXPECT_TRUE(hp.health.dead());
    EXPECT_TRUE(pool.alive(id));
}

TEST(Decal_Pool_ReviveFrees, DeadToAlive)
{
    World  world;
    Entity entity = world.createEntity();
    world.emplace<TransformComponent>(entity);
    HealthComponent& hp = world.emplace<HealthComponent>(entity);
    hp.health.applyDamage(1000.0f);

    DecalSpawnDesc desc = descAt(DecalKind::Blood, 0.0f);
    desc.space          = DecalSpace::Entity;
    desc.entity         = entity;

    DecalPool pool;
    DecalId   id{};
    ASSERT_TRUE(pool.spawn(desc, &id));
    pool.tick(world, 0.05f);
    ASSERT_TRUE(pool.alive(id));
    hp.health.revive();
    pool.tick(world, 0.05f);
    EXPECT_FALSE(pool.alive(id));
}

TEST(Decal_Pool_JumpFrees, TeleportDropsEntityKeepsWorld)
{
    World               world;
    Entity              entity = world.createEntity();
    TransformComponent& xf     = world.emplace<TransformComponent>(entity);
    HealthComponent&    hp     = world.emplace<HealthComponent>(entity);
    hp.health.applyDamage(1000.0f);

    DecalSpawnDesc body = descAt(DecalKind::Blood, 0.0f);
    body.space          = DecalSpace::Entity;
    body.entity         = entity;

    DecalSpawnDesc stain = descAt(DecalKind::Burn, 4.0f);
    stain.space          = DecalSpace::World;
    stain.entity         = entity;

    DecalPool pool;
    DecalId   bodyId{};
    DecalId   worldId{};
    ASSERT_TRUE(pool.spawn(body, &bodyId));
    ASSERT_TRUE(pool.spawn(stain, &worldId));
    pool.tick(world, 0.05f);
    ASSERT_TRUE(pool.alive(bodyId));
    ASSERT_TRUE(pool.alive(worldId));

    xf.position = Vector3f(3.0f, 0.0f, 0.0f);
    pool.tick(world, 0.05f);
    EXPECT_FALSE(pool.alive(bodyId));
    EXPECT_TRUE(pool.alive(worldId));

    hp.health.revive();
    pool.tick(world, 0.05f);
    EXPECT_TRUE(pool.alive(worldId));
}

TEST(Decal_HitBias_Shell, SixCentimeters)
{
    const Vector3f hit(1.0f, 2.0f, 3.0f);
    const Vector3f normal(0.0f, 1.0f, 0.0f);
    const Vector3f pos = decalShellBiasedPosition(hit, normal);
    EXPECT_NEAR(pos.x, hit.x, 1.0e-5f);
    EXPECT_NEAR(pos.y, hit.y - kDecalShellBiasMeters, 1.0e-5f);
    EXPECT_NEAR(pos.z, hit.z, 1.0e-5f);

    const DecalDef& blood = decalDef(DecalDefId::BloodHit);
    EXPECT_NEAR(blood.halfExtents.y, 0.10f, 1.0e-6f);
    const Box3f box(pos, Vector3f(1.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), blood.halfExtents.x, blood.halfExtents.y, blood.halfExtents.z);
    EXPECT_TRUE(box.Contains(hit - normal * 0.06f));
    EXPECT_FALSE(box.Contains(hit + normal * 0.06f));

    DecalSpawnDesc desc = descAt(DecalKind::Blood, 0.0f);
    desc.space          = DecalSpace::Entity;
    desc.position       = pos;
    DecalPool pool;
    ASSERT_TRUE(pool.spawn(desc, nullptr));
    const Visible vis = gather(pool, makeWideView().camera);
    ASSERT_EQ(vis.outsideCount, 1u);
    EXPECT_NEAR(vis.outside[0].worldFromDecal[12], pos.x, 1.0e-4f);
    EXPECT_NEAR(vis.outside[0].worldFromDecal[13], pos.y, 1.0e-4f);
    EXPECT_NEAR(vis.outside[0].worldFromDecal[5], blood.halfExtents.y, 1.0e-4f);
}

TEST(Decal_InsideCamera, CenterIsInside)
{
    DecalPool pool;
    ASSERT_TRUE(pool.spawn(descAt(DecalKind::Burn, 0.0f), nullptr));
    const Visible vis = gather(pool, Vector3f(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(vis.insideCount, 1u);
    EXPECT_EQ(vis.outsideCount, 0u);
}

TEST(Decal_Inside_Index, SecondBoxIsDistinct)
{
    DecalPool pool;
    ASSERT_TRUE(pool.spawn(descAt(DecalKind::Burn, 0.0f), nullptr));
    ASSERT_TRUE(pool.spawn(descAt(DecalKind::Burn, 0.2f), nullptr));
    const Visible vis = gather(pool, Vector3f(0.0f, 0.0f, 0.0f));
    ASSERT_EQ(vis.insideCount, 2u);
    EXPECT_NEAR(vis.inside[0].worldFromDecal[12], 0.0f, 1.0e-4f);
    EXPECT_NEAR(vis.inside[1].worldFromDecal[12], 0.2f, 1.0e-4f);
}

TEST(Decal_Burn_Weights, HotThenCold)
{
    const DecalBurnBake hot = decalBakeBurn(0.0f);
    EXPECT_NEAR(hot.albedoWeight, 0.85f, 1.0e-5f);
    EXPECT_NEAR(hot.emissiveWeight, 1.0f, 1.0e-5f);
    EXPECT_NEAR(hot.emissiveTarget, 1.0f, 1.0e-5f);
    EXPECT_NEAR(hot.tint.x, 0.85f, 1.0e-5f);
    EXPECT_NEAR(hot.tint.y, 0.22f, 1.0e-5f);
    EXPECT_NEAR(hot.tint.z, 0.04f, 1.0e-5f);

    const float storedR = 0.2f + (hot.tint.x - 0.2f) * hot.albedoWeight;
    const float term    = storedR * hot.emissiveWeight * 4.0f;
    EXPECT_NEAR(term, 3.01f, 1.0e-4f);

    const uint8_t  dst     = Dark::Color::linearToSrgb8(0.2f);
    const uint32_t raw     = decalPackBytes(dst, dst, dst, 0);
    const uint32_t mask    = DecalChannel_Albedo | DecalChannel_Emissive;
    const uint32_t blended = decalBlendAlbedo(raw, mask, hot.albedoWeight, hot.emissiveWeight, hot.tint, hot.emissiveTarget);
    uint8_t        r       = 0;
    uint8_t        g       = 0;
    uint8_t        b       = 0;
    uint8_t        a       = 0;
    decalUnpackBytes(blended, r, g, b, a);
    const float quantized = Dark::Color::srgb8ToLinear(r) * (static_cast<float>(a) / 255.0f) * 4.0f;
    EXPECT_NEAR(quantized, 3.01f, 0.05f);

    const DecalBurnBake cold = decalBakeBurn(1.0f);
    EXPECT_NEAR(cold.albedoWeight, 0.0f, 1.0e-5f);
    EXPECT_NEAR(cold.emissiveWeight, 0.0f, 1.0e-5f);
    EXPECT_NEAR(cold.tint.x, 0.03f, 1.0e-5f);
    EXPECT_NEAR(cold.tint.y, 0.02f, 1.0e-5f);
    EXPECT_NEAR(cold.tint.z, 0.015f, 1.0e-5f);
    EXPECT_EQ(decalBlendAlbedo(raw, mask, cold.albedoWeight, cold.emissiveWeight, cold.tint, cold.emissiveTarget), raw);
    EXPECT_NEAR(cold.emissiveWeight * cold.emissiveTarget * 4.0f, 0.0f, 1.0e-6f);

    DecalPool pool;
    ASSERT_TRUE(pool.spawn(descAt(DecalKind::Burn, 0.0f), nullptr));
    const Visible vis = gather(pool, makeWideView().camera);
    ASSERT_EQ(vis.outsideCount, 1u);
    EXPECT_NEAR(vis.outside[0].albedoWeight, 0.85f, 1.0e-5f);
    EXPECT_NEAR(vis.outside[0].emissiveWeight, 1.0f, 1.0e-5f);
    EXPECT_NEAR(vis.outside[0].emissiveTarget, 1.0f, 1.0e-5f);
    EXPECT_NEAR(vis.outside[0].tint[0], 0.85f, 1.0e-5f);
    EXPECT_NEAR(vis.outside[0].roughnessWeight, 0.8f, 1.0e-5f);
}

TEST(Decal_Normal_FlatTexel, WeightZeroConstant)
{
    EXPECT_NEAR(decalDef(DecalDefId::Burn).normalWeight, 0.0f, 1.0e-6f);
    EXPECT_NEAR(decalDef(DecalDefId::BloodDeath).normalWeight, 0.0f, 1.0e-6f);
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    decalWeightZeroNormalBytes(r, g, b);
    EXPECT_EQ(r, 128);
    EXPECT_EQ(g, 128);
    EXPECT_EQ(b, 255);
}

TEST(Decal_Normal_FlatBake, EveryTexel)
{
    for (int y = 0; y < kDecalNormalMapSize; ++y)
    {
        for (int x = 0; x < kDecalNormalMapSize; ++x)
        {
            const DecalBakedNormal texel = decalBakeNormalTexel(DecalNormalRecipe::Flat, x, y);
            EXPECT_EQ(texel.r, 128) << x << "," << y;
            EXPECT_EQ(texel.g, 128) << x << "," << y;
            EXPECT_EQ(texel.b, 255) << x << "," << y;
        }
    }
}

TEST(Decal_Normal_FootCenter, UnitAndUp)
{
    const DecalBakedNormal n = decalBakeNormalUv(decalDef(DecalDefId::Footmark).normalRecipe, 0.0f, 0.0f);
    EXPECT_GT(n.n.z, 0.8f);
    EXPECT_NEAR(n.n.Magnitude(), 1.0f, 1.0e-4f);
}

TEST(Decal_Normal_ImpactCenter, PitFacesOut)
{
    const DecalBakedNormal n = decalBakeNormalUv(decalDef(DecalDefId::ImpactBullet).normalRecipe, 0.0f, 0.0f);
    EXPECT_GT(n.n.z, 0.95f);
    EXPECT_NEAR(n.n.Magnitude(), 1.0f, 1.0e-4f);
}

TEST(Decal_Normal_ImpactInward, RadiusPointThree)
{
    const float            u = 0.3f;
    const DecalBakedNormal n = decalBakeNormalUv(DecalNormalRecipe::ImpactBullet, u, 0.0f);
    EXPECT_LT(n.n.x * u + n.n.y * 0.0f, 0.0f);
}

TEST(Decal_Impact_TwoDefs, BulletVersusSlash)
{
    DecalSpawnDesc bullet;
    bullet.kind   = DecalKind::Impact;
    bullet.weapon = WeaponKind::Projectile;
    DecalSpawnDesc slash;
    slash.kind   = DecalKind::Impact;
    slash.weapon = WeaponKind::Melee;

    EXPECT_EQ(decalSelectDef(bullet), DecalDefId::ImpactBullet);
    EXPECT_EQ(decalSelectDef(slash), DecalDefId::ImpactSlash);

    bullet.halfExtents = Vector3f(0.5f, 0.0f, 0.0f);
    EXPECT_EQ(decalSelectDef(bullet), DecalDefId::ImpactBullet);

    const DecalDef& bulletDef = decalDef(DecalDefId::ImpactBullet);
    const DecalDef& slashDef  = decalDef(DecalDefId::ImpactSlash);
    const uint32_t  mask      = DecalChannel_Albedo | DecalChannel_Normal | DecalChannel_Roughness;
    EXPECT_EQ(bulletDef.channelMask, mask);
    EXPECT_EQ(slashDef.channelMask, mask);
    EXPECT_NEAR(bulletDef.halfExtents.x, 0.06f, 1.0e-6f);
    EXPECT_NEAR(bulletDef.tint.x, 0.04f, 1.0e-6f);
    EXPECT_NEAR(bulletDef.tint.y, 0.035f, 1.0e-6f);
    EXPECT_NEAR(bulletDef.tint.z, 0.03f, 1.0e-6f);
    EXPECT_NEAR(slashDef.halfExtents.x, 0.22f, 1.0e-6f);
    EXPECT_NEAR(slashDef.halfExtents.z, 0.07f, 1.0e-6f);
    EXPECT_NEAR(slashDef.tint.x, 0.09f, 1.0e-6f);
    EXPECT_NEAR(slashDef.tint.y, 0.07f, 1.0e-6f);
    EXPECT_NEAR(slashDef.tint.z, 0.05f, 1.0e-6f);
    EXPECT_NE(bulletDef.roughnessTarget, slashDef.roughnessTarget);
    EXPECT_EQ(decalImpactAlpha(DecalDefId::ImpactBullet, 0.0f, 0.0f), 255);
    EXPECT_EQ(decalImpactAlpha(DecalDefId::ImpactBullet, 0.95f, 0.0f), 0);
    EXPECT_EQ(decalImpactAlpha(DecalDefId::ImpactSlash, 0.5f, 0.0f), 255);
    EXPECT_EQ(decalImpactAlpha(DecalDefId::ImpactSlash, 0.0f, 0.8f), 0);

    DecalPool pool;
    DecalId   id{};
    bullet.position = Vector3f(0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(pool.spawn(bullet, &id));
    Visible vis = gather(pool, makeWideView().camera);
    ASSERT_EQ(vis.outsideCount, 1u);
    EXPECT_NEAR(vis.outside[0].worldFromDecal[0], 0.5f, 1.0e-4f);
    EXPECT_NEAR(vis.outside[0].worldFromDecal[5], bulletDef.halfExtents.y, 1.0e-4f);
    EXPECT_NEAR(vis.outside[0].worldFromDecal[10], bulletDef.halfExtents.z, 1.0e-4f);
    EXPECT_NEAR(vis.outside[0].tint[0], bulletDef.tint.x, 1.0e-5f);
    EXPECT_EQ(vis.outside[0].channelMask, mask);

    DecalPool      defaults;
    DecalSpawnDesc stockBullet;
    stockBullet.kind     = DecalKind::Impact;
    stockBullet.weapon   = WeaponKind::Projectile;
    stockBullet.position = Vector3f(2.0f, 0.0f, 0.0f);
    ASSERT_TRUE(defaults.spawn(stockBullet, &id));
    vis = gather(defaults, makeWideView().camera);
    ASSERT_EQ(vis.outsideCount, 1u);
    EXPECT_NEAR(vis.outside[0].worldFromDecal[0], bulletDef.halfExtents.x, 1.0e-4f);

    DecalPool stock;
    slash.position = Vector3f(1.0f, 0.0f, 0.0f);
    ASSERT_TRUE(stock.spawn(slash, &id));
    vis = gather(stock, makeWideView().camera);
    ASSERT_EQ(vis.outsideCount, 1u);
    EXPECT_NEAR(vis.outside[0].worldFromDecal[0], slashDef.halfExtents.x, 1.0e-4f);
    EXPECT_NEAR(vis.outside[0].worldFromDecal[10], slashDef.halfExtents.z, 1.0e-4f);
    EXPECT_NEAR(vis.outside[0].tint[0], slashDef.tint.x, 1.0e-5f);
    EXPECT_NEAR(vis.outside[0].roughnessTarget, slashDef.roughnessTarget, 1.0e-5f);
}

TEST(Decal_Normal_SlashCenter, GrooveAndOutside)
{
    const DecalBakedNormal center = decalBakeNormalUv(decalDef(DecalDefId::ImpactSlash).normalRecipe, 0.0f, 0.0f);
    EXPECT_GT(center.n.z, 0.8f);
    EXPECT_NEAR(center.n.Magnitude(), 1.0f, 1.0e-4f);

    const float            v      = 0.1f;
    const DecalBakedNormal inward = decalBakeNormalUv(DecalNormalRecipe::ImpactSlash, 0.0f, v);
    EXPECT_LT(inward.n.x * 0.0f + inward.n.y * v, 0.0f);

    const DecalBakedNormal flat = decalBakeNormalUv(DecalNormalRecipe::ImpactSlash, 0.0f, 0.5f);
    EXPECT_EQ(flat.r, 128);
    EXPECT_EQ(flat.g, 128);
    EXPECT_EQ(flat.b, 255);
}

TEST(Decal_LightingCount_Unchanged, StaysTen)
{
    EXPECT_EQ(SceneBuffers::kLightingCount, 10u);
}

TEST(Decal_Spawn_Rejects, BadKindAndAxis)
{
    DecalPool      pool;
    DecalId        id{};
    DecalSpawnDesc badKind;
    badKind.kind = DecalKind::Count;
    EXPECT_FALSE(pool.spawn(badKind, &id));

    DecalSpawnDesc badAxis = descAt(DecalKind::Impact, 0.0f);
    badAxis.axisY          = Vector3f(0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(pool.spawn(badAxis, &id));
    EXPECT_EQ(pool.aliveCount(), 0u);
}

TEST(Decal_Pool_FrustumCull, DropsFarBox)
{
    DecalPool pool;
    ASSERT_TRUE(pool.spawn(descAt(DecalKind::Burn, 0.0f), nullptr));
    ASSERT_TRUE(pool.spawn(descAt(DecalKind::Burn, 100000.0f), nullptr));
    const Visible vis = gather(pool, makeWideView().camera);
    ASSERT_EQ(vis.outsideCount, 1u);
    EXPECT_NEAR(vis.outside[0].worldFromDecal[12], 0.0f, 1.0e-3f);
    EXPECT_EQ(vis.insideCount, 0u);
}

TEST(Decal_Pool_MissingPoseFrees, EmptyGraph)
{
    World  world;
    Entity entity = world.createEntity();
    world.emplace<TransformComponent>(entity);
    world.emplace<AnimGraphComponent>(entity);

    DecalSpawnDesc desc = descAt(DecalKind::Blood, 0.0f);
    desc.space          = DecalSpace::Bone;
    desc.entity         = entity;
    desc.bone           = 0;

    DecalPool pool;
    DecalId   id{};
    ASSERT_TRUE(pool.spawn(desc, &id));
    pool.tick(world, 0.05f);
    EXPECT_FALSE(pool.alive(id));
    EXPECT_EQ(pool.aliveCount(), 0u);
}
