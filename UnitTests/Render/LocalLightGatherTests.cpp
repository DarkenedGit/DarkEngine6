#include <gtest/gtest.h>

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/MathHelper.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"
#include "Render/Camera3D.h"
#include "Render/Frustum3f.h"
#include "Render/LocalLightGather.h"

#include <cmath>
#include <unordered_set>

using namespace Dark;
using namespace Dark::Math;

namespace
{
    float spotCosTheta(const Vector3f& lightPos, const Vector3f& worldPos, const Vector3f& spotDirTowardBase)
    {
        Vector3f toLight = lightPos - worldPos;
        const float d = toLight.Magnitude();
        if (d <= 1.0e-4f)
            return 1.0f;
        toLight *= 1.0f / d;
        return (-toLight).Dot(spotDirTowardBase);
    }

    void makeView(Camera3D& cam, Matrix4f& viewProj, Frustum3f& frustum)
    {
        cam.SetPosition(Vector3f(0.0f, 0.0f, 0.0f));
        cam.LookAt(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));
        cam.SetLens(DegreesToRadians(60.0f), 2560.0f / 1600.0f, 0.18f, 2000.0f);
        viewProj = cam.GetViewProj();
        frustum  = Frustum3f(viewProj);
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

    Entity addLight(World& world, const Vector3f& pos, LocalLightType type, float intensity, float range, const Quaternion& rot = Quaternion::IDENTITY)
    {
        Entity e = world.createEntity();
        auto& xf = world.emplace<TransformComponent>(e);
        xf.position = pos;
        xf.rotation = rot;
        auto& light = world.emplace<LocalLightComponent>(e);
        light.type      = type;
        light.intensity = intensity;
        light.range     = range;
        light.enabled   = true;
        return e;
    }
} // namespace

TEST(LocalLightGather, MissingFrustumOrViewProjReturnsFalse)
{
    World world;
    LocalLightDrawLists out{};
    LocalLightCullInput in{};
    EXPECT_FALSE(gatherLocalLights(world, in, out));

    Camera3D cam;
    Matrix4f viewProj;
    Frustum3f frustum;
    makeView(cam, viewProj, frustum);

    in.frustum = &frustum;
    EXPECT_FALSE(gatherLocalLights(world, in, out));

    in.viewProj = &viewProj;
    in.frustum  = nullptr;
    EXPECT_FALSE(gatherLocalLights(world, in, out));
}

TEST(LocalLightGather, NearPlaneClassification)
{
    World world;
    addLight(world, Vector3f(0.0f, 0.0f, 0.0f), LocalLightType::Point, 600.0f, 8.0f);
    addLight(world, Vector3f(0.0f, 0.0f, 100.0f), LocalLightType::Point, 600.0f, 8.0f);
    addLight(world, Vector3f(0.0f, 0.0f, 10.0f), LocalLightType::Point, 600.0f, 9.9f);

    Camera3D cam;
    Matrix4f viewProj;
    Frustum3f frustum;
    makeView(cam, viewProj, frustum);
    const LocalLightCullInput in = makeInput(frustum, viewProj);

    LocalLightDrawLists out{};
    ASSERT_TRUE(gatherLocalLights(world, in, out));
    EXPECT_EQ(out.count, 3u);
    EXPECT_EQ(out.pointOutCount, 1u);
    EXPECT_EQ(out.spotOutCount, 0u);
    EXPECT_EQ(out.insideCount, 2u);
    EXPECT_EQ(out.pointOutCount + out.spotOutCount + out.insideCount, out.count);

    EXPECT_NEAR(out.lights[0].pos[2], 100.0f, 1.0e-3f);
    EXPECT_NEAR(out.lights[1].pos[2], 0.0f, 1.0e-3f);
    EXPECT_NEAR(out.lights[2].pos[2], 10.0f, 1.0e-3f);
}

TEST(LocalLightGather, InsideAndOutsideAreDisjoint)
{
    World world;
    addLight(world, Vector3f(0.0f, 0.0f, 0.0f), LocalLightType::Point, 400.0f, 8.0f);
    addLight(world, Vector3f(2.0f, 0.0f, 40.0f), LocalLightType::Point, 500.0f, 8.0f);
    addLight(world, Vector3f(-2.0f, 0.0f, 40.0f), LocalLightType::Spot, 800.0f, 16.0f);
    addLight(world, Vector3f(0.0f, 0.0f, 0.2f), LocalLightType::Spot, 500.0f, 22.0f);

    Camera3D cam;
    Matrix4f viewProj;
    Frustum3f frustum;
    makeView(cam, viewProj, frustum);
    const LocalLightCullInput in = makeInput(frustum, viewProj);

    LocalLightDrawLists out{};
    ASSERT_TRUE(gatherLocalLights(world, in, out));
    ASSERT_EQ(out.pointOutCount + out.spotOutCount + out.insideCount, out.count);

    std::unordered_set<uint32_t> seen;
    auto keyOf = [](const GpuLocalLight& l) {
        return static_cast<uint32_t>(l.pos[0] * 1000.0f + 50.0f) ^ (static_cast<uint32_t>(l.pos[2] * 1000.0f) << 8);
    };
    for (uint32_t i = 0; i < out.count; ++i)
    {
        EXPECT_TRUE(seen.insert(keyOf(out.lights[i])).second) << "light packed twice at index " << i;
    }

    for (uint32_t i = 0; i < out.insideCount; ++i)
    {
        const uint32_t gpu = out.pointOutCount + out.spotOutCount + i;
        EXPECT_LT(gpu, out.count);
        EXPECT_LT(out.insideScissor[i].left, out.insideScissor[i].right);
        EXPECT_LT(out.insideScissor[i].top, out.insideScissor[i].bottom);
        (void)out.lights[gpu];
    }
}

