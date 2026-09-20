#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Terrain/HeightMap.h"
#include "Terrain/Terrain.h"
#include "Terrain/TerrainTileFile.h"

#include <filesystem>

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

TEST(HeightMap, CreateWorking_Accepts4097)
{
    HeightMap hm;
    ASSERT_TRUE(hm.createWorking(kMaxWorkingHeightMapSize, kMaxWorkingHeightMapSize, 1.0f, 1.0f));
    EXPECT_TRUE(hm.valid());
    EXPECT_EQ(hm.width(), 4097u);
    EXPECT_EQ(hm.height(), 4097u);
    hm.setHeight(4096, 0, 9.0f);
    EXPECT_FLOAT_EQ(hm.height(4096, 0), 9.0f);

    HeightMap over;
    EXPECT_FALSE(over.createWorking(4098, 4098, 1.0f, 1.0f));
    EXPECT_FALSE(over.valid());
    EXPECT_EQ(over.width(), 0u);
    EXPECT_FALSE(over.createWorking(4098, 2, 1.0f, 1.0f));
    EXPECT_FALSE(over.valid());

    HeightMap runtime;
    EXPECT_FALSE(runtime.create(2048, 2048, 1.0f, 1.0f));
    EXPECT_FALSE(runtime.valid());
    EXPECT_FALSE(runtime.create(kMaxWorkingHeightMapSize, kMaxWorkingHeightMapSize, 1.0f, 1.0f));
}

TEST(HeightMap, Create_StillAccepts129And1025)
{
    HeightMap boot;
    ASSERT_TRUE(boot.create(129, 129, 2.0f, 1.0f));
    EXPECT_TRUE(boot.valid());
    EXPECT_EQ(boot.width(), 129u);
    EXPECT_EQ(boot.height(), 129u);

    HeightMap maxRuntime;
    ASSERT_TRUE(maxRuntime.create(1025, 1025, 1.0f, 1.0f));
    EXPECT_TRUE(maxRuntime.valid());
    EXPECT_EQ(maxRuntime.width(), 1025u);
    EXPECT_EQ(maxRuntime.height(), 1025u);
    maxRuntime.setHeight(1024, 1024, 4.0f);
    EXPECT_FLOAT_EQ(maxRuntime.height(1024, 1024), 4.0f);
}

TEST(HeightMap, Working_SaveBinary_Rejected)
{
    HeightMap working;
    ASSERT_TRUE(working.createWorking(4097, 4097, 1.0f, 1.0f));

    const auto bigPath = std::filesystem::temp_directory_path() / "darkengine6_working_4097.height.bin";
    std::error_code ec;
    std::filesystem::remove(bigPath, ec);
    EXPECT_FALSE(working.saveBinary(bigPath));
    EXPECT_FALSE(std::filesystem::exists(bigPath, ec));

    HeightMap tile;
    ASSERT_TRUE(tile.create(513, 513, 1.0f, 1.0f));
    tile.setHeight(1, 1, 0.5f);
    const auto tilePath = std::filesystem::temp_directory_path() / "darkengine6_tile_513.height.bin";
    ASSERT_TRUE(tile.saveBinary(tilePath));
    HeightMap loaded;
    ASSERT_TRUE(loaded.loadBinary(tilePath));
    EXPECT_EQ(loaded.width(), 513u);
    EXPECT_EQ(loaded.height(), 513u);
    EXPECT_NEAR(loaded.height(1, 1), 0.5f, 1.0e-5f);
    std::filesystem::remove(tilePath, ec);
}

TEST(HeightMap, Tile_Sidecar_Under32MB)
{
    EXPECT_EQ(tileHeightFileName(0, 0), "t_00_00.height.bin");
    EXPECT_EQ(tileHeightFileName(3, 7), "t_03_07.height.bin");
    EXPECT_EQ(tileSplatFileName(0, 1), "t_00_01.splat.png");
    EXPECT_EQ(kMaxWorldTiles, 8u);
    EXPECT_EQ(kTileSamples, 513);

    HeightMap hm;
    ASSERT_TRUE(hm.create(static_cast<uint32_t>(kTileSamples), static_cast<uint32_t>(kTileSamples), 1.0f, 1.0f));
    hm.setHeight(0, 0, 1.5f);
    hm.setHeight(512, 512, 2.25f);

    const auto dir  = std::filesystem::temp_directory_path() / "darkengine6_tile_sidecar_ut";
    const auto path = tileHeightPath(dir, 0, 0);
    ASSERT_TRUE(hm.saveBinary(path));

    std::error_code ec;
    const uintmax_t bytes = std::filesystem::file_size(path, ec);
    EXPECT_FALSE(ec);
    EXPECT_LT(bytes, 32ull * 1024ull * 1024ull);
    EXPECT_GT(bytes, 0u);

    HeightMap loaded;
    ASSERT_TRUE(loaded.loadBinary(path));
    EXPECT_EQ(loaded.width(), static_cast<uint32_t>(kTileSamples));
    EXPECT_EQ(loaded.height(), static_cast<uint32_t>(kTileSamples));
    EXPECT_NEAR(loaded.height(0, 0), 1.5f, 1.0e-5f);
    EXPECT_NEAR(loaded.height(512, 512), 2.25f, 1.0e-5f);

    std::filesystem::remove_all(dir, ec);
}
