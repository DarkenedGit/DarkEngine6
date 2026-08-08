#include "Box3f.h"

namespace Dark
{
	namespace Math
	{
		Box3f::Box3f()
			: Center(Vector3f::ZERO)
		{
			Axis[0] = Vector3f::X_AXIS;
			Axis[1] = Vector3f::Y_AXIS;
			Axis[2] = Vector3f::Z_AXIS;
			Extent[0] = Extent[1] = Extent[2] = 0.0f;
		}

		Box3f::Box3f(const Vector3f& center,
		             const Vector3f& axis0, const Vector3f& axis1, const Vector3f& axis2,
		             float extent0, float extent1, float extent2)
			: Center(center)
		{
			Axis[0] = axis0;
			Axis[1] = axis1;
			Axis[2] = axis2;
			Extent[0] = extent0;
			Extent[1] = extent1;
			Extent[2] = extent2;
		}

		Box3f Box3f::FromAabb(const Aabb3f& aabb)
		{
			return FromCenterExtents(aabb.Center(), aabb.Extents());
		}

		Box3f Box3f::FromCenterExtents(const Vector3f& center, const Vector3f& halfExtents)
		{
			return Box3f(center,
			             Vector3f::X_AXIS, Vector3f::Y_AXIS, Vector3f::Z_AXIS,
			             halfExtents.x, halfExtents.y, halfExtents.z);
		}

		bool Box3f::Contains(const Vector3f& point) const
		{
			Vector3f d = point - Center;
			for (int i = 0; i < 3; ++i)
			{
				float proj = d.Dot(Axis[i]);
				if (proj < -Extent[i] || proj > Extent[i])
					return false;
			}
			return true;
		}

		void Box3f::GetCorners(Vector3f outCorners[8]) const
		{
			Vector3f ex = Axis[0] * Extent[0];
			Vector3f ey = Axis[1] * Extent[1];
			Vector3f ez = Axis[2] * Extent[2];

			outCorners[0] = Center - ex - ey - ez;
			outCorners[1] = Center + ex - ey - ez;
			outCorners[2] = Center - ex + ey - ez;
			outCorners[3] = Center + ex + ey - ez;
			outCorners[4] = Center - ex - ey + ez;
			outCorners[5] = Center + ex - ey + ez;
			outCorners[6] = Center - ex + ey + ez;
			outCorners[7] = Center + ex + ey + ez;
		}

		Aabb3f Box3f::ToAabb() const
		{
			Vector3f corners[8];
			GetCorners(corners);
			return Aabb3f::FromPoints(corners, 8);
		}
	}
}
