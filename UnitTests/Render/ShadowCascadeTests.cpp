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

    ShadowConstants cb{};
    packShadowConstants(cb, cascades, 3, 2048, 0.0015f, 1.25f, Vector3f(0.0f, 0.0f, 1.0f));
    EXPECT_FLOAT_EQ(cb.cascadeSplits[0], 12.0f);
    EXPECT_FLOAT_EQ(cb.cascadeSplits[1], 48.0f);
    EXPECT_FLOAT_EQ(cb.cascadeSplits[2], 200.0f);
    EXPECT_FLOAT_EQ(cb.cascadeSplits[3], 2048.0f);
    EXPECT_FLOAT_EQ(cb.params[0], 0.0015f);
    EXPECT_FLOAT_EQ(cb.params[1], 1.0f); // clamped
    EXPECT_FLOAT_EQ(cb.params[2], 3.0f);
    EXPECT_FLOAT_EQ(cb.cameraLook[2], 1.0f);
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

TEST(ShadowCascades, ConstantsSizeMatchesGpuLayout)
{
    EXPECT_EQ(sizeof(ShadowConstants), static_cast<size_t>(60 * sizeof(float)));
}
