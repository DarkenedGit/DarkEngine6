#pragma once

#include "Vector3f.h"
#include "Sphere3f.h"

namespace Dark
{
	namespace Math
	{
		// Axis-aligned bounding box (min/max corners).
		class Aabb3f
		{
		public:
			Vector3f Min;
			Vector3f Max;

			Aabb3f();
			Aabb3f(const Vector3f& min, const Vector3f& max);

			static Aabb3f FromCenterExtents(const Vector3f& center, const Vector3f& halfExtents);
			static Aabb3f FromPoints(const Vector3f* points, int count);
			static Aabb3f Empty(); // inverted empty box for progressive expansion

			Vector3f Center() const;
			Vector3f Extents() const;     // half-size
			Vector3f Size() const;        // full size
			float    SurfaceArea() const;
			float    Volume() const;

			bool IsValid() const; // Min <= Max on all axes

			bool Contains(const Vector3f& point) const;
			bool Contains(const Aabb3f& other) const;
			bool Intersects(const Aabb3f& other) const;
			bool Intersects(const Sphere3f& sphere) const;

			void Expand(float amount);
			void ExpandToInclude(const Vector3f& point);
			void ExpandToInclude(const Aabb3f& other);
			void ExpandToInclude(const Sphere3f& sphere);

			void GetCorners(Vector3f outCorners[8]) const;
			Sphere3f ToBoundingSphere() const;

			// Transform AABB by matrix (recomputes AABB of transformed corners).
			Aabb3f Transformed(const class Matrix4f& m) const;
		};
	}
}
