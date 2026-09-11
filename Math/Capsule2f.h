#pragma once

#include "Vector2f.h"
#include "Sphere2f.h"
#include "AABox2f.h"

namespace Dark::Math
{
	// Capsule2 = line segment (PointA, PointB) thickened by Radius (semicircular caps).
	// Degenerate A==B reduces to a circle of the same radius.
	class Capsule2f
	{
	public:
		Vector2f PointA;
		Vector2f PointB;
		float    Radius;

		Capsule2f();
		Capsule2f(const Vector2f& a, const Vector2f& b, float radius);
		Capsule2f(const Capsule2f&) noexcept = default;
		Capsule2f(Capsule2f&&) noexcept = default;
		Capsule2f& operator=(const Capsule2f&) noexcept = default;
		Capsule2f& operator=(Capsule2f&&) noexcept = default;

		void Update(const Vector2f& a, const Vector2f& b, float radius);
		void UpdateRadius(float radius);

		// Center of the medial segment; unit axis from A toward B (ZERO if degenerate).
		Vector2f Center() const;
		Vector2f Axis() const;
		float    SegmentLength() const;
		// Rectangle half-length (distance from Center to either endpoint).
		float    HalfHeight() const;
		// Total axial span including semicircles: SegmentLength() + 2*Radius.
		float    Height() const;

		// Build from center, unit axis, rectangle half-height, and radius.
		static Capsule2f FromCenterAxis(const Vector2f& center, const Vector2f& unitAxis, float halfHeight, float radius);

		bool Contains(const Vector2f& point) const;

		// Closest point on the medial segment (not the shell).
		Vector2f ClosestPointOnSegment(const Vector2f& point) const;
		// Closest point on the capsule surface (or the point itself if inside).
		Vector2f ClosestPoint(const Vector2f& point) const;

		// Signed distance to the shell: <0 inside, 0 on surface, >0 outside.
		float SignedDistance(const Vector2f& point) const;
		// Unsigned distance to the shell (0 if inside or on surface).
		float Distance(const Vector2f& point) const;
		float DistanceSq(const Vector2f& point) const;

		float Distance(const Sphere2f& circle) const;
		float Distance(const Capsule2f& other) const;

		bool Intersects(const Capsule2f& other) const;
		bool Intersects(const Sphere2f& circle) const;

		Sphere2f ToBoundingCircle() const;
		AABox2f  ToAABox() const;
	};

	// Alias for clarity when "stadium" is preferred in 2D code.
	using Stadium2f = Capsule2f;

	// Closest points between two finite segments. Returns squared distance between them.
	// sa/sb in [0,1] are parameters along A0→A1 and B0→B1.
	float ClosestPointsOnSegments(
		const Vector2f& a0, const Vector2f& a1,
		const Vector2f& b0, const Vector2f& b1,
		Vector2f& outA, Vector2f& outB,
		float* sa = nullptr, float* sb = nullptr);
}