TEST(LocalLightGather, WaterIndexUsesCompactedTopScores)
{
    World world;
    for (int i = 1; i <= 10; ++i)
    {
        addLight(world, Vector3f(static_cast<float>(i) * 0.1f, 0.0f, 50.0f), LocalLightType::Point, static_cast<float>(i) * 100.0f, 8.0f);
    }

    Camera3D cam;
    Matrix4f viewProj;
    Frustum3f frustum;
    makeView(cam, viewProj, frustum);
    const LocalLightCullInput in = makeInput(frustum, viewProj);

    LocalLightDrawLists out{};
    ASSERT_TRUE(gatherLocalLights(world, in, out));
    ASSERT_EQ(out.count, 10u);
    EXPECT_EQ(out.pointOutCount, 10u);
    EXPECT_EQ(out.insideCount, 0u);
    ASSERT_EQ(out.waterCount, kWaterLocalLightMax);

    std::unordered_set<uint32_t> waterSlots;
    float minWater = 1.0e9f;
    for (uint32_t i = 0; i < out.waterCount; ++i)
    {
        const uint32_t idx = out.waterIndex[i];
        EXPECT_LT(idx, out.count);
        EXPECT_TRUE(waterSlots.insert(idx).second);
        minWater = Min(minWater, out.lights[idx].color[0]);
    }
    EXPECT_GE(minWater, 300.0f - 1.0e-3f);

    uint32_t below = 0;
    for (uint32_t i = 0; i < out.count; ++i)
    {
        if (out.lights[i].color[0] < 300.0f - 1.0e-3f)
            ++below;
    }
    EXPECT_EQ(below, 2u);
}

TEST(LocalLightGather, SpotVolumeMapsUnitZToPosPlusDirRange)
{
    World world;
    const Vector3f pos(1.0f, 2.0f, 3.0f);
    const float    range = 10.0f;
    addLight(world, pos, LocalLightType::Spot, 800.0f, range, Quaternion::IDENTITY);

    Camera3D cam;
    Matrix4f viewProj;
    Frustum3f frustum;
    makeView(cam, viewProj, frustum);
    const LocalLightCullInput in = makeInput(frustum, viewProj);

    LocalLightDrawLists out{};
    ASSERT_TRUE(gatherLocalLights(world, in, out));
    ASSERT_GE(out.count, 1u);

    uint32_t spot = 0;
    for (uint32_t i = 0; i < out.count; ++i)
    {
        if (out.lights[i].type > 0.5f)
            spot = i;
    }
    EXPECT_NEAR(out.lights[spot].type, 1.0f, 1.0e-5f);
    EXPECT_NEAR(out.lights[spot].dir[2], 1.0f, 1.0e-4f);

    const Vector4f mapped = out.volumeWorld[spot] * Vector4f(0.0f, 0.0f, 1.0f, 1.0f);
    EXPECT_NEAR(mapped.x, pos.x, 1.0e-3f);
    EXPECT_NEAR(mapped.y, pos.y, 1.0e-3f);
    EXPECT_NEAR(mapped.z, pos.z + range, 1.0e-3f);

    const Vector3f lightPos(out.lights[spot].pos[0], out.lights[spot].pos[1], out.lights[spot].pos[2]);
    const Vector3f dir(out.lights[spot].dir[0], out.lights[spot].dir[1], out.lights[spot].dir[2]);
    const Vector3f onAxis = lightPos + dir * (range * 0.5f);
    EXPECT_NEAR(spotCosTheta(lightPos, onAxis, dir), 1.0f, 1.0e-4f);
}

TEST(LocalLightGather, DegenerateOuterTreatedAsPoint)
{
    World world;
    Entity e = addLight(world, Vector3f(0.0f, 0.0f, 40.0f), LocalLightType::Spot, 400.0f, 8.0f);
    world.get<LocalLightComponent>(e)->outerConeDeg = 0.0f;

    Camera3D cam;
    Matrix4f viewProj;
    Frustum3f frustum;
    makeView(cam, viewProj, frustum);

    LocalLightDrawLists out{};
    ASSERT_TRUE(gatherLocalLights(world, makeInput(frustum, viewProj), out));
    ASSERT_EQ(out.count, 1u);
    EXPECT_EQ(out.pointOutCount, 1u);
    EXPECT_EQ(out.spotOutCount, 0u);
    EXPECT_NEAR(out.lights[0].type, 0.0f, 1.0e-5f);
    EXPECT_NEAR(out.lights[0].dir[0], 0.0f, 1.0e-5f);
    EXPECT_NEAR(out.lights[0].dir[1], 0.0f, 1.0e-5f);
    EXPECT_NEAR(out.lights[0].dir[2], 0.0f, 1.0e-5f);
}

TEST(LocalLightGather, SkipsDisabledAndMissingTransform)
{
    World world;
    Entity disabled = addLight(world, Vector3f(0.0f, 0.0f, 20.0f), LocalLightType::Point, 600.0f, 8.0f);
    world.get<LocalLightComponent>(disabled)->enabled = false;

    Entity noXf = world.createEntity();
    world.emplace<LocalLightComponent>(noXf);

    addLight(world, Vector3f(0.0f, 0.0f, 30.0f), LocalLightType::Point, 600.0f, 8.0f);

    Camera3D cam;
    Matrix4f viewProj;
    Frustum3f frustum;
    makeView(cam, viewProj, frustum);

    LocalLightDrawLists out{};
    ASSERT_TRUE(gatherLocalLights(world, makeInput(frustum, viewProj), out));
    EXPECT_EQ(out.count, 1u);
    EXPECT_NEAR(out.lights[0].pos[2], 30.0f, 1.0e-3f);
}
