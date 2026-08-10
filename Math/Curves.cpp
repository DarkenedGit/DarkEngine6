#include "Curves.h"

namespace Dark
{
	namespace Math
	{
		float BezierCubicLength(const Vector3f& p0, const Vector3f& p1, const Vector3f& p2, const Vector3f& p3, unsigned int segments)
		{
			if (segments < 1)
				segments = 1;

			float length = 0.0f;
			Vector3f prev = p0;
			for (unsigned int i = 1; i <= segments; ++i)
			{
				float t = static_cast<float>(i) / static_cast<float>(segments);
				Vector3f cur = BezierCubic(p0, p1, p2, p3, t);
				length += (cur - prev).Magnitude();
				prev = cur;
			}
			return length;
		}
	}
}
