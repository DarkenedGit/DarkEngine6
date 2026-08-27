#include <gtest/gtest.h>

#include "AI/Walkability.h"
#include "Math/AABox3f.h"
#include "Terrain/HeightMap.h"

using namespace Dark::Math;
using namespace Dark::Terrain;
using namespace Dark::AI;

namespace
{
    HeightMap MakeFlat(int samples, float cell, float y)
    {
        HeightMap hm;
        EXPECT_TRUE(hm.create(static_cast<uint32_t>(samples), static_cast<uint32_t>(samples), cell, 1.0f));
        hm.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
        for (int z = 0; z < samples; ++z)
        {
            for (int x = 0; x < samples; ++x)
                hm.setHeight(x, z, y);
        }
        return hm;
    }
} // namespace

TEST(Walkability, FlatDryIsOneIsland)
{
    HeightMap hm = MakeFlat(5, 1.0f, 10.0f);
    Walkability w;
    WalkabilityDesc d;
    d.heightMap  = &hm;
    d.waterLevel = -10.0f;
    ASSERT_TRUE(w.bake(d));
    EXPECT_EQ(w.cellsX(), 4);
    EXPECT_EQ(w.cellsZ(), 4);
    EXPECT_TRUE(w.walkable(1, 1));
    EXPECT_EQ(w.island(0, 0), w.island(3, 3));
    EXPECT_GT(w.island(1, 1), 0);
}

TEST(Walkability, WetValleyUnwalkable)
{
    HeightMap hm = MakeFlat(9, 1.0f, 10.0f);
    for (int z = 0; z < 9; ++z)
        hm.setHeight(4, z, 0.0f);
    Walkability w;
    WalkabilityDesc d;
    d.heightMap  = &hm;
    d.waterLevel = 5.0f;
    ASSERT_TRUE(w.bake(d));
    EXPECT_FALSE(w.walkable(4, 2));
    EXPECT_TRUE(w.walkable(0, 2));
    EXPECT_TRUE(w.walkable(7, 2));
    EXPECT_NE(w.island(0, 2), w.island(7, 2));
}

TEST(Walkability, OffMapWorldToCell)
{
    HeightMap hm = MakeFlat(5, 1.0f, 10.0f);
    Walkability w;
    WalkabilityDesc d;
    d.heightMap  = &hm;
    d.waterLevel = -10.0f;
    ASSERT_TRUE(w.bake(d));
    int cx = 0;
    int cz = 0;
    EXPECT_FALSE(w.worldToCell(-1.0f, 1.0f, cx, cz));
}

TEST(Walkability, CubeInflateBlocksCells)
{
    HeightMap hm = MakeFlat(9, 1.0f, 10.0f);
    const Aabb3f cube = Aabb3f::FromCenterExtents(Vector3f{ 4.0f, 10.5f, 4.0f }, Vector3f{ 0.5f, 0.5f, 0.5f });
    Walkability w;
    WalkabilityDesc d;
    d.heightMap   = &hm;
    d.waterLevel  = -10.0f;
    d.agentRadius = 0.8f;
    d.cubes       = &cube;
    d.cubeCount   = 1;
    ASSERT_TRUE(w.bake(d));
    int cx = 0;
    int cz = 0;
    ASSERT_TRUE(w.worldToCell(4.0f, 4.0f, cx, cz));
    EXPECT_FALSE(w.walkable(cx, cz));
}

TEST(Walkability, SteepRampUnwalkable)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(5, 5, 1.0f, 1.0f));
    hm.setOrigin(Vector3f{ 0, 0, 0 });
    for (int z = 0; z < 5; ++z)
    {
        for (int x = 0; x < 5; ++x)
            hm.setHeight(x, z, static_cast<float>(x) * 3.0f);
    }
    Walkability w;
    WalkabilityDesc d;
    d.heightMap  = &hm;
    d.waterLevel = -10.0f;
    ASSERT_TRUE(w.bake(d));
    EXPECT_FALSE(w.walkable(2, 2));
}
