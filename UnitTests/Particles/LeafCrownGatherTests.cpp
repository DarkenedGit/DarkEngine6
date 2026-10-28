#include <gtest/gtest.h>

#include "Particles/LeafRenderer.h"
#include "Terrain/TerrainGrid.h"

#include <vector>

using namespace Dark;
using namespace Dark::Terrain;
using Dark::Math::Vector3f;

namespace
{

    constexpr int   kTileCells = 8;
    constexpr float kCell      = 16.0f;
    constexpr float kTreeTop   = 10.0f;

    bool makeGrid(TerrainGrid& grid)
    {
        constexpr uint32_t kSamples = static_cast<uint32_t>(kTileCells * 2 + 1);
        HeightMap          working;
        if (!working.create(kSamples, kSamples, kCell, 1.0f))
            return false;
        working.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
        HeightMap       coarse = working;
        TerrainGridDesc desc{};
        desc.tilesX       = 2;
        desc.tilesZ       = 2;
        desc.tileCells    = static_cast<uint32_t>(kTileCells);
        desc.cellSize     = kCell;
        desc.origin       = Vector3f{ 0.0f, 0.0f, 0.0f };
        desc.residentRing = 1;
        if (!grid.createFromCoarse(desc, std::move(coarse)))
            return false;
        return grid.setWorking(std::move(working), SplatMap{});
    }

    FoliageRecord makeRec(FoliageKind kind, float x, float y, float z, float scale)
    {
        FoliageRecord rec{};
        rec.x     = x;
        rec.y     = y;
        rec.z     = z;
        rec.scale = scale;
        rec.kind  = static_cast<uint8_t>(kind);
        return rec;
    }

    bool hasX(const LeafField::Crown* crowns, int count, float x)
    {
        for (int i = 0; i < count; ++i)
        {
            if (crowns[i].x == x)
                return true;
        }
        return false;
    }

} // namespace

TEST(LeafCrownGather, HostListKeepsResidentTreesInVectorOrder)
{
    TerrainGrid grid;
    ASSERT_TRUE(makeGrid(grid));

    // 128 m tiles. Camera on tile 0, near the edge, so tile 1 is inside 96 m and a far tree can stay on tile 0.
    const float    tileWorld = static_cast<float>(grid.tileCells()) * grid.cellSize();
    const Vector3f camera(120.0f, 20.0f, 8.0f);
    ASSERT_GT(tileWorld, LeafField::kGatherRadiusM);
    ASSERT_LT(camera.x, tileWorld);
    ASSERT_GT(camera.x - 10.0f, LeafField::kGatherRadiusM);
    ASSERT_GT(130.0f, tileWorld);
    ASSERT_LT(130.0f - camera.x, LeafField::kGatherRadiusM);

    grid.updateStreaming(camera, nullptr);
    ASSERT_TRUE(grid.isResident(0, 0));
    ASSERT_FALSE(grid.isResident(1, 0));

    std::vector<FoliageRecord> host;
    host.push_back(makeRec(FoliageKind::Flower, 100.0f, 1.0f, 8.0f, 1.0f));
    host.push_back(makeRec(FoliageKind::Tree, 110.0f, 4.0f, 8.0f, 2.0f));
    host.push_back(makeRec(FoliageKind::Rock, 115.0f, 2.0f, 8.0f, 1.0f));
    host.push_back(makeRec(FoliageKind::Tree, 118.0f, 7.0f, 8.0f, 0.05f));
    host.push_back(makeRec(FoliageKind::Tree, 10.0f, 9.0f, 8.0f, 1.0f));
    host.push_back(makeRec(FoliageKind::Tree, 130.0f, 6.0f, 8.0f, 1.0f));

    LeafField::Crown crowns[8]{};
    const int        count = gatherLeafCrowns(grid, &host, camera, nullptr, kTreeTop, crowns, 8);
    ASSERT_EQ(count, 2);
    EXPECT_FLOAT_EQ(crowns[0].x, 110.0f);
    EXPECT_FLOAT_EQ(crowns[0].z, 8.0f);
    EXPECT_FLOAT_EQ(crowns[0].seatY, 4.0f);
    EXPECT_FLOAT_EQ(crowns[0].crownY, 4.0f + kTreeTop * 2.0f);
    EXPECT_FLOAT_EQ(crowns[1].x, 118.0f);
    EXPECT_FLOAT_EQ(crowns[1].z, 8.0f);
    EXPECT_FLOAT_EQ(crowns[1].seatY, 7.0f);
    EXPECT_FLOAT_EQ(crowns[1].crownY, 7.0f + kTreeTop);
    EXPECT_FALSE(hasX(crowns, count, 100.0f));
    EXPECT_FALSE(hasX(crowns, count, 115.0f));
    EXPECT_FALSE(hasX(crowns, count, 10.0f));
    EXPECT_FALSE(hasX(crowns, count, 130.0f));

    host[1].x       = 112.0f;
    host[1].scale   = 3.0f;
    const int moved = gatherLeafCrowns(grid, &host, camera, nullptr, kTreeTop, crowns, 8);
    ASSERT_EQ(moved, 2);
    EXPECT_FLOAT_EQ(crowns[0].x, 112.0f);
    EXPECT_FLOAT_EQ(crowns[0].crownY, 4.0f + kTreeTop * 3.0f);
    EXPECT_FLOAT_EQ(crowns[1].x, 118.0f);
    EXPECT_FLOAT_EQ(crowns[1].crownY, 7.0f + kTreeTop);

    host[0]           = makeRec(FoliageKind::Tree, 100.0f, 3.0f, 8.0f, 1.0f);
    host[3].kind      = static_cast<uint8_t>(FoliageKind::Flower);
    const int rebuilt = gatherLeafCrowns(grid, &host, camera, nullptr, kTreeTop, crowns, 8);
    ASSERT_EQ(rebuilt, 2);
    EXPECT_FLOAT_EQ(crowns[0].x, 100.0f);
    EXPECT_FLOAT_EQ(crowns[0].crownY, 3.0f + kTreeTop);
    EXPECT_FLOAT_EQ(crowns[1].x, 112.0f);
    EXPECT_FALSE(hasX(crowns, rebuilt, 118.0f));

    std::vector<FoliageRecord> other;
    other.push_back(makeRec(FoliageKind::Tree, 10.0f, 1.0f, 8.0f, 1.0f));
    other.push_back(makeRec(FoliageKind::Tree, 12.0f, 1.0f, 8.0f, 1.0f));
    other.push_back(makeRec(FoliageKind::Tree, 110.0f, 5.0f, 8.0f, 1.0f));
    other.push_back(makeRec(FoliageKind::Flower, 115.0f, 2.0f, 8.0f, 1.0f));
    other.push_back(makeRec(FoliageKind::Tree, 130.0f, 1.0f, 8.0f, 1.0f));
    other.push_back(makeRec(FoliageKind::Tree, 140.0f, 1.0f, 8.0f, 1.0f));
    const int otherCount = gatherLeafCrowns(grid, &other, camera, nullptr, kTreeTop, crowns, 8);
    ASSERT_EQ(otherCount, 1);
    EXPECT_FLOAT_EQ(crowns[0].x, 110.0f);
    EXPECT_FLOAT_EQ(crowns[0].seatY, 5.0f);
    EXPECT_FLOAT_EQ(crowns[0].crownY, 5.0f + kTreeTop);
}
