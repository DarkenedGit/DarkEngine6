#include "Box2f.h"
#include <cmath>

namespace Dark
{
	namespace Math
	{
		Box2f::Box2f(): Center(Vector2f::ZERO)
		{
			Axis[0] = Vector2f::X_AXIS;
			Axis[1] = Vector2f::Y_AXIS;
			Extent[0] = Extent[1] = 0.0f;
		}

		Box2f::Box2f(const Vector2f& center,
		             const Vector2f& axis0, const Vector2f& axis1,
		             float extent0, float extent1)
			: Center(center)
		{
			Axis[0] = axis0;
			Axis[1] = axis1;
			Extent[0] = extent0;
			Extent[1] = extent1;
		}

		Box2f::Box2f(const Vector2f& center, float rotationRadians,
		             float halfExtentX, float halfExtentY)
			: Center(center)
		{
			float c = cosf(rotationRadians);
			float s = sinf(rotationRadians);
			Axis[0] = Vector2f(c, s);
			Axis[1] = Vector2f(-s, c);
			Extent[0] = halfExtentX;
			Extent[1] = halfExtentY;
		}

		Box2f Box2f::FromAabb(const Aabb2f& aabb)
		{
			return FromCenterExtents(aabb.Center(), aabb.Extents());
		}

		Box2f Box2f::FromCenterExtents(const Vector2f& center, const Vector2f& halfExtents)
		{
			return Box2f(center,
			             Vector2f::X_AXIS, Vector2f::Y_AXIS,
			             halfExtents.x, halfExtents.y);
		}

		bool Box2f::Contains(const Vector2f& point) const
		{
			Vector2f d = point - Center;
			for (int i = 0; i < 2; ++i)
			{
				float proj = d.Dot(Axis[i]);
				if (proj < -Extent[i] || proj > Extent[i])
					return false;
			}
			return true;
		}

		void Box2f::GetCorners(Vector2f outCorners[4]) const
		{
			Vector2f ex = Axis[0] * Extent[0];
			Vector2f ey = Axis[1] * Extent[1];

			outCorners[0] = Center - ex - ey;
			outCorners[1] = Center + ex - ey;
			outCorners[2] = Center - ex + ey;
			outCorners[3] = Center + ex + ey;
		}

		Aabb2f Box2f::ToAabb() const
		{
			Vector2f corners[4];
			GetCorners(corners);
			return Aabb2f::FromPoints(corners, 4);
		}
	}
}
