#include <gtest/gtest.h>

#include <cmath>

#include "Math/AABox3f.h"
#include "Math/MathHelper.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"
#include "Render/Camera3D.h"
#include "Render/ShadowCascades.h"

using namespace Dark;
using namespace Dark::Math;

TEST(ShadowCascades, PracticalSplitsAreMonotonic)
{
    float splits[kMaxShadowCascades];
    computePracticalSplits(1.0f, 100.0f, 3, 0.65f, splits);
    EXPECT_LT(splits[0], splits[1]);
    EXPECT_LT(splits[1], splits[2]);
    EXPECT_GT(splits[0], 1.0f);
    EXPECT_LE(splits[2], 100.0f + 1.0e-4f);
}

TEST(ShadowCascades, UniformSplitsMatchEvenDepth)
{
    float splits[kMaxShadowCascades];
    computePracticalSplits(1.0f, 100.0f, 3, 0.0f, splits);
    EXPECT_NEAR(splits[0], 34.0f, 1.0e-3f);
    EXPECT_NEAR(splits[1], 67.0f, 1.0e-3f);
    EXPECT_NEAR(splits[2], 100.0f, 1.0e-3f);
}

TEST(ShadowCascades, LogSplitsAreCloserThanUniform)
{
    float uni[kMaxShadowCascades];
    float logSplits[kMaxShadowCascades];
    computePracticalSplits(1.0f, 100.0f, 3, 0.0f, uni);
    computePracticalSplits(1.0f, 100.0f, 3, 1.0f, logSplits);
    EXPECT_LT(logSplits[0], uni[0]);
    EXPECT_LT(logSplits[1], uni[1]);
}

TEST(ShadowCascades, FrustumCornersScaleWithDistance)
{
    Camera3D cam;
    cam.SetLens(1.04719755f, 1.0f, 1.0f, 100.0f);
    cam.LookAt(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));

    Vector3f nearC[8];
    Vector3f farC[8];
    extractFrustumCorners(cam, 1.0f, 1.0f, nearC);
    extractFrustumCorners(cam, 10.0f, 10.0f, farC);

    const float tanHalf = tanf(0.5f * cam.GetFovY());
    EXPECT_NEAR(nearC[0].z, 1.0f, 1.0e-4f);
    EXPECT_NEAR(farC[0].z, 10.0f, 1.0e-4f);
    EXPECT_NEAR(fabsf(nearC[0].x), tanHalf, 1.0e-3f);
    EXPECT_NEAR(fabsf(farC[0].x), 10.0f * tanHalf, 1.0e-3f);
}

TEST(ShadowCascades, PackWritesSplitsBiasAndLook)
{
    CascadeData cascades[kMaxShadowCascades];
    cascades[0].splitFar = 12.0f;
    cascades[1].splitFar = 48.0f;
    cascades[2].splitFar = 200.0f;
    cascades[0].zRange   = 40.0f;
    cascades[1].zRange   = 80.0f;
    cascades[2].zRange   = 200.0f;

    ShadowConstants cb{};
    packShadowConstants(cb, cascades, 3, 2048, 0.05f, 1.25f, Vector3f(0.0f, 0.0f, 1.0f), 3.0f);
    EXPECT_FLOAT_EQ(cb.cascadeSplits[0], 12.0f);
    EXPECT_FLOAT_EQ(cb.cascadeSplits[1], 48.0f);
    EXPECT_FLOAT_EQ(cb.cascadeSplits[2], 200.0f);
    EXPECT_FLOAT_EQ(cb.cascadeSplits[3], 2048.0f);
    EXPECT_FLOAT_EQ(cb.params[0], 0.05f);
    EXPECT_FLOAT_EQ(cb.params[1], 1.0f); // clamped
    EXPECT_FLOAT_EQ(cb.params[2], 3.0f);
    EXPECT_FLOAT_EQ(cb.params[3], 3.0f); // ping-pong slice offset
    EXPECT_FLOAT_EQ(cb.cameraLook[2], 1.0f);
    EXPECT_NEAR(cb.cascadeInvZ[0], 1.0f / 40.0f, 1.0e-6f);
    EXPECT_NEAR(cb.cascadeInvZ[1], 1.0f / 80.0f, 1.0e-6f);
    EXPECT_NEAR(cb.cascadeInvZ[2], 1.0f / 200.0f, 1.0e-6f);
}

TEST(ShadowCascades, BuildCascadeRejectsZeroLight)
{
    Vector3f corners[8];
    for (int i = 0; i < 8; ++i)
        corners[i] = Vector3f(0.0f, 0.0f, 0.0f);
    CascadeData out;
    Aabb3f scene(Vector3f(-1.0f, -1.0f, -1.0f), Vector3f(1.0f, 1.0f, 1.0f));
    EXPECT_FALSE(buildCascadeMatrix(corners, Vector3f(0.0f, 0.0f, 0.0f), scene, 10.0f, 1024, out));
}

