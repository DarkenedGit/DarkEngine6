#include <gtest/gtest.h>

#include "Terrain/GrassBend.h"
#include "Terrain/GrassMesh.h"

#include <algorithm>
#include <cmath>

using namespace Dark::Terrain;

namespace
{
    float tipLength(const GrassTip& tip)
    {
        return std::sqrt(tip.x * tip.x + tip.z * tip.z);
    }

    float yTipOf(const GrassTip& tip, float height)
    {
        const float yFloor = 0.20f * height;
        const float under  = height * height - tipLength(tip) * tipLength(tip);
        return std::sqrt(std::max(under, yFloor * yFloor));
    }
} // namespace

TEST(GrassBend, FlexZero_IsZeroTip)
{
    GrassTipIn in;
    in.height          = 0.55f;
    in.flex            = 0.0f;
    in.windX           = 4.0f;
    in.windZ           = -3.0f;
    in.shoveX          = 2.0f;
    in.shoveZ          = 8.0f;
    const GrassTip tip = grassTipOffset(in);
    EXPECT_FLOAT_EQ(tip.x, 0.0f);
    EXPECT_FLOAT_EQ(tip.z, 0.0f);
}

TEST(GrassBend, WindPlusShove_ClampsTo85PercentOfHeight)
{
    GrassTipIn under;
    under.height         = 1.0f;
    under.flex           = 0.5f;
    under.windX          = 0.20f;
    under.windZ          = 0.0f;
    under.shoveX         = 0.0f;
    under.shoveZ         = 0.10f;
    const GrassTip loose = grassTipOffset(under);
    EXPECT_NEAR(loose.x, 0.10f, 1e-6f);
    EXPECT_NEAR(loose.z, 0.05f, 1e-6f);
    EXPECT_LT(tipLength(loose), 0.85f * under.height);

    GrassTipIn over;
    over.height            = 1.0f;
    over.flex              = 1.0f;
    over.windX             = 3.0f;
    over.windZ             = 0.0f;
    over.shoveX            = 0.0f;
    over.shoveZ            = 4.0f;
    const GrassTip clamped = grassTipOffset(over);
    EXPECT_NEAR(tipLength(clamped), 0.85f * over.height, 1e-5f);
    EXPECT_NEAR(clamped.x / tipLength(clamped), 0.6f, 1e-5f);
    EXPECT_NEAR(clamped.z / tipLength(clamped), 0.8f, 1e-5f);
}

TEST(GrassBend, Lod0_OriginAtT0_NoRootLift)
{
    GrassTip tip;
    tip.x              = 0.40f;
    tip.z              = -0.20f;
    const float height = 0.80f;

    const GrassLocal origin = grassBladeLocal(0, 0.0f, tip, height);
    EXPECT_FLOAT_EQ(origin.x, 0.0f);
    EXPECT_FLOAT_EQ(origin.y, 0.0f);
    EXPECT_FLOAT_EQ(origin.z, 0.0f);

    const GrassLocal pivot = grassBladeLocal(2, 0.0f, tip, height);
    EXPECT_FLOAT_EQ(pivot.x, 0.0f);
    EXPECT_FLOAT_EQ(pivot.y, 0.0f);
    EXPECT_FLOAT_EQ(pivot.z, 0.0f);

    GrassTip         upright{};
    const GrassLocal top = grassBladeLocal(2, 1.0f, upright, height);
    EXPECT_NEAR(top.y, height, 1e-5f);
    EXPECT_GT(std::fabs(top.y - (height + 0.015f)), 0.01f);
    EXPECT_FLOAT_EQ(top.x, 0.0f);
    EXPECT_FLOAT_EQ(top.z, 0.0f);

    const float      t    = 0.5f;
    const float      u    = 1.0f - t;
    const float      b1   = 2.0f * u * t;
    const float      b2   = t * t;
    const float      yTip = yTipOf(tip, height);
    const GrassLocal mid  = grassBladeLocal(0, t, tip, height);
    EXPECT_NEAR(mid.x, b1 * (tip.x * 0.25f) + b2 * tip.x, 1e-5f);
    EXPECT_NEAR(mid.y, b1 * (height * 0.55f) + b2 * yTip, 1e-5f);
    EXPECT_NEAR(mid.z, b1 * (tip.z * 0.25f) + b2 * tip.z, 1e-5f);
    const GrassLocal linear = grassBladeLocal(2, t, tip, height);
    EXPECT_GT(std::fabs(mid.y - linear.y), 0.01f);
}

