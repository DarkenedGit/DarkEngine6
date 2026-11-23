#include <gtest/gtest.h>

#include "Render/Fog.h"
#include "Render/HeightProvider.h"
#include "Terrain/HeightMap.h"
#include "Terrain/TerrainGrid.h"

using namespace Dark;
using namespace Dark::Math;
using namespace Dark::Terrain;

TEST(TerrainHeightProvider, AnInvalidHeightMapHasNoHeightGrid)
{
    HeightMap  empty;
    HeightGrid out;
    out.cellSize                   = 9.0f;
    out.width                      = 9;
    const IHeightProvider& heights = empty;
    EXPECT_FALSE(heights.heightGrid(out));
    EXPECT_FLOAT_EQ(out.cellSize, 9.0f);
    EXPECT_EQ(out.width, 9u);

    HeightMap thin; // one row: not a grid
    ASSERT_FALSE(thin.create(17, 1, 2.0f, 1.0f) && thin.valid());
    EXPECT_FALSE(static_cast<const IHeightProvider&>(thin).heightGrid(out));
}

TEST(TerrainHeightProvider, TheHeightGridIsTheHeightMapLayout)
{
    HeightMap map;
    ASSERT_TRUE(map.create(17, 9, 2.5f, 3.0f));
    map.setOrigin(Vector3f{ -40.0f, 5.0f, 12.0f });
    const IHeightProvider& heights = map;
    HeightGrid             out;
    ASSERT_TRUE(heights.heightGrid(out));
    EXPECT_EQ(out.width, 17u);
    EXPECT_EQ(out.height, 9u);
    EXPECT_FLOAT_EQ(out.cellSize, 2.5f);
    EXPECT_FLOAT_EQ(out.origin.x, -40.0f);
    EXPECT_FLOAT_EQ(out.origin.y, 5.0f);
    EXPECT_FLOAT_EQ(out.origin.z, 12.0f);

    map.setCellSize(4.0f);
    ASSERT_TRUE(heights.heightGrid(out));
    EXPECT_FLOAT_EQ(out.cellSize, 4.0f);
}

TEST(TerrainHeightProvider, FogPacksTheTerrainCoarseHeightField)
{
    HeightMap coarse;
    ASSERT_TRUE(coarse.create(17, 9, 2.5f, 1.0f));
    coarse.setOrigin(Vector3f{ -40.0f, 5.0f, 12.0f });
    TerrainGridDesc desc{};
    desc.tilesX       = 2;
    desc.tilesZ       = 1;
    desc.tileCells    = 8;
    desc.cellSize     = 2.5f;
    desc.origin       = coarse.origin();
    desc.residentRing = 1;
    TerrainGrid grid;
    ASSERT_TRUE(grid.createFromCoarse(desc, std::move(coarse)));

    // What Sandbox passes: the grid's coarse HF.
    FogGpu fog{};
    fillFogHeightMap(fog, &grid.coarse());
    EXPECT_FLOAT_EQ(fog.heightOriginX, -40.0f);
    EXPECT_FLOAT_EQ(fog.heightOriginZ, 12.0f);
    EXPECT_FLOAT_EQ(fog.heightCellSize, 2.5f);
    EXPECT_FLOAT_EQ(fog.heightWorldSizeX, 40.0f);
    EXPECT_FLOAT_EQ(fog.heightWorldSizeZ, 20.0f);

    grid.clear();
    fillFogHeightMap(fog, &grid.coarse());
    EXPECT_FLOAT_EQ(fog.heightCellSize, 0.0f);
}
