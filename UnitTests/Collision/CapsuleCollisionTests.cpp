#include <gtest/gtest.h>
#include "Collision/StaticCollision.h"
#include "Collision/SweptCollision.h"
#include "Math/Capsule3f.h"
#include "Math/Sphere3f.h"
#include "Math/AABox3f.h"
#include "Math/Box3f.h"
#include "Math/Ray3f.h"
#include "Math/Vector3f.h"

using namespace Dark::Math;
using namespace Dark::Collision;

TEST(CapsuleCollisionStatic, PointCapsule)
{
	Capsule3f c{Vector3f{0.0f, -1.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, 1.0f};
	EXPECT_TRUE(Intersects(Vector3f{0.0f, 0.0f, 0.0f}, c));
	EXPECT_TRUE(Intersects(Vector3f{0.9f, 0.0f, 0.0f}, c));
	EXPECT_FALSE(Intersects(Vector3f{2.0f, 0.0f, 0.0f}, c));
	EXPECT_TRUE(Intersects(c, Vector3f{0.0f, 0.0f, 0.0f})); // symmetric helper
}

TEST(CapsuleCollisionStatic, CapsuleCapsuleAndSphere)
{
	Capsule3f a{Vector3f{0.0f, -1.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, 0.5f};
	Capsule3f b{Vector3f{0.8f, -1.0f, 0.0f}, Vector3f{0.8f, 1.0f, 0.0f}, 0.5f};
	Capsule3f far{Vector3f{5.0f, -1.0f, 0.0f}, Vector3f{5.0f, 1.0f, 0.0f}, 0.5f};
	EXPECT_TRUE(Intersects(a, b));
	EXPECT_FALSE(Intersects(a, far));

	Sphere3f s{Vector3f{1.2f, 0.0f, 0.0f}, 0.5f};
	EXPECT_TRUE(Intersects(a, s));
	EXPECT_TRUE(Intersects(s, a));
}

TEST(CapsuleCollisionStatic, CapsuleAabb)
{
	Capsule3f c{Vector3f{0.0f, -1.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, 0.5f};
	AABox3f box{Vector3f{0.6f, -0.5f, -0.5f}, Vector3f{2.0f, 0.5f, 0.5f}};
	AABox3f far{Vector3f{5.0f, -1.0f, -1.0f}, Vector3f{6.0f, 1.0f, 1.0f}};
	EXPECT_TRUE(Intersects(c, box));
	EXPECT_FALSE(Intersects(c, far));
}

TEST(CapsuleCollisionStatic, CapsuleObb)
{
	Capsule3f c{Vector3f{0.0f, -1.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, 0.5f};
	Box3f box = Box3f::FromCenterExtents(Vector3f{1.2f, 0.0f, 0.0f}, Vector3f{0.5f, 0.5f, 0.5f});
	Box3f far = Box3f::FromCenterExtents(Vector3f{5.0f, 0.0f, 0.0f}, Vector3f{0.5f, 0.5f, 0.5f});
	EXPECT_TRUE(Intersects(c, box));
	EXPECT_FALSE(Intersects(c, far));
}

TEST(CapsuleCollisionStatic, RayCapsuleHitAndMiss)
{
	Capsule3f c{Vector3f{0.0f, -1.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, 1.0f};
	Ray3f hitRay{Vector3f{-5.0f, 0.0f, 0.0f}, Vector3f{1.0f, 0.0f, 0.0f}};
	Ray3f missRay{Vector3f{-5.0f, 0.0f, 5.0f}, Vector3f{1.0f, 0.0f, 0.0f}};

	RayHit3D hit = Intersect(hitRay, c);
	ASSERT_TRUE(hit.hit);
	EXPECT_NEAR(hit.t, 4.0f, 1.0e-3f); // surface at x = -1

	RayHit3D miss = Intersect(missRay, c);
	EXPECT_FALSE(miss.hit);
}

TEST(CapsuleCollisionStatic, RayCapsuleEndCap)
{
	Capsule3f c{Vector3f{0.0f, -1.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, 1.0f};
	Ray3f ray{Vector3f{0.0f, -5.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}};
	RayHit3D hit = Intersect(ray, c);
	ASSERT_TRUE(hit.hit);
	EXPECT_NEAR(hit.t, 3.0f, 1.0e-3f); // hits bottom cap at y = -2
}

TEST(CapsuleCollisionSwept, PointVsCapsule)
{
	Capsule3f c{Vector3f{0.0f, -1.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, 1.0f};
	SweptHit3D hit = SweptIntersects(Vector3f{-5.0f, 0.0f, 0.0f}, Vector3f{5.0f, 0.0f, 0.0f}, c);
	ASSERT_TRUE(hit.hit);
	EXPECT_NEAR(hit.t, 4.0f / 5.0f, 1.0e-3f);

	SweptHit3D miss = SweptIntersects(Vector3f{-5.0f, 0.0f, 5.0f}, Vector3f{5.0f, 0.0f, 0.0f}, c);
	EXPECT_FALSE(miss.hit);
}

TEST(CapsuleCollisionSwept, SphereVsCapsule)
{
	Capsule3f c{Vector3f{0.0f, -1.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, 0.5f};
	Sphere3f s{Vector3f{-3.0f, 0.0f, 0.0f}, 0.5f};
	SweptHit3D hit = SweptIntersects(s, Vector3f{3.0f, 0.0f, 0.0f}, c);
	ASSERT_TRUE(hit.hit);
	EXPECT_GT(hit.t, 0.0f);
	EXPECT_LE(hit.t, 1.0f);
}

TEST(CapsuleCollisionSwept, CapsuleVsCapsuleRelative)
{
	Capsule3f a{Vector3f{-3.0f, -1.0f, 0.0f}, Vector3f{-3.0f, 1.0f, 0.0f}, 0.5f};
	Capsule3f b{Vector3f{0.0f, -1.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, 0.5f};
	SweptHit3D hit = SweptIntersects(a, Vector3f{3.0f, 0.0f, 0.0f}, b);
	ASSERT_TRUE(hit.hit);
	EXPECT_NEAR(hit.t, 2.0f / 3.0f, 5.0e-2f); // centers approach; radii 0.5+0.5

	SweptHit3D already = SweptIntersects(a, Vector3f{0.0f, 0.0f, 0.0f}, Capsule3f{Vector3f{-3.0f, -1.0f, 0.0f}, Vector3f{-3.0f, 1.0f, 0.0f}, 0.5f});
	EXPECT_TRUE(already.hit);
	EXPECT_FLOAT_EQ(already.t, 0.0f);
}

TEST(CapsuleCollisionSwept, CapsuleVsAabbAndObb)
{
	Capsule3f c{Vector3f{-3.0f, 0.0f, 0.0f}, Vector3f{-3.0f, 2.0f, 0.0f}, 0.5f};
	AABox3f box{Vector3f{-0.5f, -0.5f, -0.5f}, Vector3f{0.5f, 0.5f, 0.5f}};
	SweptHit3D hitAabb = SweptIntersects(c, Vector3f{3.0f, 0.0f, 0.0f}, box);
	ASSERT_TRUE(hitAabb.hit);
	EXPECT_GT(hitAabb.t, 0.0f);
	EXPECT_LE(hitAabb.t, 1.0f);

	Box3f obb = Box3f::FromCenterExtents(Vector3f{0.0f, 0.0f, 0.0f}, Vector3f{0.5f, 0.5f, 0.5f});
	SweptHit3D hitObb = SweptIntersects(c, Vector3f{3.0f, 0.0f, 0.0f}, obb);
	ASSERT_TRUE(hitObb.hit);
	EXPECT_GT(hitObb.t, 0.0f);
	EXPECT_LE(hitObb.t, 1.0f);
}

TEST(CapsuleCollisionSwept, CapsuleVsSphere)
{
	Capsule3f c{Vector3f{-3.0f, -1.0f, 0.0f}, Vector3f{-3.0f, 1.0f, 0.0f}, 0.5f};
	Sphere3f s{Vector3f{0.0f, 0.0f, 0.0f}, 0.5f};
	SweptHit3D hit = SweptIntersects(c, Vector3f{3.0f, 0.0f, 0.0f}, s);
	ASSERT_TRUE(hit.hit);
	EXPECT_GT(hit.t, 0.0f);
	EXPECT_LE(hit.t, 1.0f);
}

TEST(CapsuleCollisionSwept, MissPreservesDefaultT)
{
	Capsule3f c{Vector3f{0.0f, -1.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, 0.5f};
	SweptHit3D miss = SweptIntersects(Vector3f{0.0f, 0.0f, 10.0f}, Vector3f{1.0f, 0.0f, 0.0f}, c);
	EXPECT_FALSE(miss.hit);
	EXPECT_FLOAT_EQ(miss.t, 1.0f);
}