TEST(GrassBend, TipUsesHeight_NotMeshWidth)
{
    const float height = 1.0f;
    GrassTip    tip;
    tip.x                  = 0.30f;
    tip.z                  = 0.0f;
    const GrassLocal at1   = grassBladeLocal(0, 1.0f, tip, height);
    const float      reach = std::sqrt(at1.x * at1.x + at1.y * at1.y + at1.z * at1.z);
    EXPECT_NEAR(at1.x, tip.x, 1e-5f);
    EXPECT_NEAR(at1.z, tip.z, 1e-5f);
    EXPECT_NEAR(at1.y, yTipOf(tip, height), 1e-5f);
    EXPECT_NEAR(reach, height, 1e-5f);

    const GrassLocal tall = grassBladeLocal(0, 1.0f, tip, height * 2.0f);
    EXPECT_NEAR(tall.x, tip.x, 1e-5f);
    EXPECT_GT(tall.y, at1.y);

    Dark::MeshData mesh;
    ASSERT_TRUE(buildGrassBladeMesh(0, mesh));
    ASSERT_GE(mesh.positions.size(), 2u);
    const float half = std::fabs(mesh.positions[1].x);
    EXPECT_NEAR(half, 0.035f, 1e-6f);
    EXPECT_GT(std::fabs(half * (height * 2.0f) - half), 0.001f);
    EXPECT_GT(std::fabs(tall.x - half * (height * 2.0f)), 0.01f);
    EXPECT_GT(std::fabs(at1.x - half * height), 0.01f);
    EXPECT_GT(std::fabs(reach - std::sqrt(half * height * half * height + at1.y * at1.y)), 0.01f);
}

TEST(GrassBend, Lod2AndLod3_FollowZeroShove)
{
    const float height = 0.80f;
    GrassTipIn  shoved;
    shoved.height = height;
    shoved.flex   = 1.0f;
    shoved.windX  = 0.10f;
    shoved.windZ  = 0.20f;
    shoved.shoveX = 0.50f;
    shoved.shoveZ = -0.40f;

    GrassTipIn forced = shoved;
    forced.shoveX     = 0.0f;
    forced.shoveZ     = 0.0f;

    const GrassTip withShove = grassTipOffset(shoved);
    const GrassTip tip       = grassTipOffset(forced);
    EXPECT_GT(std::fabs(withShove.x - tip.x) + std::fabs(withShove.z - tip.z), 0.05f);
    EXPECT_LT(tipLength(tip), 0.85f * height + 1e-4f);

    const float      t    = 0.75f;
    const float      yTip = yTipOf(tip, height);
    const GrassLocal lod2 = grassBladeLocal(2, t, tip, height);
    const GrassLocal lod3 = grassBladeLocal(3, t, tip, height);
    EXPECT_NEAR(lod2.x, tip.x * t, 1e-5f);
    EXPECT_NEAR(lod2.y, yTip * t, 1e-5f);
    EXPECT_NEAR(lod2.z, tip.z * t, 1e-5f);
    EXPECT_NEAR(lod3.x, lod2.x, 1e-6f);
    EXPECT_NEAR(lod3.y, lod2.y, 1e-6f);
    EXPECT_NEAR(lod3.z, lod2.z, 1e-6f);
    EXPECT_GT(std::fabs(lod2.x - withShove.x * t), 0.01f);

    const GrassLocal end = grassBladeLocal(2, 1.0f, tip, height);
    EXPECT_NEAR(end.x, tip.x, 1e-5f);
    EXPECT_NEAR(end.y, yTip, 1e-5f);
    EXPECT_GT(std::fabs(end.y - (yTip + 0.015f)), 0.01f);
}
