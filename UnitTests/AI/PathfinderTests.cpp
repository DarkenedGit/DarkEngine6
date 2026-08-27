#include <gtest/gtest.h>

#include "AI/Pathfinder.h"
#include "AI/Walkability.h"
#include "Math/AABox3f.h"
#include "Terrain/HeightMap.h"

using namespace Dark::Math;
using namespace Dark::Terrain;
using namespace Dark::AI;

namespace
{
    HeightMap MakeFlat(int samples, float y)
    {
        HeightMap hm;
        EXPECT_TRUE(hm.create(static_cast<uint32_t>(samples), static_cast<uint32_t>(samples), 1.0f, 1.0f));
        hm.setOrigin(Vector3f{ 0, 0, 0 });
        for (int z = 0; z < samples; ++z)
        {
            for (int x = 0; x < samples; ++x)
                hm.setHeight(x, z, y);
        }
        return hm;
    }

    bool PathDry(const Walkability& w, const PathResult& p, float water)
    {
        for (const Vector3f& v : p.points)
        {
            if (w.destWet(v.x, v.z))
                return false;
            (void)water;
        }
        return true;
    }
} // namespace

TEST(Pathfinder, AroundWetValley)
{
    HeightMap hm = MakeFlat(11, 10.0f);
    for (int z = 2; z < 11; ++z)
        hm.setHeight(5, z, 0.0f);
    Walkability w;
    WalkabilityDesc d;
    d.heightMap  = &hm;
    d.waterLevel = 5.0f;
    ASSERT_TRUE(w.bake(d));
    ASSERT_TRUE(w.walkableWorld(1.5f, 6.5f));
    ASSERT_TRUE(w.walkableWorld(8.5f, 6.5f));
    ASSERT_EQ(w.islandWorld(1.5f, 6.5f), w.islandWorld(8.5f, 6.5f));
    Pathfinder pf;
    ASSERT_TRUE(pf.bind(&w));
    PathRequest req;
    req.startX = 1.5f;
    req.startZ = 6.5f;
    req.destX  = 8.5f;
    req.destZ  = 6.5f;
    PathResult out;
    ASSERT_TRUE(pf.find(req, out));
    EXPECT_FALSE(out.points.empty());
    EXPECT_TRUE(PathDry(w, out, 5.0f));
}

TEST(Pathfinder, DestWetFails)
{
    HeightMap hm = MakeFlat(7, 10.0f);
    hm.setHeight(3, 3, 0.0f);
    Walkability w;
    WalkabilityDesc d;
    d.heightMap  = &hm;
    d.waterLevel = 5.0f;
    ASSERT_TRUE(w.bake(d));
    Pathfinder pf;
    ASSERT_TRUE(pf.bind(&w));
    PathRequest req;
    req.startX = 1.5f;
    req.startZ = 1.5f;
    req.destX  = 3.5f;
    req.destZ  = 3.5f;
    PathResult out;
    EXPECT_FALSE(pf.find(req, out));
}

TEST(Pathfinder, StartWetFails)
{
    HeightMap hm = MakeFlat(7, 10.0f);
    hm.setHeight(1, 1, 0.0f);
    Walkability w;
    WalkabilityDesc d;
    d.heightMap  = &hm;
    d.waterLevel = 5.0f;
    ASSERT_TRUE(w.bake(d));
    Pathfinder pf;
    ASSERT_TRUE(pf.bind(&w));
    PathRequest req;
    req.startX = 1.5f;
    req.startZ = 1.5f;
    req.destX  = 4.5f;
    req.destZ  = 4.5f;
    PathResult out;
    EXPECT_FALSE(pf.find(req, out));
}

TEST(Pathfinder, OffMapDestFails)
{
    HeightMap hm = MakeFlat(5, 10.0f);
    Walkability w;
    WalkabilityDesc d;
    d.heightMap  = &hm;
    d.waterLevel = -10.0f;
    ASSERT_TRUE(w.bake(d));
    Pathfinder pf;
    ASSERT_TRUE(pf.bind(&w));
    PathRequest req;
    req.startX = 1.5f;
    req.startZ = 1.5f;
    req.destX  = 50.0f;
    req.destZ  = 50.0f;
    PathResult out;
    EXPECT_FALSE(pf.find(req, out));
}

