#include <gtest/gtest.h>

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/MathHelper.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Render/Camera3D.h"
#include "Render/Frustum3f.h"
#include "Render/LocalLightGather.h"
#include "Render/SsrSettings.h"
#include "Render/WaterPipeline.h"
#include "Sky/Environment.h"

#include <cstddef>
#include <unordered_set>

using namespace Dark;
using namespace Dark::Math;

namespace
{
    void makeView(Camera3D& cam, Matrix4f& viewProj, Frustum3f& frustum)
    {
        cam.SetPosition(Vector3f(0.0f, 0.0f, 0.0f));
        cam.LookAt(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));
        cam.SetLens(DegreesToRadians(60.0f), 2560.0f / 1600.0f, 0.18f, 2000.0f);
        viewProj = cam.GetViewProj();
        frustum  = Frustum3f(cam.GetCullViewProj());
    }

    LocalLightCullInput makeInput(const Frustum3f& frustum, const Matrix4f& viewProj)
    {
        LocalLightCullInput in{};
        in.frustum    = &frustum;
        in.viewProj   = &viewProj;
        in.cameraPos  = Vector3f(0.0f, 0.0f, 0.0f);
        in.cameraLook = Vector3f(0.0f, 0.0f, 1.0f);
        in.nearZ      = 0.18f;
        in.maxOut     = kMaxLocalLights;
        in.maxRange   = 80.0f;
        in.viewportW  = 2560;
        in.viewportH  = 1600;
        return in;
    }

    Entity addLight(World& world, const Vector3f& pos, float intensity, float range)
    {
        Entity e = world.createEntity();
        auto& xf = world.emplace<TransformComponent>(e);
        xf.position = pos;
        auto& light = world.emplace<LocalLightComponent>(e);
        light.type      = LocalLightType::Point;
        light.intensity = intensity;
        light.range     = range;
        light.enabled   = true;
        return e;
    }
} // namespace

TEST(WaterLightPick, FrameConstantsKeepLayoutThenGainLightIndices)
{
    EXPECT_EQ(offsetof(WaterFrameConstants, lightCount), 64u * sizeof(float));
    EXPECT_EQ(offsetof(WaterFrameConstants, waterIndex), 64u * sizeof(float) + sizeof(uint32_t));
    EXPECT_EQ(offsetof(WaterFrameConstants, fogColor), 64u * sizeof(float) + sizeof(uint32_t) * (1u + kWaterLocalLightMax));
    EXPECT_EQ(offsetof(WaterFrameConstants, invViewProj), 96u * sizeof(float));
    EXPECT_EQ(offsetof(WaterFrameConstants, ssrEnabled), 129u * sizeof(float));
    EXPECT_EQ(offsetof(WaterFrameConstants, skyEval), 136u * sizeof(float));
    EXPECT_EQ((sizeof(WaterFrameConstants) + 255u) & ~255u, 768u);
    EXPECT_EQ(WaterPipeline::kRootSsrSrv, 5u);
    EXPECT_EQ(kWaterLocalLightMax, 8u);
    WaterFrameConstants cb{};
    EXPECT_EQ(cb.lightCount, 0u);
    EXPECT_EQ(cb.waterIndex[0], 0u);
    EXPECT_EQ(cb.waterIndex[7], 0u);
}

TEST(WaterLightPick, FillConstantsZerosLightPickAndHonorsLightingFlag)
{
    WaterFrameConstants cb{};
    cb.lightCount    = 7;
    cb.waterIndex[3] = 9;
    float wvp[16]{};
    float cam[3]{};
    float light[3]{ 0.0f, 1.0f, 0.0f };
    WaterParams params{};
    WaterPipeline::fillConstants(cb, wvp, cam, 0.0f, light, params, nullptr, true);
    EXPECT_EQ(cb.lightCount, 0u);
    EXPECT_EQ(cb.waterIndex[3], 0u);
    EXPECT_GT(cb.specPower, 0.0f);

    WaterPipeline::fillConstants(cb, wvp, cam, 0.0f, light, params, nullptr, false);
    EXPECT_LT(cb.specPower, 0.0f);
    EXPECT_EQ(cb.lightCount, 0u);
}

