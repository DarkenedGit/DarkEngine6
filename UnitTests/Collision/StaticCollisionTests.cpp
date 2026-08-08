#include <gtest/gtest.h>
#include "Collision/StaticCollision.h"
#include "Math/Sphere3f.h"
#include "Math/Aabb3f.h"
#include "Math/Ray3f.h"
#include "Math/Vector3f.h"

using namespace Dark::Math;
using namespace Dark::Collision;

TEST(CollisionStatic, PointSphere)
{
    Sphere3f s{Vector3f{0.0f, 0.0f, 0.0f}, 1.0f};
    EXPECT_TRUE(Intersects(Vector3f{0.0f, 0.0f, 0.0f}, s));
    EXPECT_TRUE(Intersects(Vector3f{0.5f, 0.0f, 0.0f}, s));
    EXPECT_FALSE(Intersects(Vector3f{2.0f, 0.0f, 0.0f}, s));
}

TEST(CollisionStatic, SphereSphere)
{
    Sphere3f a{Vector3f{0.0f, 0.0f, 0.0f}, 1.0f};
    Sphere3f b{Vector3f{1.5f, 0.0f, 0.0f}, 1.0f};
    Sphere3f c{Vector3f{3.0f, 0.0f, 0.0f}, 0.5f};

    EXPECT_TRUE(Intersects(a, b));
    EXPECT_FALSE(Intersects(a, c));
}

TEST(CollisionStatic, PointAabb)
{
    Aabb3f box{Vector3f{-1.0f, -1.0f, -1.0f}, Vector3f{1.0f, 1.0f, 1.0f}};
    EXPECT_TRUE(Intersects(Vector3f{0.0f, 0.0f, 0.0f}, box));
    EXPECT_TRUE(Intersects(Vector3f{1.0f, 1.0f, 1.0f}, box)); // on boundary
    EXPECT_FALSE(Intersects(Vector3f{1.1f, 0.0f, 0.0f}, box));
}

TEST(CollisionStatic, AabbAabb)
{
    Aabb3f a{Vector3f{0.0f, 0.0f, 0.0f}, Vector3f{1.0f, 1.0f, 1.0f}};
    Aabb3f b{Vector3f{0.5f, 0.5f, 0.5f}, Vector3f{2.0f, 2.0f, 2.0f}};
    Aabb3f c{Vector3f{2.0f, 2.0f, 2.0f}, Vector3f{3.0f, 3.0f, 3.0f}};

    EXPECT_TRUE(Intersects(a, b));
    EXPECT_FALSE(Intersects(a, c));
}

TEST(CollisionStatic, SphereAabb)
{
    Aabb3f box{Vector3f{-1.0f, -1.0f, -1.0f}, Vector3f{1.0f, 1.0f, 1.0f}};
    Sphere3f inside{Vector3f{0.0f, 0.0f, 0.0f}, 0.25f};
    Sphere3f touching{Vector3f{2.0f, 0.0f, 0.0f}, 1.0f};
    Sphere3f farAway{Vector3f{5.0f, 0.0f, 0.0f}, 0.5f};

    EXPECT_TRUE(Intersects(inside, box));
    EXPECT_TRUE(Intersects(touching, box));
    EXPECT_FALSE(Intersects(farAway, box));
}

TEST(CollisionStatic, RaySphereHitAndMiss)
{
    Sphere3f s{Vector3f{0.0f, 0.0f, 0.0f}, 1.0f};
    Ray3f hitRay{Vector3f{-5.0f, 0.0f, 0.0f}, Vector3f{1.0f, 0.0f, 0.0f}};
    Ray3f missRay{Vector3f{-5.0f, 5.0f, 0.0f}, Vector3f{1.0f, 0.0f, 0.0f}};

    RayHit3D hit = Intersect(hitRay, s);
    ASSERT_TRUE(hit.hit);
    EXPECT_NEAR(hit.t, 4.0f, 1.0e-4f); // enters at x = -1

    RayHit3D miss = Intersect(missRay, s);
    EXPECT_FALSE(miss.hit);
}

TEST(CollisionStatic, RayAabb)
{
    Aabb3f box{Vector3f{-1.0f, -1.0f, -1.0f}, Vector3f{1.0f, 1.0f, 1.0f}};
    Ray3f ray{Vector3f{-5.0f, 0.0f, 0.0f}, Vector3f{1.0f, 0.0f, 0.0f}};

    RayHit3D hit = Intersect(ray, box);
    ASSERT_TRUE(hit.hit);
    EXPECT_NEAR(hit.t, 4.0f, 1.0e-4f);
}
