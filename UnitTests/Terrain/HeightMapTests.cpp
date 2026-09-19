#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Terrain/HeightMap.h"
#include "Terrain/Terrain.h"

using namespace Dark::Math;
using namespace Dark::Terrain;

TEST(HeightMap, CreateAndSample)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(5, 5, 2.0f, 1.0f));
    EXPECT_TRUE(hm.valid());
    EXPECT_EQ(hm.width(), 5u);
    EXPECT_EQ(hm.height(), 5u);

    hm.setHeight(0, 0, 1.0f);
    hm.setHeight(1, 0, 3.0f);
    hm.setHeight(0, 1, 5.0f);
    hm.setHeight(1, 1, 7.0f);

    EXPECT_FLOAT_EQ(hm.height(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(hm.height(-4, -4), 1.0f); // clamp
    EXPECT_NEAR(hm.sampleBilinear(0.5f, 0.0f), 2.0f, 1.0e-5f);
    EXPECT_NEAR(hm.sampleBilinear(0.5f, 0.5f), 4.0f, 1.0e-5f);
}

TEST(HeightMap, WorldMapping)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(3, 3, 10.0f, 2.0f));
    hm.setOrigin(Vector3f{ -10.0f, 5.0f, -10.0f });
    hm.setHeight(1, 1, 4.0f);

    EXPECT_NEAR(hm.worldX(1), 0.0f, 1.0e-5f);
    EXPECT_NEAR(hm.worldZ(1), 0.0f, 1.0e-5f);
    EXPECT_NEAR(hm.heightAtWorld(0.0f, 0.0f), 5.0f + 8.0f, 1.0e-4f);

    const Vector3f p = hm.positionAtSample(1, 1);
    EXPECT_NEAR(p.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(p.y, 13.0f, 1.0e-5f);
    EXPECT_NEAR(p.z, 0.0f, 1.0e-5f);
}

TEST(HeightMap, FromU16AndAddLayer)
{
    const uint16_t raw[4] = { 0, 65535, 0, 32768 };
    HeightMap base;
    ASSERT_TRUE(base.createFromU16(2, 2, raw, 0.0f, 10.0f, 1.0f, 1.0f));
    EXPECT_NEAR(base.height(0, 0), 0.0f, 1.0e-4f);
    EXPECT_NEAR(base.height(1, 0), 10.0f, 1.0e-3f);

    HeightMap detail;
    ASSERT_TRUE(detail.create(2, 2, 1.0f, 1.0f));
    detail.setHeight(0, 0, 1.0f);
    ASSERT_TRUE(base.addLayer(detail, 2.0f));
    EXPECT_NEAR(base.height(0, 0), 2.0f, 1.0e-4f);
}

TEST(HeightMap, FbmDeterministic)
{
    HeightMap a;
    HeightMap b;
    ASSERT_TRUE(a.createFbm(17, 17, 42u, 4, 3.0f, 1.0f, 2.0f, 0.5f, 1.0f, 1.0f));
    ASSERT_TRUE(b.createFbm(17, 17, 42u, 4, 3.0f, 1.0f, 2.0f, 0.5f, 1.0f, 1.0f));
    for (int z = 0; z < 17; ++z)
    {
        for (int x = 0; x < 17; ++x)
            EXPECT_FLOAT_EQ(a.height(x, z), b.height(x, z));
    }

    const AABox3f box = a.bounds();
    EXPECT_TRUE(box.IsValid());
    EXPECT_GT(box.Size().x, 0.0f);
}

TEST(HeightMap, SlopeNormal)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(5, 5, 1.0f, 1.0f));
    for (int z = 0; z < 5; ++z)
    {
        for (int x = 0; x < 5; ++x)
            hm.setHeight(x, z, static_cast<float>(x));
    }

    const Vector3f n = hm.normalAtWorld(2.0f, 2.0f);
    EXPECT_LT(n.x, 0.0f); // slope rises in +X, normal tilts -X
    EXPECT_GT(n.y, 0.0f);
    EXPECT_NEAR(n.Magnitude(), 1.0f, 1.0e-4f);
}

TEST(HeightMap, Create_RejectsOversize)
{
    HeightMap hm;
    EXPECT_FALSE(hm.create(2048, 2048, 1.0f, 1.0f));
    EXPECT_FALSE(hm.valid());
    EXPECT_EQ(hm.width(), 0u);
    EXPECT_FALSE(hm.create(1026, 2, 1.0f, 1.0f));
    EXPECT_FALSE(hm.create(2, 2048, 1.0f, 1.0f));
}

TEST(HeightMap, Create_StillAccepts129)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(129, 129, 2.0f, 1.0f));
    EXPECT_TRUE(hm.valid());
    EXPECT_EQ(hm.width(), 129u);
    EXPECT_EQ(hm.height(), 129u);
}

TEST(HeightMap, AddDisk_ZeroRadiusNoOp)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(5, 5, 1.0f, 1.0f));
    hm.addDisk(2.0f, 2.0f, 0.0f, 4.0f);
    hm.addDisk(2.0f, 2.0f, -1.0f, 4.0f);
    EXPECT_NEAR(hm.height(2, 2), 0.0f, 1.0e-5f);
    hm.smoothDisk(2.0f, 2.0f, 2.0f, 0.0f);
    EXPECT_NEAR(hm.height(2, 2), 0.0f, 1.0e-5f);
}

TEST(HeightMap, MarkHeightDirty_NeedsRebuild)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(17, 17, 1.0f, 1.0f));
    TerrainDesc desc;
    desc.heightMap  = std::move(hm);
    desc.chunkCells = 16;
    TerrainWorld world;
    ASSERT_TRUE(world.create(std::move(desc)));
    world.rebuildDirtyCpuMeshes();
    EXPECT_FALSE(world.needsRebuild());

    world.markHeightDirty();
    EXPECT_TRUE(world.needsRebuild());
    world.rebuildDirtyCpuMeshes();
    EXPECT_FALSE(world.needsRebuild());

    world.markHeightDirtyRect(0, 0, 1, 1);
    EXPECT_TRUE(world.needsRebuild());
    world.rebuildDirtyCpuMeshes();
    EXPECT_FALSE(world.needsRebuild());

    world.markHeightDirtyRect(16, 16, 16, 16);
    EXPECT_TRUE(world.needsRebuild());
    world.rebuildDirtyCpuMeshes();
    world.markHeightDirtyRect(40, 40, 50, 50);
    EXPECT_FALSE(world.needsRebuild());
    world.markHeightDirtyRect(4, 4, -1, -2);
    EXPECT_TRUE(world.needsRebuild());
}
