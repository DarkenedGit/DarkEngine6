#pragma once

#include "Vector3f.h"
#include "Sphere3f.h"
#include "AABox3f.h"

namespace Dark::Math
{
	// Capsule = line segment (PointA, PointB) thickened by Radius (hemispherical caps).
	// Degenerate A==B reduces to a sphere of the same radius.
	class Capsule3f
	{
	public:
		Vector3f PointA;
		Vector3f PointB;
		float    Radius;

		Capsule3f();
		Capsule3f(const Vector3f& a, const Vector3f& b, float radius);
		Capsule3f(const Capsule3f&) noexcept = default;
		Capsule3f(Capsule3f&&) noexcept = default;
		Capsule3f& operator=(const Capsule3f&) noexcept = default;
		Capsule3f& operator=(Capsule3f&&) noexcept = default;

		void Update(const Vector3f& a, const Vector3f& b, float radius);
		void UpdateRadius(float radius);

		// Center of the medial segment; unit axis from A toward B (ZERO if degenerate).
		Vector3f Center() const;
		Vector3f Axis() const;
		float    SegmentLength() const;
		// Cylinder half-length (distance from Center to either endpoint).
		float    HalfHeight() const;
		// Total axial span including hemispheres: SegmentLength() + 2*Radius.
		float    Height() const;

		// Build from center, unit axis, cylinder half-height, and radius.
		static Capsule3f FromCenterAxis(const Vector3f& center, const Vector3f& unitAxis, float halfHeight, float radius);

		bool Contains(const Vector3f& point) const;

		// Closest point on the medial segment (not the shell).
		Vector3f ClosestPointOnSegment(const Vector3f& point) const;
		// Closest point on the capsule surface (or the point itself if inside).
		Vector3f ClosestPoint(const Vector3f& point) const;

		// Signed distance to the shell: <0 inside, 0 on surface, >0 outside.
		float SignedDistance(const Vector3f& point) const;
		// Unsigned distance to the shell (0 if inside or on surface).
		float Distance(const Vector3f& point) const;
		float DistanceSq(const Vector3f& point) const;

		float Distance(const Sphere3f& sphere) const;
		float Distance(const Capsule3f& other) const;

		bool Intersects(const Capsule3f& other) const;
		bool Intersects(const Sphere3f& sphere) const;

		Sphere3f ToBoundingSphere() const;
		AABox3f  ToAABox() const;
	};

	// Closest points between two finite segments. Returns squared distance between them.
	// sa/sb in [0,1] are parameters along A0→A1 and B0→B1.
	float ClosestPointsOnSegments(
		const Vector3f& a0, const Vector3f& a1,
		const Vector3f& b0, const Vector3f& b1,
		Vector3f& outA, Vector3f& outB,
		float* sa = nullptr, float* sb = nullptr);
}