TEST(Pathfinder, CubeDestSnapsBeside)
{
    HeightMap hm = MakeFlat(9, 10.0f);
    const Aabb3f cube = Aabb3f::FromCenterExtents(Vector3f{ 4.5f, 10.5f, 4.5f }, Vector3f{ 0.5f, 0.5f, 0.5f });
    Walkability w;
    WalkabilityDesc d;
    d.heightMap   = &hm;
    d.waterLevel  = -10.0f;
    d.agentRadius = 0.8f;
    d.cubes       = &cube;
    d.cubeCount   = 1;
    ASSERT_TRUE(w.bake(d));
    Pathfinder pf;
    ASSERT_TRUE(pf.bind(&w));
    PathRequest req;
    req.startX = 1.5f;
    req.startZ = 1.5f;
    req.destX  = 4.5f;
    req.destZ  = 4.5f;
    PathResult out;
    ASSERT_TRUE(pf.find(req, out));
    const Vector3f& end = out.points.back();
    EXPECT_GT((end.x - 4.5f) * (end.x - 4.5f) + (end.z - 4.5f) * (end.z - 4.5f), 0.2f);
}

TEST(Pathfinder, CubeSurroundedByWaterFails)
{
    HeightMap hm = MakeFlat(9, 10.0f);
    for (int z = 2; z <= 6; ++z)
    {
        for (int x = 2; x <= 6; ++x)
        {
            if (x == 4 && z == 4)
                continue;
            hm.setHeight(x, z, 0.0f);
        }
    }
    const Aabb3f cube = Aabb3f::FromCenterExtents(Vector3f{ 4.5f, 10.5f, 4.5f }, Vector3f{ 0.5f, 0.5f, 0.5f });
    Walkability w;
    WalkabilityDesc d;
    d.heightMap   = &hm;
    d.waterLevel  = 5.0f;
    d.cubes       = &cube;
    d.cubeCount   = 1;
    ASSERT_TRUE(w.bake(d));
    Pathfinder pf;
    ASSERT_TRUE(pf.bind(&w));
    PathRequest req;
    req.startX = 0.5f;
    req.startZ = 0.5f;
    req.destX  = 4.5f;
    req.destZ  = 4.5f;
    PathResult out;
    EXPECT_FALSE(pf.find(req, out));
}

TEST(Pathfinder, NoWetCornerCut)
{
    HeightMap hm = MakeFlat(6, 10.0f);
    hm.setHeight(2, 2, 0.0f);
    Walkability w;
    WalkabilityDesc d;
    d.heightMap  = &hm;
    d.waterLevel = 5.0f;
    ASSERT_TRUE(w.bake(d));
    Pathfinder pf;
    ASSERT_TRUE(pf.bind(&w));
    PathRequest req;
    req.startX = 1.5f;
    req.startZ = 2.5f;
    req.destX  = 2.5f;
    req.destZ  = 1.5f;
    PathResult out;
    if (pf.find(req, out))
    {
        for (const Vector3f& v : out.points)
            EXPECT_FALSE(w.destWet(v.x, v.z));
    }
}

TEST(Pathfinder, DynamicDetourAroundAgent)
{
    HeightMap hm = MakeFlat(9, 10.0f);
    Walkability w;
    WalkabilityDesc d;
    d.heightMap  = &hm;
    d.waterLevel = -10.0f;
    ASSERT_TRUE(w.bake(d));
    Pathfinder pf;
    ASSERT_TRUE(pf.bind(&w));
    AgentStamp other;
    other.x      = 4.5f;
    other.z      = 4.5f;
    other.radius = 0.8f;
    PathRequest req;
    req.startX     = 1.5f;
    req.startZ     = 4.5f;
    req.destX      = 7.5f;
    req.destZ      = 4.5f;
    req.others     = &other;
    req.otherCount = 1;
    PathResult out;
    ASSERT_TRUE(pf.find(req, out));
    for (const Vector3f& v : out.points)
    {
        const float dx = v.x - other.x;
        const float dz = v.z - other.z;
        EXPECT_GT(dx * dx + dz * dz, 0.5f);
    }
}
