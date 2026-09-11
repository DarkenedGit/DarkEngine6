#include <gtest/gtest.h>
#include "Math/Capsule2f.h"
#include "Math/Sphere2f.h"
#include "Math/Vector2f.h"
#include "Math/MathHelper.h"

using namespace Dark::Math;

namespace
{
bool Near(const Vector2f& a, const Vector2f& b, float eps = 1.0e-4f)
{
	return NearEqual(a.x, b.x, eps) && NearEqual(a.y, b.y, eps);
}
} // namespace

TEST(Capsule2f, ConstructionAndAccessors)
{
	Capsule2f c{Vector2f{0.0f, -1.0f}, Vector2f{0.0f, 1.0f}, 0.5f};
	EXPECT_TRUE(Near(c.Center(), Vector2f{0.0f, 0.0f}));
	EXPECT_NEAR(c.SegmentLength(), 2.0f, 1.0e-5f);
	EXPECT_NEAR(c.HalfHeight(), 1.0f, 1.0e-5f);
	EXPECT_NEAR(c.Height(), 3.0f, 1.0e-5f); // 2 + 2*0.5
	EXPECT_TRUE(Near(c.Axis(), Vector2f{0.0f, 1.0f}));
}

TEST(Capsule2f, FromCenterAxis)
{
	Capsule2f c = Capsule2f::FromCenterAxis(Vector2f{0.0f, 0.0f}, Vector2f{0.0f, 1.0f}, 1.0f, 0.25f);
	EXPECT_TRUE(Near(c.PointA, Vector2f{0.0f, -1.0f}));
	EXPECT_TRUE(Near(c.PointB, Vector2f{0.0f, 1.0f}));
	EXPECT_FLOAT_EQ(c.Radius, 0.25f);
}

TEST(Capsule2f, ContainsAndDistance)
{
	Capsule2f c{Vector2f{0.0f, -1.0f}, Vector2f{0.0f, 1.0f}, 1.0f};
	EXPECT_TRUE(c.Contains(Vector2f{0.0f, 0.0f}));
	EXPECT_TRUE(c.Contains(Vector2f{0.5f, 0.0f}));
	EXPECT_FALSE(c.Contains(Vector2f{2.0f, 0.0f}));

	EXPECT_NEAR(c.SignedDistance(Vector2f{2.0f, 0.0f}), 1.0f, 1.0e-4f);
	EXPECT_NEAR(c.Distance(Vector2f{0.0f, 0.0f}), 0.0f, 1.0e-5f);
	EXPECT_NEAR(c.DistanceSq(Vector2f{2.0f, 0.0f}), 1.0f, 1.0e-4f);
}

TEST(Capsule2f, ClosestPointOnSegment)
{
	Capsule2f c{Vector2f{0.0f, 0.0f}, Vector2f{4.0f, 0.0f}, 0.5f};
	EXPECT_TRUE(Near(c.ClosestPointOnSegment(Vector2f{2.0f, 3.0f}), Vector2f{2.0f, 0.0f}));
	EXPECT_TRUE(Near(c.ClosestPointOnSegment(Vector2f{-1.0f, 1.0f}), Vector2f{0.0f, 0.0f}));
	EXPECT_TRUE(Near(c.ClosestPointOnSegment(Vector2f{5.0f, 1.0f}), Vector2f{4.0f, 0.0f}));
}

TEST(Capsule2f, ClosestPointOnSurface)
{
	Capsule2f c{Vector2f{0.0f, 0.0f}, Vector2f{4.0f, 0.0f}, 1.0f};
	EXPECT_TRUE(Near(c.ClosestPoint(Vector2f{2.0f, 3.0f}), Vector2f{2.0f, 1.0f}));
	EXPECT_TRUE(Near(c.ClosestPoint(Vector2f{2.0f, 0.5f}), Vector2f{2.0f, 0.5f})); // inside
}

