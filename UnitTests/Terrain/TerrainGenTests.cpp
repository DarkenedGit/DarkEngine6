#include <gtest/gtest.h>

#include "Terrain/HeightMap.h"
#include "Terrain/SplatMap.h"
#include "Terrain/TerrainGen.h"

#include <cmath>
#include <cstdlib>

using namespace Dark::Terrain;

namespace
{
    WorldGenDesc MakeSmallDesc()
    {
        WorldGenDesc d;
        d.tilesX                      = 1;
        d.tilesZ                      = 1;
        d.tileCells                   = 64; // 65²
        d.cellSize                    = 1.0f;
        d.heightScale                 = 1.0f;
        d.origin                      = Dark::Math::Vector3f{ 0.0f, 0.0f, 0.0f };
        d.erosion.seed                = 1337u;
        d.erosion.thermalIterations   = 4;
        d.erosion.hydraulicMaxSteps   = 8;
        d.erosion.hydraulicIterations = 0;
        d.erosion.fbmOctaves          = 3;
        return d;
    }
} // namespace

TEST(TerrainGen, Hash21_UnitInterval)
{
    for (int z = -3; z < 8; ++z)
    {
        for (int x = -3; x < 8; ++x)
        {
            const float h = hash21(x, z, 1337u);
            EXPECT_GE(h, 0.0f);
            EXPECT_LT(h, 1.0f);
        }
    }
    EXPECT_FLOAT_EQ(hash21(0, 0, 42u), hash21(0, 0, 42u));
    EXPECT_NE(hash21(0, 0, 42u), hash21(1, 0, 42u));
}

TEST(TerrainGen, World_Deterministic)
{
    const WorldGenDesc desc = MakeSmallDesc();
    HeightMap          a;
    HeightMap          b;
    SplatMap           sa;
    SplatMap           sb;
    ASSERT_TRUE(generateWorld(desc, a, sa, nullptr, nullptr, nullptr));
    ASSERT_TRUE(generateWorld(desc, b, sb, nullptr, nullptr, nullptr));
    ASSERT_TRUE(a.valid());
    ASSERT_TRUE(b.valid());
    EXPECT_EQ(a.width(), 65u);
    EXPECT_EQ(a.height(), 65u);
    float mn = a.height(0, 0);
    float mx = mn;
    for (int z = 0; z < 65; ++z)
    {
        for (int x = 0; x < 65; ++x)
        {
            EXPECT_FLOAT_EQ(a.height(x, z), b.height(x, z)) << x << "," << z;
            const float h = a.height(x, z);
            if (h < mn)
                mn = h;
            if (h > mx)
                mx = h;
        }
    }
    EXPECT_GT(mx - mn, 0.05f);
}

TEST(TerrainGen, World_HeightScale_IsNotBakedIntoSamples)
{
    WorldGenDesc desc     = MakeSmallDesc();
    desc.heightScale      = 80.0f;
    desc.erosion.seaLevelRaw = 0.0f;
    HeightMap hm;
    SplatMap  sp;
    ASSERT_TRUE(generateWorld(desc, hm, sp, nullptr, nullptr, nullptr));
    float mx = hm.height(0, 0);
    for (int z = 0; z < static_cast<int>(hm.height()); ++z)
    {
        for (int x = 0; x < static_cast<int>(hm.width()); ++x)
        {
            const float h = hm.height(x, z);
            if (h > mx)
                mx = h;
        }
    }
    EXPECT_LT(mx, 4.0f);
    float mnRaw = mx;
    for (int z = 0; z < static_cast<int>(hm.height()); ++z)
    {
        for (int x = 0; x < static_cast<int>(hm.width()); ++x)
        {
            const float h = hm.height(x, z);
            if (h < mnRaw)
                mnRaw = h;
        }
    }
    EXPECT_LT(mnRaw, 0.2f);
    const float y = hm.heightAtWorld(hm.origin().x, hm.origin().z);
    EXPECT_LT(y, 200.0f);
    float yMin = 1.0e9f;
    float yMax = -1.0e9f;
    for (int z = 0; z < static_cast<int>(hm.height()); z += 4)
    {
        for (int x = 0; x < static_cast<int>(hm.width()); x += 4)
        {
            const float yw = hm.worldY(hm.height(x, z));
            if (yw < yMin)
                yMin = yw;
            if (yw > yMax)
                yMax = yw;
        }
    }
    EXPECT_GT(yMax - yMin, 20.0f);
}