TEST(WaterLightPick, FillSsrWritesEnabledAndSkyEval)
{
    WaterFrameConstants cb{};
    Camera3D            cam;
    cam.SetPosition(Vector3f(0.0f, 2.0f, 0.0f));
    cam.LookAt(Vector3f(0.0f, 2.0f, 0.0f), Vector3f(0.0f, 2.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));
    cam.SetLens(DegreesToRadians(60.0f), 16.0f / 9.0f, 0.18f, 2000.0f);

    SsrSettings settings{};
    settings.enabled = true;
    Sky::Environment env{};
    env.evaluate();

    WaterPipeline::fillSsr(cb, cam, &settings, true, true, &env);
    EXPECT_FLOAT_EQ(cb.ssrEnabled, 1.0f);
    EXPECT_FLOAT_EQ(cb.nearZ, 0.18f);
    EXPECT_FLOAT_EQ(cb.thickness, 0.2f);
    EXPECT_FLOAT_EQ(cb.stride, 2.0f);
    EXPECT_FLOAT_EQ(cb.edgeFade, 0.1f);
    EXPECT_FLOAT_EQ(cb.maxRoughness, 0.4f);
    EXPECT_FLOAT_EQ(cb.skyEval.coverage, env.weather.cloudCoverage);
    EXPECT_FLOAT_EQ(cb.skyEval.exposure, 1.0f);
    EXPECT_GT(cb.skyEval.sunDir[0] * cb.skyEval.sunDir[0] + cb.skyEval.sunDir[1] * cb.skyEval.sunDir[1] + cb.skyEval.sunDir[2] * cb.skyEval.sunDir[2], 0.5f);

    WaterPipeline::fillSsr(cb, cam, &settings, false, true, &env);
    EXPECT_FLOAT_EQ(cb.ssrEnabled, 0.0f);

    settings.enabled = false;
    WaterPipeline::fillSsr(cb, cam, &settings, true, true, &env);
    EXPECT_FLOAT_EQ(cb.ssrEnabled, 0.0f);

    settings.enabled = true;
    WaterPipeline::fillSsr(cb, cam, &settings, true, false, &env);
    EXPECT_FLOAT_EQ(cb.ssrEnabled, 0.0f);

    WaterPipeline::fillSsr(cb, cam, nullptr, true, true, &env);
    EXPECT_FLOAT_EQ(cb.ssrEnabled, 0.0f);
}

TEST(WaterLightPick, EmptyWorldHasZeroWaterCount)
{
    World world;
    Camera3D cam;
    Matrix4f viewProj;
    Frustum3f frustum;
    makeView(cam, viewProj, frustum);

    LocalLightDrawLists out{};
    ASSERT_TRUE(gatherLocalLights(world, makeInput(frustum, viewProj), out));
    EXPECT_EQ(out.count, 0u);
    EXPECT_EQ(out.waterCount, 0u);
}

TEST(WaterLightPick, CompactedIndicesOfTopEightScores)
{
    World world;
    for (int i = 1; i <= 12; ++i)
        addLight(world, Vector3f(static_cast<float>(i) * 0.1f, 0.0f, 50.0f), static_cast<float>(i) * 100.0f, 8.0f);

    Camera3D cam;
    Matrix4f viewProj;
    Frustum3f frustum;
    makeView(cam, viewProj, frustum);

    LocalLightDrawLists out{};
    ASSERT_TRUE(gatherLocalLights(world, makeInput(frustum, viewProj), out));
    ASSERT_EQ(out.count, 12u);
    ASSERT_EQ(out.waterCount, kWaterLocalLightMax);

    std::unordered_set<uint32_t> slots;
    float minKept = 1.0e9f;
    for (uint32_t i = 0; i < out.waterCount; ++i)
    {
        const uint32_t idx = out.waterIndex[i];
        EXPECT_LT(idx, out.count);
        EXPECT_TRUE(slots.insert(idx).second);
        minKept = Min(minKept, out.lights[idx].color[0]);
    }
    EXPECT_EQ(slots.size(), kWaterLocalLightMax);
    EXPECT_GE(minKept, 500.0f - 1.0e-3f);

    uint32_t dropped = 0;
    for (uint32_t i = 0; i < out.count; ++i)
    {
        if (out.lights[i].color[0] < 500.0f - 1.0e-3f)
            ++dropped;
    }
    EXPECT_EQ(dropped, 4u);
}

TEST(WaterLightPick, FewerThanEightKeepsAllCompactedSlots)
{
    World world;
    addLight(world, Vector3f(0.0f, 0.0f, 40.0f), 200.0f, 8.0f);
    addLight(world, Vector3f(0.2f, 0.0f, 40.0f), 800.0f, 8.0f);
    addLight(world, Vector3f(0.4f, 0.0f, 40.0f), 400.0f, 8.0f);

    Camera3D cam;
    Matrix4f viewProj;
    Frustum3f frustum;
    makeView(cam, viewProj, frustum);

    LocalLightDrawLists out{};
    ASSERT_TRUE(gatherLocalLights(world, makeInput(frustum, viewProj), out));
    ASSERT_EQ(out.count, 3u);
    ASSERT_EQ(out.waterCount, 3u);

    std::unordered_set<uint32_t> slots;
    for (uint32_t i = 0; i < out.waterCount; ++i)
    {
        EXPECT_LT(out.waterIndex[i], out.count);
        EXPECT_TRUE(slots.insert(out.waterIndex[i]).second);
    }
    EXPECT_EQ(slots.size(), 3u);
}