TEST(Capsule2f, DistanceToCircleAndCapsule)
{
	Capsule2f a{Vector2f{0.0f, -1.0f}, Vector2f{0.0f, 1.0f}, 0.5f};
	Sphere2f nearCircle{Vector2f{2.0f, 0.0f}, 0.5f};
	Sphere2f overlapping{Vector2f{0.8f, 0.0f}, 0.5f};
	EXPECT_NEAR(a.Distance(nearCircle), 1.0f, 1.0e-4f); // 2 - 0.5 - 0.5
	EXPECT_NEAR(a.Distance(overlapping), 0.0f, 1.0e-5f);

	Capsule2f far{Vector2f{5.0f, -1.0f}, Vector2f{5.0f, 1.0f}, 0.5f};
	EXPECT_NEAR(a.Distance(far), 4.0f, 1.0e-4f); // 5 - 0.5 - 0.5
}

TEST(Capsule2f, IntersectsCircleAndCapsule)
{
	Capsule2f a{Vector2f{0.0f, -1.0f}, Vector2f{0.0f, 1.0f}, 0.5f};
	Sphere2f sNear{Vector2f{1.0f, 0.0f}, 0.6f};
	Sphere2f sFar{Vector2f{3.0f, 0.0f}, 0.5f};
	EXPECT_TRUE(a.Intersects(sNear));
	EXPECT_FALSE(a.Intersects(sFar));

	Capsule2f b{Vector2f{1.0f, -1.0f}, Vector2f{1.0f, 1.0f}, 0.6f};
	Capsule2f c{Vector2f{5.0f, -1.0f}, Vector2f{5.0f, 1.0f}, 0.5f};
	EXPECT_TRUE(a.Intersects(b));
	EXPECT_FALSE(a.Intersects(c));
}

TEST(Capsule2f, BoundingCircleAndAabb)
{
	Capsule2f cap{Vector2f{0.0f, -2.0f}, Vector2f{0.0f, 2.0f}, 1.0f};
	Sphere2f bs = cap.ToBoundingCircle();
	EXPECT_TRUE(Near(bs.Center, Vector2f{0.0f, 0.0f}));
	EXPECT_NEAR(bs.Radius, 3.0f, 1.0e-5f); // halfHeight 2 + radius 1

	AABox2f box = cap.ToAABox();
	EXPECT_TRUE(Near(box.Min, Vector2f{-1.0f, -3.0f}));
	EXPECT_TRUE(Near(box.Max, Vector2f{1.0f, 3.0f}));
}

TEST(Capsule2f, ClosestPointsOnSegments)
{
	Vector2f outA, outB;
	float sa = -1.0f, sb = -1.0f;
	float d2 = ClosestPointsOnSegments(
		Vector2f{0.0f, 0.0f}, Vector2f{2.0f, 0.0f},
		Vector2f{1.0f, 1.0f}, Vector2f{1.0f, 3.0f},
		outA, outB, &sa, &sb);
	EXPECT_NEAR(d2, 1.0f, 1.0e-4f);
	EXPECT_TRUE(Near(outA, Vector2f{1.0f, 0.0f}));
	EXPECT_TRUE(Near(outB, Vector2f{1.0f, 1.0f}));
	EXPECT_NEAR(sa, 0.5f, 1.0e-4f);
	EXPECT_NEAR(sb, 0.0f, 1.0e-4f);
}

TEST(Capsule2f, DegenerateIsCircle)
{
	Capsule2f c{Vector2f{1.0f, 2.0f}, Vector2f{1.0f, 2.0f}, 2.0f};
	EXPECT_TRUE(Near(c.Axis(), Vector2f::ZERO));
	EXPECT_NEAR(c.SegmentLength(), 0.0f, 1.0e-6f);
	EXPECT_TRUE(c.Contains(Vector2f{1.0f, 2.0f}));
	EXPECT_FALSE(c.Contains(Vector2f{1.0f, 5.0f}));
	EXPECT_NEAR(c.SignedDistance(Vector2f{1.0f, 5.0f}), 1.0f, 1.0e-4f);
}