TEST(TerrainGen, World_TileBoundaryNotRockRim)
{
    WorldGenDesc desc = MakeSmallDesc();
    desc.tilesX     = 2;
    desc.tilesZ     = 2;
    desc.tileCells  = 32;
    HeightMap hm;
    SplatMap  sp;
    ASSERT_TRUE(generateWorld(desc, hm, sp, nullptr, nullptr, nullptr));
    ASSERT_EQ(hm.width(), 65u);
    ASSERT_TRUE(sp.valid());

    auto slopeAt = [&](int x, int z) {
        const Dark::Math::Vector3f n = hm.normalAtWorld(hm.worldX(x), hm.worldZ(z));
        return 1.0f - n.y;
    };

    double border = 0.0;
    double inner  = 0.0;
    int    nb     = 0;
    int    ni     = 0;
    const int seam = 32;
    for (int z = 1; z < 64; ++z)
    {
        border += slopeAt(seam, z);
        ++nb;
        inner += slopeAt(seam - 3, z) + slopeAt(seam + 3, z);
        ni += 2;
    }
    for (int x = 1; x < 64; ++x)
    {
        border += slopeAt(x, seam);
        ++nb;
        inner += slopeAt(x, seam - 3) + slopeAt(x, seam + 3);
        ni += 2;
    }
    ASSERT_GT(nb, 0);
    ASSERT_GT(ni, 0);
    EXPECT_LT(border / static_cast<double>(nb), inner / static_cast<double>(ni) + 0.08);
}

TEST(TerrainGen, Thermal_PaddedTile_MatchesFull)
{
    WorldGenDesc desc = MakeSmallDesc();
    HeightMap    l0;
    SplatMap     s0;
    ASSERT_TRUE(generateWorld(desc, l0, s0, nullptr, nullptr, nullptr));
    ASSERT_EQ(l0.width(), 65u);

    HeightMap full;
    ASSERT_TRUE(full.createWorking(65, 65, 1.0f, 1.0f));
    for (int z = 0; z < 65; ++z)
    {
        for (int x = 0; x < 65; ++x)
            full.setHeight(x, z, l0.height(x, z));
    }
    ErosionParams tp     = desc.erosion;
    tp.thermalIterations = 4;
    tp.hydraulicMaxSteps = 0;
    ASSERT_TRUE(applyThermalJacobi(full, tp));

    const int pad   = 4;
    const int inner = 17;
    const int x0    = 16;
    const int z0    = 16;
    const int tw    = inner + 2 * pad;
    HeightMap tile;
    ASSERT_TRUE(tile.createWorking(static_cast<uint32_t>(tw), static_cast<uint32_t>(tw), 1.0f, 1.0f));
    for (int z = 0; z < tw; ++z)
    {
        for (int x = 0; x < tw; ++x)
            tile.setHeight(x, z, l0.height(x0 - pad + x, z0 - pad + z));
    }
    ASSERT_TRUE(applyThermalJacobi(tile, tp));

    for (int z = 0; z < inner; ++z)
    {
        for (int x = 0; x < inner; ++x)
        {
            EXPECT_FLOAT_EQ(tile.height(pad + x, pad + z), full.height(x0 + x, z0 + z)) << x << "," << z;
        }
    }
}

