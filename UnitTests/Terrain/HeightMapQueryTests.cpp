#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Math/Ray3f.h"
#include "Terrain/HeightMap.h"

#include <cmath>

using namespace Dark::Math;
using namespace Dark::Terrain;
using namespace Dark::Collision;

namespace
{

HeightMap MakeFlat(int samples, float y, float cell = 1.0f)
{
    HeightMap hm;
    hm.create(static_cast<uint32_t>(samples), static_cast<uint32_t>(samples), cell, 1.0f);
    for (int z = 0; z < samples; ++z)
    {
        for (int x = 0; x < samples; ++x)
            hm.setHeight(x, z, y);
    }
    return hm;
}

HeightMap MakeRamp(int samples, float cell = 1.0f)
{
    HeightMap hm;
    hm.create(static_cast<uint32_t>(samples), static_cast<uint32_t>(samples), cell, 1.0f);
    for (int z = 0; z < samples; ++z)
    {
        for (int x = 0; x < samples; ++x)
            hm.setHeight(x, z, static_cast<float>(x) * 0.25f);
    }
    return hm;
}

} // namespace

TEST(HeightMapQuery, HeightAtWorldBilinear)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(2, 2, 2.0f, 1.0f));
    hm.setHeight(0, 0, 0.0f);
    hm.setHeight(1, 0, 4.0f);
    hm.setHeight(0, 1, 0.0f);
    hm.setHeight(1, 1, 4.0f);

    EXPECT_NEAR(hm.heightAtWorld(0.0f, 0.0f), 0.0f, 1.0e-5f);
    EXPECT_NEAR(hm.heightAtWorld(2.0f, 0.0f), 4.0f, 1.0e-5f);
    EXPECT_NEAR(hm.heightAtWorld(1.0f, 1.0f), 2.0f, 1.0e-5f);

    float y = -1.0f;
    EXPECT_TRUE(hm.tryHeightAtWorld(1.0f, 1.0f, y));
    EXPECT_NEAR(y, 2.0f, 1.0e-5f);
    EXPECT_FALSE(hm.tryHeightAtWorld(-1.0f, 0.0f, y));
    EXPECT_FALSE(hm.tryHeightAtWorld(3.0f, 0.0f, y));
}

TEST(HeightMapQuery, VerticalRayHitsHeight)
{
    HeightMap hm = MakeFlat(9, 3.0f);
    const Ray3f down{ Vector3f{ 4.0f, 10.0f, 4.0f }, Vector3f{ 0.0f, -1.0f, 0.0f } };
    const RayHit3D hit = hm.raycast(down);
    ASSERT_TRUE(hit.hit);
    EXPECT_NEAR(hit.t, 7.0f, 1.0e-4f);
    EXPECT_NEAR(hit.point.y, 3.0f, 1.0e-4f);
    EXPECT_NEAR(hit.point.y, hm.heightAtWorld(hit.point.x, hit.point.z), 1.0e-4f);
    EXPECT_GT(hit.normal.y, 0.0f);

    const Ray3f up{ Vector3f{ 4.0f, 10.0f, 4.0f }, Vector3f{ 0.0f, 1.0f, 0.0f } };
    EXPECT_FALSE(hm.raycast(up).hit);
}

TEST(HeightMapQuery, AngledRayMatchesBilinear)
{
    HeightMap hm = MakeRamp(17);
    const Ray3f ray{ Vector3f{ 0.0f, 8.0f, 4.0f }, Vector3f{ 1.0f, -0.5f, 0.0f } };
    Vector3f dir = ray.Direction;
    dir.Normalize();
    const Ray3f unit{ ray.Origin, dir };

    const RayHit3D hit = hm.raycast(unit);
    ASSERT_TRUE(hit.hit);
    EXPECT_NEAR(hit.point.y, hm.heightAtWorld(hit.point.x, hit.point.z), 1.5e-3f);
    EXPECT_GE(hit.t, 0.0f);
}

TEST(HeightMapQuery, SideMissAndMaxDistance)
{
    HeightMap hm = MakeFlat(5, 0.0f);
    const Ray3f miss{ Vector3f{ -10.0f, 5.0f, 2.0f }, Vector3f{ 0.0f, 0.0f, 1.0f } };
    EXPECT_FALSE(hm.raycast(miss).hit);

    const Ray3f down{ Vector3f{ 2.0f, 10.0f, 2.0f }, Vector3f{ 0.0f, -1.0f, 0.0f } };
    EXPECT_FALSE(hm.raycast(down, 4.0f).hit);
    EXPECT_TRUE(hm.raycast(down, 20.0f).hit);
}

