#pragma once

#include "Vector3f.h"
#include "Sphere3f.h"

namespace Dark::Math
{
	// Axis-aligned bounding box (min/max corners).
	class AABox3f
	{
	public:
		Vector3f Min;
		Vector3f Max;

		AABox3f();
		AABox3f(const Vector3f& min, const Vector3f& max);

		static AABox3f FromCenterExtents(const Vector3f& center, const Vector3f& halfExtents);
		static AABox3f FromPoints(const Vector3f* points, int count);
		static AABox3f Empty(); // inverted empty box for progressive expansion

		Vector3f Center() const;
		Vector3f Extents() const;     // half-size
		Vector3f Size() const;        // full size
		float    SurfaceArea() const;
		float    Volume() const;

		bool IsValid() const; // Min <= Max on all axes

		bool Contains(const Vector3f& point) const;
		bool Contains(const AABox3f& other) const;
		bool Intersects(const AABox3f& other) const;
		bool Intersects(const Sphere3f& sphere) const;

		void Expand(float amount);
		void ExpandToInclude(const Vector3f& point);
		void ExpandToInclude(const AABox3f& other);
		void ExpandToInclude(const Sphere3f& sphere);

		void GetCorners(Vector3f outCorners[8]) const;
		Sphere3f ToBoundingSphere() const;

		// Transform AABB by matrix (recomputes AABB of transformed corners).
		AABox3f Transformed(const class Matrix4f& m) const;
	};
} // Dark::Math
