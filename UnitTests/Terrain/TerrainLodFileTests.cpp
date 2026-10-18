#include <gtest/gtest.h>

#include "Core/ContentRoots.h"
#include "Terrain/TerrainLodFile.h"

#include <fstream>
#include <sstream>

using namespace Dark;
using namespace Dark::Terrain;

TEST(TerrainLodFile, BadJsonLeavesSettings)
{
    TerrainLodSettings rows = builtinTerrainLodSettings();
    rows.terrainCount = 5;
    rows.grass.promote0 = 13.6f;
    EXPECT_FALSE(parseTerrainLodSettings("{", rows));
    EXPECT_FALSE(parseTerrainLodSettings("{\"version\":2,\"terrain\":{\"chunkMultiples\":[2]},\"grass\":{}}", rows));
    EXPECT_FALSE(parseTerrainLodSettings("{\"version\":1,\"terrain\":{\"chunkMultiples\":[4,2]},\"grass\":{}}", rows));
    EXPECT_EQ(rows.terrainCount, 5);
    EXPECT_FLOAT_EQ(rows.grass.promote0, 13.6f);
}

TEST(TerrainLodFile, ChunkMultiplesScaleTheChunkWidth)
{
    const TerrainLodSettings& lod = terrainLodSettings();
    ASSERT_TRUE(terrainLodLoadedFromFile());
    float dist[kMaxLodLevels]{};
    int   count = 0;
    fillChunkLodDistances(16.0f, lod, dist, count);
    ASSERT_EQ(count, 5);
    EXPECT_FLOAT_EQ(dist[0], 32.0f);
    EXPECT_FLOAT_EQ(dist[1], 64.0f);
    EXPECT_FLOAT_EQ(dist[2], 128.0f);
    EXPECT_FLOAT_EQ(dist[3], 256.0f);
    EXPECT_FLOAT_EQ(dist[4], 512.0f);
}

TEST(TerrainLodFile, ContentFileIsTheLiveGrassAndTerrain)
{
    std::filesystem::path path;
    for (const std::filesystem::path& root : contentRootCandidates())
    {
        const std::filesystem::path candidate = root / "terrain" / "lod.json";
        std::error_code             ec;
        if (std::filesystem::is_regular_file(candidate, ec) && !ec)
        {
            path = candidate;
            break;
        }
    }
    ASSERT_FALSE(path.empty());
    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    TerrainLodSettings parsed = builtinTerrainLodSettings();
    parsed.terrainCount = 1;
    ASSERT_TRUE(parseTerrainLodSettings(ss.str(), parsed));
    EXPECT_EQ(parsed.terrainCount, 5);
    EXPECT_FLOAT_EQ(parsed.terrainMultiples[0], 2.0f);
    EXPECT_FLOAT_EQ(parsed.terrainMultiples[4], 32.0f);
    EXPECT_FLOAT_EQ(parsed.grass.nominal0, 16.0f);
    EXPECT_FLOAT_EQ(parsed.grass.nominal1, 40.0f);
    EXPECT_FLOAT_EQ(parsed.grass.nominal2, 80.0f);
    EXPECT_FLOAT_EQ(parsed.grass.nominal3, 128.0f);
    EXPECT_FLOAT_EQ(parsed.grass.promote0, 13.6f);
    EXPECT_FLOAT_EQ(parsed.grass.promote1, 34.0f);
    EXPECT_FLOAT_EQ(parsed.grass.promote2, 68.0f);
    EXPECT_FLOAT_EQ(parsed.grass.demote0, 18.4f);
    EXPECT_FLOAT_EQ(parsed.grass.demote1, 46.0f);
    EXPECT_FLOAT_EQ(parsed.grass.demote2, 92.0f);
    EXPECT_FLOAT_EQ(parsed.grass.createRadius, 128.0f);
    EXPECT_FLOAT_EQ(parsed.grass.evictRadius, 147.2f);
    EXPECT_FLOAT_EQ(parsed.grass.fadeStart, 112.0f);
    EXPECT_FLOAT_EQ(parsed.grass.fadeEnd, 128.0f);

    const TerrainLodSettings& live = terrainLodSettings();
    EXPECT_EQ(live.terrainCount, parsed.terrainCount);
    EXPECT_FLOAT_EQ(live.terrainMultiples[0], parsed.terrainMultiples[0]);
    EXPECT_FLOAT_EQ(live.grass.promote0, parsed.grass.promote0);
    EXPECT_FLOAT_EQ(live.grass.evictRadius, parsed.grass.evictRadius);
}