TEST(HeightMapQuery, FromBelowGoingUp)
{
    HeightMap hm = MakeFlat(5, 2.0f);
    const Ray3f up{ Vector3f{ 2.0f, -3.0f, 2.0f }, Vector3f{ 0.0f, 1.0f, 0.0f } };
    const RayHit3D hit = hm.raycast(up);
    ASSERT_TRUE(hit.hit);
    EXPECT_NEAR(hit.t, 5.0f, 1.0e-4f);
    EXPECT_NEAR(hit.point.y, 2.0f, 1.0e-4f);
}

TEST(HeightMapQuery, GrazingRayClosestHit)
{
    HeightMap hm = MakeFlat(33, 0.0f, 1.0f);
    // Long ray just above the surface should miss; just below origin shooting
    // along +X while descending should hit once near the start.
    const Ray3f above{ Vector3f{ 1.0f, 0.25f, 16.0f }, Vector3f{ 1.0f, 0.0f, 0.0f } };
    EXPECT_FALSE(hm.raycast(above).hit);

    Vector3f dir{ 1.0f, -0.05f, 0.0f };
    dir.Normalize();
    const Ray3f down{ Vector3f{ 1.0f, 0.4f, 16.0f }, dir };
    const RayHit3D hit = hm.raycast(down);
    ASSERT_TRUE(hit.hit);
    EXPECT_LT(hit.point.x, 16.0f);
    EXPECT_NEAR(hit.point.y, 0.0f, 2.0e-3f);
}

TEST(HeightMapQuery, HierarchyMatchesBruteOnFbm)
{
    HeightMap hm;
    ASSERT_TRUE(hm.createFbm(33, 33, 123u, 5, 4.0f, 1.0f, 2.0f, 0.5f, 1.0f, 4.0f));

    // Vertical rays over the interior must hit, and every hit (any direction)
    // must lie on the same bilinear surface as heightAtWorld.
    int verticalHits = 0;
    int angledHits   = 0;
    for (int i = 0; i < 16; ++i)
    {
        const float x = 2.0f + static_cast<float>(i) * 1.7f;
        const float z = 3.0f + static_cast<float>(i % 7) * 3.0f;
        const Ray3f down{ Vector3f{ x, 80.0f, z }, Vector3f{ 0.0f, -1.0f, 0.0f } };
        const RayHit3D h = hm.raycast(down);
        ASSERT_TRUE(h.hit) << i;
        EXPECT_NEAR(h.point.y, hm.heightAtWorld(h.point.x, h.point.z), 1.5e-3f);
        ++verticalHits;

        Vector3f dir{ 0.12f, -1.0f, 0.07f };
        dir.Normalize();
        const Ray3f angled{ Vector3f{ 8.0f + static_cast<float>(i % 5), 25.0f, 8.0f + static_cast<float>(i % 4) }, dir };
        const RayHit3D ha = hm.raycast(angled);
        if (ha.hit)
        {
            EXPECT_NEAR(ha.point.y, hm.heightAtWorld(ha.point.x, ha.point.z), 2.0e-3f);
            ++angledHits;
        }
    }
    EXPECT_EQ(verticalHits, 16);
    EXPECT_GE(angledHits, 8);
}

TEST(HeightMapQuery, TwistedCellQuadratic)
{
    // Non-planar quad: bilinear twist, e3 != 0.
    HeightMap hm;
    ASSERT_TRUE(hm.create(2, 2, 1.0f, 1.0f));
    hm.setHeight(0, 0, 0.0f);
    hm.setHeight(1, 0, 0.0f);
    hm.setHeight(0, 1, 0.0f);
    hm.setHeight(1, 1, 4.0f);

    const float mid = hm.heightAtWorld(0.5f, 0.5f);
    EXPECT_NEAR(mid, 1.0f, 1.0e-4f); // 0.5*0.5*4

    const Ray3f down{ Vector3f{ 0.5f, 5.0f, 0.5f }, Vector3f{ 0.0f, -1.0f, 0.0f } };
    const RayHit3D hit = hm.raycast(down);
    ASSERT_TRUE(hit.hit);
    EXPECT_NEAR(hit.point.y, mid, 1.0e-3f);
}
