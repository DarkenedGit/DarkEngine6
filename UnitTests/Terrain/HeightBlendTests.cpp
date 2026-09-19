#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Render/Octahedral.h"
#include "Terrain/HeightBlend.h"

#include <cmath>

using namespace Dark;
using namespace Dark::Math;
using namespace Dark::Terrain;

TEST(HeightBlend, Renormalize)
{
    const float H[kMaxTerrainLayers]{ 0.10f, 0.80f, 0.20f, 0.00f };
    const float w[kMaxTerrainLayers]{ 0.25f, 0.25f, 0.25f, 0.25f };
    const float k = 0.5f;
    const float t = 0.1f;
    float       outW[kMaxTerrainLayers]{};
    ASSERT_TRUE(heightBlendWeights(H, w, k, t, outW));

    float sum = 0.0f;
    for (int i = 0; i < kMaxTerrainLayers; ++i)
        sum += outW[i];
    EXPECT_NEAR(sum, 1.0f, 1.0e-5f);

    float h[kMaxTerrainLayers];
    float hMax = -Infinity;
    for (int i = 0; i < kMaxTerrainLayers; ++i)
    {
        h[i] = H[i] + k * w[i];
        if (h[i] > hMax)
            hMax = h[i];
    }
    for (int i = 0; i < kMaxTerrainLayers; ++i)
    {
        if (outW[i] > 1.0e-5f)
            EXPECT_GE(h[i] + 1.0e-5f, hMax - t);
    }
}

TEST(HeightBlend, KZero_EqualsSplat)
{
    const float H[kMaxTerrainLayers]{ 0.9f, 0.1f, 0.4f, 0.0f };
    const float w[kMaxTerrainLayers]{ 0.5f, 0.25f, 0.25f, 0.0f };
    const float expected[kMaxTerrainLayers]{ 0.5f, 0.25f, 0.25f, 0.0f };

    float out0[kMaxTerrainLayers]{};
    float outNeg[kMaxTerrainLayers]{};
    ASSERT_TRUE(heightBlendWeights(H, w, 0.0f, 0.1f, out0));
    ASSERT_TRUE(heightBlendWeights(H, w, -1.0f, 0.1f, outNeg));
    for (int i = 0; i < kMaxTerrainLayers; ++i)
    {
        EXPECT_NEAR(out0[i], expected[i], 1.0e-5f);
        EXPECT_NEAR(outNeg[i], expected[i], 1.0e-5f);
    }

    const float unnorm[kMaxTerrainLayers]{ 2.0f, 2.0f, 0.0f, 0.0f };
    float       outUnnorm[kMaxTerrainLayers]{};
    ASSERT_TRUE(heightBlendWeights(H, unnorm, 0.0f, 0.1f, outUnnorm));
    EXPECT_NEAR(outUnnorm[0], 0.5f, 1.0e-5f);
    EXPECT_NEAR(outUnnorm[1], 0.5f, 1.0e-5f);
    EXPECT_NEAR(outUnnorm[2], 0.0f, 1.0e-5f);
    EXPECT_NEAR(outUnnorm[3], 0.0f, 1.0e-5f);
}

TEST(HeightBlend, TClamped)
{
    const float H[kMaxTerrainLayers]{ 0.1f, 0.8f, 0.2f, 0.0f };
    const float w[kMaxTerrainLayers]{ 0.25f, 0.25f, 0.25f, 0.25f };
    float       outW[kMaxTerrainLayers]{};
    ASSERT_TRUE(heightBlendWeights(H, w, 0.5f, 0.0f, outW));
    float sum = 0.0f;
    for (int i = 0; i < kMaxTerrainLayers; ++i)
    {
        EXPECT_TRUE(std::isfinite(outW[i]));
        sum += outW[i];
    }
    EXPECT_TRUE(std::isfinite(sum));
    EXPECT_NEAR(sum, 1.0f, 1.0e-5f);
}

