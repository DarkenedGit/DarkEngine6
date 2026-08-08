#include "Aabb3f.h"
#include "Matrix4f.h"
#include "MathDefines.h"
#include <algorithm>
#include <cmath>

namespace Dark
{
	namespace Math
	{
		Aabb3f::Aabb3f()
			: Min(Vector3f::ZERO)
			, Max(Vector3f::ZERO)
		{
		}

		Aabb3f::Aabb3f(const Vector3f& min, const Vector3f& max)
			: Min(min)
			, Max(max)
		{
		}

		Aabb3f Aabb3f::FromCenterExtents(const Vector3f& center, const Vector3f& halfExtents)
		{
			return Aabb3f(center - halfExtents, center + halfExtents);
		}

		Aabb3f Aabb3f::FromPoints(const Vector3f* points, int count)
		{
			Aabb3f box = Empty();
			for (int i = 0; i < count; ++i)
				box.ExpandToInclude(points[i]);
			return box;
		}

		Aabb3f Aabb3f::Empty()
		{
			return Aabb3f(
				Vector3f(Infinity, Infinity, Infinity),
				Vector3f(NegInfinity, NegInfinity, NegInfinity));
		}

		Vector3f Aabb3f::Center() const
		{
			return (Min + Max) * 0.5f;
		}

		Vector3f Aabb3f::Extents() const
		{
			return (Max - Min) * 0.5f;
		}

		Vector3f Aabb3f::Size() const
		{
			return Max - Min;
		}

		float Aabb3f::SurfaceArea() const
		{
			Vector3f s = Size();
			return 2.0f * (s.x * s.y + s.y * s.z + s.z * s.x);
		}

		float Aabb3f::Volume() const
		{
			Vector3f s = Size();
			return s.x * s.y * s.z;
		}

		bool Aabb3f::IsValid() const
		{
			return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z;
		}

		bool Aabb3f::Contains(const Vector3f& point) const
		{
			return point.x >= Min.x && point.x <= Max.x
				&& point.y >= Min.y && point.y <= Max.y
				&& point.z >= Min.z && point.z <= Max.z;
		}

		bool Aabb3f::Contains(const Aabb3f& other) const
		{
			return other.Min.x >= Min.x && other.Max.x <= Max.x
				&& other.Min.y >= Min.y && other.Max.y <= Max.y
				&& other.Min.z >= Min.z && other.Max.z <= Max.z;
		}

		bool Aabb3f::Intersects(const Aabb3f& other) const
		{
			return Min.x <= other.Max.x && Max.x >= other.Min.x
				&& Min.y <= other.Max.y && Max.y >= other.Min.y
				&& Min.z <= other.Max.z && Max.z >= other.Min.z;
		}

		bool Aabb3f::Intersects(const Sphere3f& sphere) const
		{
			// Closest point on AABB to sphere center
			float cx = std::max(Min.x, std::min(sphere.Center.x, Max.x));
			float cy = std::max(Min.y, std::min(sphere.Center.y, Max.y));
			float cz = std::max(Min.z, std::min(sphere.Center.z, Max.z));

			float dx = cx - sphere.Center.x;
			float dy = cy - sphere.Center.y;
			float dz = cz - sphere.Center.z;
			return (dx * dx + dy * dy + dz * dz) <= sphere.Radius * sphere.Radius;
		}

		void Aabb3f::Expand(float amount)
		{
			Min.x -= amount;
			Min.y -= amount;
			Min.z -= amount;
			Max.x += amount;
			Max.y += amount;
			Max.z += amount;
		}

		void Aabb3f::ExpandToInclude(const Vector3f& point)
		{
			if (point.x < Min.x) Min.x = point.x;
			if (point.y < Min.y) Min.y = point.y;
			if (point.z < Min.z) Min.z = point.z;
			if (point.x > Max.x) Max.x = point.x;
			if (point.y > Max.y) Max.y = point.y;
			if (point.z > Max.z) Max.z = point.z;
		}

		void Aabb3f::ExpandToInclude(const Aabb3f& other)
		{
			ExpandToInclude(other.Min);
			ExpandToInclude(other.Max);
		}

		void Aabb3f::ExpandToInclude(const Sphere3f& sphere)
		{
			Vector3f r(sphere.Radius, sphere.Radius, sphere.Radius);
			ExpandToInclude(sphere.Center - r);
			ExpandToInclude(sphere.Center + r);
		}

		void Aabb3f::GetCorners(Vector3f outCorners[8]) const
		{
			outCorners[0] = Vector3f(Min.x, Min.y, Min.z);
			outCorners[1] = Vector3f(Max.x, Min.y, Min.z);
			outCorners[2] = Vector3f(Min.x, Max.y, Min.z);
			outCorners[3] = Vector3f(Max.x, Max.y, Min.z);
			outCorners[4] = Vector3f(Min.x, Min.y, Max.z);
			outCorners[5] = Vector3f(Max.x, Min.y, Max.z);
			outCorners[6] = Vector3f(Min.x, Max.y, Max.z);
			outCorners[7] = Vector3f(Max.x, Max.y, Max.z);
		}

		Sphere3f Aabb3f::ToBoundingSphere() const
		{
			Vector3f c = Center();
			Vector3f e = Extents();
			return Sphere3f(c, e.Magnitude());
		}

		Aabb3f Aabb3f::Transformed(const Matrix4f& m) const
		{
			Vector3f corners[8];
			GetCorners(corners);

			Aabb3f result = Empty();
			for (int i = 0; i < 8; ++i)
			{
				Vector4f p = m * Vector4f(corners[i], 1.0f);
				result.ExpandToInclude(p.xyz());
			}
			return result;
		}
	}
}
