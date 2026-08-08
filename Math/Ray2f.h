#pragma once

#include "Vector2f.h"
#include "Aabb2f.h"
#include "Sphere2f.h"

namespace Dark
{
	namespace Math
	{
		class Ray2f
		{
		public:
			Vector2f Origin;
			Vector2f Direction; // should be unit length for distance queries

			Ray2f();
			Ray2f(const Vector2f& origin, const Vector2f& direction);

			Vector2f PointAt(float t) const;

			// Returns true and sets t (distance along ray) on hit. t >= 0.
			bool IntersectCircle(const Sphere2f& circle, float& t) const;
			bool IntersectAabb(const Aabb2f& box, float& tMin, float& tMax) const;
		};
	}
}
