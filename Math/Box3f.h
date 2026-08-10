#pragma once

#include <array>
#include "Vector3f.h"
#include "AABox3f.h"

namespace Dark
{
	namespace Math
	{
		// Oriented bounding box (center + orthonormal axes + half-extents).
		// Axis[0] = forward/local-X, Axis[1] = up/local-Y, Axis[2] = right/local-Z
		// Extent[i] = half-length along Axis[i].
		class Box3f
		{
		public:
			Vector3f Center;
			std::array<Vector3f, 3> Axis;
			std::array<float, 3>    Extent;

			Box3f();
			Box3f(const Vector3f& center,
			      const Vector3f& axis0, const Vector3f& axis1, const Vector3f& axis2,
			      float extent0, float extent1, float extent2);

			// Axis-aligned box from AABB.
			static Box3f FromAabb(const Aabb3f& aabb);

			// Axis-aligned box from center + half extents (world axes).
			static Box3f FromCenterExtents(const Vector3f& center, const Vector3f& halfExtents);

			bool Contains(const Vector3f& point) const;
			void GetCorners(Vector3f outCorners[8]) const;
			Aabb3f ToAabb() const;
		};
	}
}
