#include "Frustum3f.h"
#include <cmath>

namespace Dark
{
	namespace Math
	{
		Frustum3f::Frustum3f()
		{
			for (auto& p : Planes)
				p = Plane4f(0.0f, 1.0f, 0.0f, 0.0f);
		}

		Frustum3f::Frustum3f(const Matrix4f& viewProjection, bool normalize)
		{
			Update(viewProjection, normalize);
		}

		void Frustum3f::Update(const Matrix4f& vp, bool normalize)
		{
			// Gribb/Hartmann extraction for row-major, row-vector matrices.
			// Left
			Planes[Left] = Plane4f(
				vp(0, 3) + vp(0, 0),
				vp(1, 3) + vp(1, 0),
				vp(2, 3) + vp(2, 0),
				vp(3, 3) + vp(3, 0));

			// Right
			Planes[Right] = Plane4f(
				vp(0, 3) - vp(0, 0),
				vp(1, 3) - vp(1, 0),
				vp(2, 3) - vp(2, 0),
				vp(3, 3) - vp(3, 0));

			// Top
			Planes[Top] = Plane4f(
				vp(0, 3) - vp(0, 1),
				vp(1, 3) - vp(1, 1),
				vp(2, 3) - vp(2, 1),
				vp(3, 3) - vp(3, 1));

			// Bottom
			Planes[Bottom] = Plane4f(
				vp(0, 3) + vp(0, 1),
				vp(1, 3) + vp(1, 1),
				vp(2, 3) + vp(2, 1),
				vp(3, 3) + vp(3, 1));

			// Near
			Planes[Near] = Plane4f(
				vp(0, 2),
				vp(1, 2),
				vp(2, 2),
				vp(3, 2));

			// Far
			Planes[Far] = Plane4f(
				vp(0, 3) - vp(0, 2),
				vp(1, 3) - vp(1, 2),
				vp(2, 3) - vp(2, 2),
				vp(3, 3) - vp(3, 2));

			if (normalize)
			{
				for (auto& plane : Planes)
					plane.Normalize();
			}
		}

		bool Frustum3f::Contains(const Vector3f& point) const
		{
			for (const auto& plane : Planes)
			{
				if (plane.DistanceToPoint(point) < 0.0f)
					return false;
			}
			return true;
		}

		bool Frustum3f::Intersects(const Sphere3f& sphere) const
		{
			for (const auto& plane : Planes)
			{
				if (plane.DistanceToPoint(sphere.Center) < -sphere.Radius)
					return false;
			}
			return true;
		}

		bool Frustum3f::Envelops(const Sphere3f& sphere) const
		{
			for (const auto& plane : Planes)
			{
				if (plane.DistanceToPoint(sphere.Center) < sphere.Radius)
					return false;
			}
			return true;
		}

		bool Frustum3f::Intersects(const Aabb3f& box) const
		{
			// Outside any plane ⇒ no intersection (p-vertex test).
			for (const auto& plane : Planes)
			{
				Vector3f p(
					plane.x >= 0.0f ? box.Max.x : box.Min.x,
					plane.y >= 0.0f ? box.Max.y : box.Min.y,
					plane.z >= 0.0f ? box.Max.z : box.Min.z);

				if (plane.DistanceToPoint(p) < 0.0f)
					return false;
			}
			return true;
		}

		bool Frustum3f::Intersects(const Box3f& box) const
		{
			// Project OBB onto each plane normal; if fully on negative side, culled.
			for (const auto& plane : Planes)
			{
				Vector3f n = plane.Normal();
				float r = box.Extent[0] * fabsf(n.Dot(box.Axis[0]))
				        + box.Extent[1] * fabsf(n.Dot(box.Axis[1]))
				        + box.Extent[2] * fabsf(n.Dot(box.Axis[2]));

				float s = plane.DistanceToPoint(box.Center);
				if (s < -r)
					return false;
			}
			return true;
		}
	}
}
