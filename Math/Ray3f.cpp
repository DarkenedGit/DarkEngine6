#include "Ray3f.h"
#include "MathDefines.h"
#include <cmath>
#include <algorithm>

namespace Dark
{
	namespace Math
	{
		Ray3f::Ray3f()
			: Origin(Vector3f::ZERO)
			, Direction(Vector3f::Z_AXIS)
		{
		}

		Ray3f::Ray3f(const Vector3f& origin, const Vector3f& direction)
			: Origin(origin)
			, Direction(direction)
		{
		}

		Vector3f Ray3f::PointAt(float t) const
		{
			return Origin + Direction * t;
		}

		bool Ray3f::IntersectSphere(const Sphere3f& sphere, float& t) const
		{
			// Solve |O + tD - C|^2 = R^2
			Vector3f m = Origin - sphere.Center;
			float b = m.Dot(Direction);
			float c = m.Dot(m) - sphere.Radius * sphere.Radius;

			// Ray origin outside and pointing away
			if (c > 0.0f && b > 0.0f)
				return false;

			float discr = b * b - c;
			if (discr < 0.0f)
				return false;

			t = -b - sqrtf(discr);
			if (t < 0.0f)
				t = 0.0f; // inside sphere: clamp to origin
			return true;
		}

		bool Ray3f::IntersectAabb(const Aabb3f& box, float& tMin, float& tMax) const
		{
			// Slab method
			tMin = 0.0f;
			tMax = Infinity;

			for (int i = 0; i < 3; ++i)
			{
				float o = Origin[i];
				float d = Direction[i];
				float minB = box.Min[i];
				float maxB = box.Max[i];

				if (fabsf(d) < Epsilon)
				{
					if (o < minB || o > maxB)
						return false;
				}
				else
				{
					float invD = 1.0f / d;
					float t1 = (minB - o) * invD;
					float t2 = (maxB - o) * invD;
					if (t1 > t2)
						std::swap(t1, t2);

					tMin = std::max(tMin, t1);
					tMax = std::min(tMax, t2);
					if (tMin > tMax)
						return false;
				}
			}
			return true;
		}
	}
}