TEST(ShadowCascades, BuildCascadePlacesSliceCenterInClip)
{
    Camera3D cam;
    cam.SetLens(1.04719755f, 1.6f, 1.0f, 80.0f);
    cam.LookAt(Vector3f(0.0f, 12.0f, -20.0f), Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));

    Vector3f corners[8];
    extractFrustumCorners(cam, 2.0f, 30.0f, corners);

    Aabb3f scene(Vector3f(-40.0f, -5.0f, -40.0f), Vector3f(40.0f, 30.0f, 40.0f));
    CascadeData out;
    ASSERT_TRUE(buildCascadeMatrix(corners, Vector3f(0.35f, 0.85f, -0.35f), scene, 40.0f, 1024, out));

    // Squaring + texel snap expands the ortho, so every slice corner stays in clip XY.
    for (int i = 0; i < 8; ++i)
    {
        const Vector4f clip = out.viewProj * Vector4f(corners[i].x, corners[i].y, corners[i].z, 1.0f);
        EXPECT_LE(fabsf(clip.x), 1.0f + 2.0f / 1024.0f);
        EXPECT_LE(fabsf(clip.y), 1.0f + 2.0f / 1024.0f);
        EXPECT_GE(clip.z, 0.0f);
        EXPECT_LE(clip.z, 1.0f);
    }

    CascadeData again;
    ASSERT_TRUE(buildCascadeMatrix(corners, Vector3f(0.35f, 0.85f, -0.35f), scene, 40.0f, 1024, again));
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(out.viewProj.m_afEntry[i], again.viewProj.m_afEntry[i]);
}

TEST(ShadowCascades, WorldPointUvMovesWhenCameraTranslates)
{
    Camera3D cam;
    cam.SetLens(1.04719755f, 1.6f, 0.5f, 2000.0f);
    cam.LookAt(Vector3f(0.0f, 20.0f, -40.0f), Vector3f(0.0f, 5.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));

    Aabb3f scene(Vector3f(-128.0f, 0.0f, -128.0f), Vector3f(128.0f, 22.0f, 128.0f));
    const Vector3f light(0.35f, 0.85f, -0.35f);
    const Vector3f worldPt(0.0f, 5.0f, 0.0f);

    auto clipXy = [&](const Camera3D& c) {
        Vector3f corners[8];
        extractFrustumCorners(c, 1.0f, 50.0f, corners);
        CascadeData out;
        EXPECT_TRUE(buildCascadeMatrix(corners, light, scene, 24.0f, 2048, out));
        const Vector4f clip = out.viewProj * Vector4f(worldPt.x, worldPt.y, worldPt.z, 1.0f);
        const float    w    = Max(fabsf(clip.w), 1.0e-5f);
        return Vector3f(clip.x / w, clip.y / w, 0.0f);
    };

    const Vector3f uv0 = clipXy(cam);
    cam.Strafe(30.0f);
    const Vector3f uv1 = clipXy(cam);
    const float    dx  = uv1.x - uv0.x;
    const float    dy  = uv1.y - uv0.y;
    EXPECT_GT(dx * dx + dy * dy, 0.0025f);
}

TEST(ShadowCascades, ConstantsSizeMatchesGpuLayout)
{
    EXPECT_EQ(sizeof(ShadowConstants), static_cast<size_t>(64 * sizeof(float)));
}

TEST(ShadowCascades, LargeTerrainBoundsPreserveMeterScaleDepth)
{
    Camera3D cam;
    cam.SetLens(1.04719755f, 1.6f, 0.5f, 2000.0f);
    cam.LookAt(Vector3f(0.0f, 48.0f, -86.0f), Vector3f(0.0f, 8.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));

    Vector3f corners[8];
    extractFrustumCorners(cam, 80.0f, 280.0f, corners);

    Aabb3f scene(Vector3f(-128.0f, 0.0f, -128.0f), Vector3f(128.0f, 22.0f, 128.0f));
    CascadeData out;
    const Vector3f light(0.40f, 0.60f, 0.30f);
    ASSERT_TRUE(buildCascadeMatrix(corners, light, scene, 24.0f, 2048, out));
    EXPECT_GT(out.zRange, 1.0f);

    Vector3f lightN = light;
    lightN.Normalize();
    const Vector3f ground(0.0f, 8.0f, 0.0f);
    const Vector3f towardLight = ground + lightN * 1.0f;
    const Vector4f gClip = out.viewProj * Vector4f(ground.x, ground.y, ground.z, 1.0f);
    const Vector4f cClip = out.viewProj * Vector4f(towardLight.x, towardLight.y, towardLight.z, 1.0f);
    const float gZ = gClip.z / Max(fabsf(gClip.w), 1.0e-5f);
    const float cZ = cClip.z / Max(fabsf(cClip.w), 1.0e-5f);
    EXPECT_LT(cZ, gZ);
    // 1m along the light must remain larger than the old 0.0015 NDC bias and
    // the new world-space 0.05m bias after conversion.
    const float ndcBias = 0.05f / Max(out.zRange, 1.0f);
    EXPECT_GT(gZ - cZ, ndcBias * 4.0f);
}
