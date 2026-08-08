#pragma once

#include "Vector3f.h"
#include "Aabb3f.h"
#include "Sphere3f.h"

namespace Dark
{
	namespace Math
	{
		class Ray3f
		{
		public:
			Vector3f Origin;
			Vector3f Direction; // should be unit length for distance queries

			Ray3f();
			Ray3f(const Vector3f& origin, const Vector3f& direction);

			Vector3f PointAt(float t) const;

			// Returns true and sets t (distance along ray) on hit. t >= 0.
			bool IntersectSphere(const Sphere3f& sphere, float& t) const;
			bool IntersectAabb(const Aabb3f& box, float& tMin, float& tMax) const;
		};
	}
}