TEST(TerrainGen, Thermal_ReducesTalus)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(17, 17, 1.0f, 1.0f));
    hm.setHeight(8, 8, 10.0f);

    ErosionParams p;
    p.thermalIterations = 40;
    p.talusTan          = 0.7f;
    p.thermalRate       = 0.5f;
    ASSERT_TRUE(applyThermalJacobi(hm, p));

    const int ox[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    const int oz[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    for (int z = 1; z < 16; ++z)
    {
        for (int x = 1; x < 16; ++x)
        {
            const float h = hm.height(x, z);
            for (int n = 0; n < 8; ++n)
            {
                const float nh    = hm.height(x + ox[n], z + oz[n]);
                const float dist  = std::sqrt(static_cast<float>(ox[n] * ox[n] + oz[n] * oz[n]));
                const float dh    = h - nh;
                const float talus = p.talusTan * dist;
                EXPECT_LE(dh, talus + 0.05f) << x << "," << z;
            }
        }
    }
}

TEST(TerrainGen, Hydraulic_StopsAtMaxSteps)
{
    HeightMap base;
    ASSERT_TRUE(base.create(32, 32, 1.0f, 1.0f));
    for (int z = 0; z < 32; ++z)
    {
        for (int x = 0; x < 32; ++x)
            base.setHeight(x, z, static_cast<float>(31 - x) * 0.4f);
    }

    ErosionParams p;
    p.seed              = 7u;
    p.hydraulicDroplets = 1;
    p.hydraulicMaxSteps = 0;
    p.evaporate         = 0.0f;
    p.erode             = 0.8f;
    p.deposit           = 0.8f;
    p.capacity          = 1.0f;
    p.gravity           = 4.0f;

    HeightMap zeroSteps;
    ASSERT_TRUE(zeroSteps.createFrom(base.width(), base.height(), base.samples(), base.cellSize(), base.heightScale()));
    ASSERT_TRUE(applyHydraulicDroplets(zeroSteps, p));
    for (int z = 0; z < 32; ++z)
    {
        for (int x = 0; x < 32; ++x)
            EXPECT_FLOAT_EQ(zeroSteps.height(x, z), base.height(x, z));
    }

    p.hydraulicMaxSteps = 8;
    HeightMap stepped;
    ASSERT_TRUE(stepped.createFrom(base.width(), base.height(), base.samples(), base.cellSize(), base.heightScale()));
    ASSERT_TRUE(applyHydraulicDroplets(stepped, p));

    const float spawnX = hash21(0, 0, p.seed) * 31.0f;
    const float spawnZ = hash21(0, 1, p.seed) * 31.0f;
    bool        any    = false;
    for (int z = 0; z < 32; ++z)
    {
        for (int x = 0; x < 32; ++x)
        {
            const float dx   = std::fabs(static_cast<float>(x) - spawnX);
            const float dz   = std::fabs(static_cast<float>(z) - spawnZ);
            const float cheb = dx > dz ? dx : dz;
            if (cheb > static_cast<float>(p.hydraulicMaxSteps) + 1.5f)
                EXPECT_FLOAT_EQ(stepped.height(x, z), base.height(x, z)) << x << "," << z;
            if (stepped.height(x, z) != base.height(x, z))
                any = true;
        }
    }
    EXPECT_TRUE(any);
}

TEST(TerrainGen, Rejects_OversizeWorld)
{
    WorldGenDesc d = MakeSmallDesc();
    d.tilesX       = 9;
    d.tilesZ       = 1;
    d.tileCells    = kTileCells;
    HeightMap hm;
    SplatMap  splat;
    EXPECT_FALSE(generateWorld(d, hm, splat, nullptr, nullptr, nullptr));
    EXPECT_FALSE(hm.valid());

    d.tilesX    = 8;
    d.tilesZ    = 1;
    d.tileCells = 513; // 8*513+1 = 4105 > 4097
    EXPECT_FALSE(generateWorld(d, hm, splat, nullptr, nullptr, nullptr));
    EXPECT_FALSE(hm.valid());

    EXPECT_EQ(static_cast<uint32_t>(kMaxWorldTiles) * static_cast<uint32_t>(kTileCells) + 1u, static_cast<uint32_t>(kMaxWorkingSize));
}

TEST(TerrainGen, Progress_Cancel_NoThrow)
{
    const WorldGenDesc desc = MakeSmallDesc();
    HeightMap          hm;
    SplatMap           splat;
    auto               prog = [](float t, const char*, void*) -> bool { return t < 0.1f; };
    EXPECT_FALSE(generateWorld(desc, hm, splat, prog, nullptr, nullptr));
    EXPECT_FALSE(hm.valid());
}
