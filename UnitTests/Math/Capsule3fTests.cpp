#include <gtest/gtest.h>
#include "Math/Capsule3f.h"
#include "Math/Sphere3f.h"
#include "Math/Vector3f.h"
#include "Math/MathHelper.h"

using namespace Dark::Math;

namespace
{
bool Near(const Vector3f& a, const Vector3f& b, float eps = 1.0e-4f)
{
	return NearEqual(a.x, b.x, eps) && NearEqual(a.y, b.y, eps) && NearEqual(a.z, b.z, eps);
}
} // namespace

TEST(Capsule3f, ConstructionAndAccessors)
{
	Capsule3f c{Vector3f{0.0f, -1.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, 0.5f};
	EXPECT_TRUE(Near(c.Center(), Vector3f{0.0f, 0.0f, 0.0f}));
	EXPECT_NEAR(c.SegmentLength(), 2.0f, 1.0e-5f);
	EXPECT_NEAR(c.HalfHeight(), 1.0f, 1.0e-5f);
	EXPECT_NEAR(c.Height(), 3.0f, 1.0e-5f); // 2 + 2*0.5
	EXPECT_TRUE(Near(c.Axis(), Vector3f{0.0f, 1.0f, 0.0f}));
}

TEST(Capsule3f, FromCenterAxis)
{
	Capsule3f c = Capsule3f::FromCenterAxis(Vector3f{0.0f, 0.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, 1.0f, 0.25f);
	EXPECT_TRUE(Near(c.PointA, Vector3f{0.0f, -1.0f, 0.0f}));
	EXPECT_TRUE(Near(c.PointB, Vector3f{0.0f, 1.0f, 0.0f}));
	EXPECT_FLOAT_EQ(c.Radius, 0.25f);
}

TEST(Capsule3f, ContainsAndDistance)
{
	Capsule3f c{Vector3f{0.0f, -1.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, 1.0f};
	EXPECT_TRUE(c.Contains(Vector3f{0.0f, 0.0f, 0.0f}));
	EXPECT_TRUE(c.Contains(Vector3f{0.5f, 0.0f, 0.0f}));
	EXPECT_FALSE(c.Contains(Vector3f{2.0f, 0.0f, 0.0f}));

	EXPECT_NEAR(c.SignedDistance(Vector3f{2.0f, 0.0f, 0.0f}), 1.0f, 1.0e-4f);
	EXPECT_NEAR(c.Distance(Vector3f{0.0f, 0.0f, 0.0f}), 0.0f, 1.0e-5f);
}

TEST(Capsule3f, ClosestPointOnSegment)
{
	Capsule3f c{Vector3f{0.0f, 0.0f, 0.0f}, Vector3f{4.0f, 0.0f, 0.0f}, 0.5f};
	EXPECT_TRUE(Near(c.ClosestPointOnSegment(Vector3f{2.0f, 3.0f, 0.0f}), Vector3f{2.0f, 0.0f, 0.0f}));
	EXPECT_TRUE(Near(c.ClosestPointOnSegment(Vector3f{-1.0f, 1.0f, 0.0f}), Vector3f{0.0f, 0.0f, 0.0f}));
	EXPECT_TRUE(Near(c.ClosestPointOnSegment(Vector3f{5.0f, 1.0f, 0.0f}), Vector3f{4.0f, 0.0f, 0.0f}));
}

TEST(Capsule3f, IntersectsSphereAndCapsule)
{
	Capsule3f a{Vector3f{0.0f, -1.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, 0.5f};
	Sphere3f sNear{Vector3f{1.0f, 0.0f, 0.0f}, 0.6f};
	Sphere3f sFar{Vector3f{3.0f, 0.0f, 0.0f}, 0.5f};
	EXPECT_TRUE(a.Intersects(sNear));
	EXPECT_FALSE(a.Intersects(sFar));

	Capsule3f b{Vector3f{1.0f, -1.0f, 0.0f}, Vector3f{1.0f, 1.0f, 0.0f}, 0.6f};
	Capsule3f c{Vector3f{5.0f, -1.0f, 0.0f}, Vector3f{5.0f, 1.0f, 0.0f}, 0.5f};
	EXPECT_TRUE(a.Intersects(b));
	EXPECT_FALSE(a.Intersects(c));
}

TEST(Capsule3f, BoundingSphereAndAabb)
{
	Capsule3f cap{Vector3f{0.0f, -2.0f, 0.0f}, Vector3f{0.0f, 2.0f, 0.0f}, 1.0f};
	Sphere3f bs = cap.ToBoundingSphere();
	EXPECT_TRUE(Near(bs.Center, Vector3f{0.0f, 0.0f, 0.0f}));
	EXPECT_NEAR(bs.Radius, 3.0f, 1.0e-5f); // halfHeight 2 + radius 1

	AABox3f box = cap.ToAABox();
	EXPECT_TRUE(Near(box.Min, Vector3f{-1.0f, -3.0f, -1.0f}));
	EXPECT_TRUE(Near(box.Max, Vector3f{1.0f, 3.0f, 1.0f}));
}

TEST(Capsule3f, ClosestPointsOnSegments)
{
	Vector3f outA, outB;
	float sa = -1.0f, sb = -1.0f;
	float d2 = ClosestPointsOnSegments(
		Vector3f{0.0f, 0.0f, 0.0f}, Vector3f{2.0f, 0.0f, 0.0f},
		Vector3f{1.0f, 1.0f, 0.0f}, Vector3f{1.0f, 3.0f, 0.0f},
		outA, outB, &sa, &sb);
	EXPECT_NEAR(d2, 1.0f, 1.0e-4f);
	EXPECT_TRUE(Near(outA, Vector3f{1.0f, 0.0f, 0.0f}));
	EXPECT_TRUE(Near(outB, Vector3f{1.0f, 1.0f, 0.0f}));
	EXPECT_NEAR(sa, 0.5f, 1.0e-4f);
	EXPECT_NEAR(sb, 0.0f, 1.0e-4f);
}

TEST(Capsule3f, DegenerateIsSphere)
{
	Capsule3f c{Vector3f{1.0f, 2.0f, 3.0f}, Vector3f{1.0f, 2.0f, 3.0f}, 2.0f};
	EXPECT_TRUE(Near(c.Axis(), Vector3f::ZERO));
	EXPECT_NEAR(c.SegmentLength(), 0.0f, 1.0e-6f);
	EXPECT_TRUE(c.Contains(Vector3f{1.0f, 2.0f, 3.0f}));
	EXPECT_FALSE(c.Contains(Vector3f{1.0f, 2.0f, 6.0f}));
}
