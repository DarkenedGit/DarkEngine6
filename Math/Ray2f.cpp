#include "Ray2f.h"
#include "MathDefines.h"
#include <cmath>
#include <algorithm>

namespace Dark
{
	namespace Math
	{
		Ray2f::Ray2f()
			: Origin(Vector2f::ZERO)
			, Direction(Vector2f::X_AXIS)
		{
		}

		Ray2f::Ray2f(const Vector2f& origin, const Vector2f& direction)
			: Origin(origin)
			, Direction(direction)
		{
		}

		Vector2f Ray2f::PointAt(float t) const
		{
			return Origin + Direction * t;
		}

		bool Ray2f::IntersectCircle(const Sphere2f& circle, float& t) const
		{
			Vector2f m = Origin - circle.Center;
			float b = m.Dot(Direction);
			float c = m.Dot(m) - circle.Radius * circle.Radius;

			if (c > 0.0f && b > 0.0f)
				return false;

			float discr = b * b - c;
			if (discr < 0.0f)
				return false;

			t = -b - sqrtf(discr);
			if (t < 0.0f)
				t = 0.0f;
			return true;
		}

		bool Ray2f::IntersectAabb(const Aabb2f& box, float& tMin, float& tMax) const
		{
			tMin = 0.0f;
			tMax = Infinity;

			for (int i = 0; i < 2; ++i)
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
