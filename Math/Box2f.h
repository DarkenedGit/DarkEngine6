#pragma once

#include <array>
#include "Vector2f.h"
#include "Aabb2f.h"

namespace Dark
{
	namespace Math
	{
		// Oriented bounding box in 2D (center + orthonormal axes + half-extents).
		// Axis[0] = local-X, Axis[1] = local-Y.
		// Extent[i] = half-length along Axis[i].
		class Box2f
		{
		public:
			Vector2f Center;
			std::array<Vector2f, 2> Axis;
			std::array<float, 2>    Extent;

			Box2f();
			Box2f(const Vector2f& center,
			      const Vector2f& axis0, const Vector2f& axis1,
			      float extent0, float extent1);

			// Rotation angle (radians) around center; axes = (cos,sin) and (-sin,cos).
			Box2f(const Vector2f& center, float rotationRadians,
			      float halfExtentX, float halfExtentY);

			static Box2f FromAabb(const Aabb2f& aabb);
			static Box2f FromCenterExtents(const Vector2f& center, const Vector2f& halfExtents);

			bool Contains(const Vector2f& point) const;
			void GetCorners(Vector2f outCorners[4]) const;
			Aabb2f ToAabb() const;
		};
	}
}
