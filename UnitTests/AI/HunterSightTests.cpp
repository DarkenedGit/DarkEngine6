#include <gtest/gtest.h>

#include "AI/HunterSight.h"
#include "Math/Vector3f.h"
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

    HeightMap MakeRidge()
    {
        HeightMap hm = MakeFlat(17, 1.0f, 0.0f);
        for (int z = 0; z < 17; ++z)
            hm.setHeight(8, z, 20.0f);
        return hm;
    }
} // namespace

TEST(HunterSight, FlatInConeIsSeen)
{
    HeightMap hm = MakeFlat(17, 1.0f, 0.0f);
    HunterSightQuery q;
    q.eye       = Vector3f{ 2.0f, 1.5f, 8.0f };
    q.forward   = Vector3f{ 1.0f, 0.0f, 0.0f };
    q.target    = Vector3f{ 10.0f, 1.5f, 8.0f };
    q.coneDeg   = 70.0f;
    q.range     = 25.0f;
    q.heightMap = &hm;
    EXPECT_TRUE(sees(q));
}

TEST(HunterSight, HillBuriesTarget)
{
    HeightMap hm = MakeRidge();
    HunterSightQuery q;
    q.eye       = Vector3f{ 2.0f, 1.5f, 8.0f };
    q.forward   = Vector3f{ 1.0f, 0.0f, 0.0f };
    q.target    = Vector3f{ 14.0f, 1.5f, 8.0f };
    q.coneDeg   = 70.0f;
    q.range     = 25.0f;
    q.heightMap = &hm;
    EXPECT_FALSE(sees(q));
}

TEST(HunterSight, DoesNotUseCubes)
{
    HeightMap hm = MakeFlat(17, 1.0f, 0.0f);
    HunterSightQuery q;
    q.eye       = Vector3f{ 2.0f, 1.5f, 8.0f };
    q.forward   = Vector3f{ 1.0f, 0.0f, 0.0f };
    q.target    = Vector3f{ 10.0f, 1.5f, 8.0f };
    q.coneDeg   = 70.0f;
    q.range     = 25.0f;
    q.heightMap = &hm;
    EXPECT_TRUE(sees(q));
}

TEST(HunterSight, BehindForwardIsUnseen)
{
    HeightMap hm = MakeFlat(17, 1.0f, 0.0f);
    HunterSightQuery q;
    q.eye       = Vector3f{ 8.0f, 1.5f, 8.0f };
    q.forward   = Vector3f{ 0.0f, 0.0f, 1.0f };
    q.target    = Vector3f{ 8.0f, 1.5f, 2.0f };
    q.coneDeg   = 70.0f;
    q.range     = 25.0f;
    q.heightMap = &hm;
    EXPECT_FALSE(sees(q));
}

TEST(HunterSight, BeyondRangeIsUnseen)
{
    HeightMap hm = MakeFlat(17, 1.0f, 0.0f);
    HunterSightQuery q;
    q.eye       = Vector3f{ 1.0f, 1.5f, 8.0f };
    q.forward   = Vector3f{ 1.0f, 0.0f, 0.0f };
    q.target    = Vector3f{ 15.0f, 1.5f, 8.0f };
    q.coneDeg   = 70.0f;
    q.range     = 5.0f;
    q.heightMap = &hm;
    EXPECT_FALSE(sees(q));
}

TEST(HunterSight, SameXzDoesNotHang)
{
    HeightMap hm = MakeFlat(9, 1.0f, 0.0f);
    HunterSightQuery q;
    q.eye       = Vector3f{ 4.0f, 1.5f, 4.0f };
    q.forward   = Vector3f{ 0.0f, 0.0f, 1.0f };
    q.target    = Vector3f{ 4.0f, 1.6f, 4.0f };
    q.coneDeg   = 70.0f;
    q.range     = 25.0f;
    q.heightMap = &hm;
    EXPECT_TRUE(sees(q));
}