TEST(HeightBlend, NullOutW_ReturnsFalse)
{
    const float H[kMaxTerrainLayers]{ 1.0f, 1.0f, 1.0f, 1.0f };
    const float w[kMaxTerrainLayers]{ 1.0f, 0.0f, 0.0f, 0.0f };
    EXPECT_FALSE(heightBlendWeights(H, w, 0.5f, 0.1f, nullptr));

    float outW[kMaxTerrainLayers]{ 9.0f, 9.0f, 9.0f, 9.0f };
    ASSERT_TRUE(heightBlendWeights(nullptr, w, 0.5f, 0.1f, outW));
    float sum = 0.0f;
    for (int i = 0; i < kMaxTerrainLayers; ++i)
    {
        EXPECT_TRUE(std::isfinite(outW[i]));
        sum += outW[i];
    }
    EXPECT_NEAR(sum, 1.0f, 1.0e-5f);

    ASSERT_TRUE(heightBlendWeights(H, nullptr, 0.0f, 0.1f, outW));
    EXPECT_NEAR(outW[0] + outW[1] + outW[2] + outW[3], 0.0f, 1.0e-5f);
}

TEST(HeightBlend, Whiteout_DiffersFromLerpRgb)
{
    Vector3f n[kMaxTerrainLayers]{
        Vector3f(0.80f, 0.00f, 0.60f),
        Vector3f(0.00f, 0.80f, 0.60f),
        Vector3f(0.00f, 0.00f, 1.00f),
        Vector3f(0.00f, 0.00f, 1.00f),
    };
    n[0].Normalize();
    n[1].Normalize();
    const float w[kMaxTerrainLayers]{ 0.5f, 0.5f, 0.0f, 0.0f };

    const Vector3f wu = whiteoutBlend(n, w);
    const Vector3f lr = lerpRgbNormal(n, w);
    EXPECT_GT((wu - lr).MagnitudeSqrd(), 1.0e-8f);

    // Unweighted two-map Whiteout vs RGB lerp: multiplied z keeps more detail.
    Vector3f twoMap(n[0].x + n[1].x, n[0].y + n[1].y, n[0].z * n[1].z);
    twoMap.Normalize();
    Vector3f rgb = n[0] * 0.5f + n[1] * 0.5f;
    rgb.Normalize();
    EXPECT_GT((twoMap - rgb).MagnitudeSqrd(), 1.0e-8f);
    EXPECT_LT(twoMap.z, rgb.z);
}

TEST(HeightBlend, WhiteoutNormal_FlatTbn)
{
    const Vector3f geomN(0.0f, 1.0f, 0.0f);
    const Vector3f up = whiteoutNormal(geomN, Vector3f(0.0f, 0.0f, 1.0f));
    EXPECT_NEAR(up.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(up.y, 1.0f, 1.0e-5f);
    EXPECT_NEAR(up.z, 0.0f, 1.0e-5f);

    const Vector3f alongU = whiteoutNormal(geomN, Vector3f(1.0f, 0.0f, 0.0f));
    EXPECT_NEAR(alongU.x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(alongU.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(alongU.z, 0.0f, 1.0e-5f);

    EXPECT_TRUE(whiteoutBlend(nullptr, nullptr).z > 0.5f);
    EXPECT_NEAR(lerpRgbNormals(nullptr, nullptr).z, 1.0f, 1.0e-5f);
}

TEST(HeightBlend, TerrainTiling_WorldScale)
{
    EXPECT_NEAR(layerTilingWorldScale(24.0f, 256.0f, 256.0f), 24.0f / 256.0f, 1.0e-6f);
    EXPECT_NEAR(layerTilingWorldScale(24.0f, 256.0f, 128.0f), 24.0f / 256.0f, 1.0e-6f);
    EXPECT_EQ(layerTilingWorldScale(24.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_EQ(layerTilingWorldScale(24.0f, -10.0f, -20.0f), 0.0f);
    EXPECT_EQ(layerTilingWorldScale(8.0f, 1.0e-4f, 1.0e-4f), 0.0f);
}

TEST(HeightBlend, TerrainAttrib_Pack_OctRoughMetal)
{
    const Vector3f n(0.2f, 0.8f, 0.4f);
    Vector3f       unit = n;
    unit.Normalize();
    const Vector2f oct = encodeOct(unit);
    const Vector4f packed = packTerrainAttrib(unit, 0.37f, 0.81f);
    EXPECT_NEAR(packed.x, oct.x, 1.0e-6f);
    EXPECT_NEAR(packed.y, oct.y, 1.0e-6f);
    EXPECT_NEAR(packed.z, 0.37f, 1.0e-6f);
    EXPECT_NEAR(packed.w, 0.81f, 1.0e-6f);
}
