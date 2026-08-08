#include "Aabb2f.h"
#include "MathDefines.h"
#include <algorithm>
#include <cmath>

namespace Dark
{
	namespace Math
	{
		Aabb2f::Aabb2f(): Min(Vector2f::ZERO), Max(Vector2f::ZERO)
		{
		}

		Aabb2f::Aabb2f(const Vector2f& min, const Vector2f& max): Min(min), Max(max)
		{
		}

		Aabb2f Aabb2f::FromCenterExtents(const Vector2f& center, const Vector2f& halfExtents)
		{
			return Aabb2f(center - halfExtents, center + halfExtents);
		}

		Aabb2f Aabb2f::FromPoints(const Vector2f* points, int count)
		{
			Aabb2f box = Empty();
			for (int i = 0; i < count; ++i)
				box.ExpandToInclude(points[i]);
			return box;
		}

		Aabb2f Aabb2f::Empty()
		{
			return Aabb2f(Vector2f(Infinity, Infinity),Vector2f(NegInfinity, NegInfinity));
		}

		Vector2f Aabb2f::Center() const
		{
			return (Min + Max) * 0.5f;
		}

		Vector2f Aabb2f::Extents() const
		{
			return (Max - Min) * 0.5f;
		}

		Vector2f Aabb2f::Size() const
		{
			return Max - Min;
		}

		float Aabb2f::Perimeter() const
		{
			Vector2f s = Size();
			return 2.0f * (s.x + s.y);
		}

		float Aabb2f::Area() const
		{
			Vector2f s = Size();
			return s.x * s.y;
		}

		bool Aabb2f::IsValid() const
		{
			return Min.x <= Max.x && Min.y <= Max.y;
		}

		bool Aabb2f::Contains(const Vector2f& point) const
		{
			return point.x >= Min.x && point.x <= Max.x && point.y >= Min.y && point.y <= Max.y;
		}

		bool Aabb2f::Contains(const Aabb2f& other) const
		{
			return other.Min.x >= Min.x && other.Max.x <= Max.x && other.Min.y >= Min.y && other.Max.y <= Max.y;
		}

		bool Aabb2f::Intersects(const Aabb2f& other) const
		{
			return Min.x <= other.Max.x && Max.x >= other.Min.x && Min.y <= other.Max.y && Max.y >= other.Min.y;
		}

		bool Aabb2f::Intersects(const Sphere2f& circle) const
		{
			float cx = std::max(Min.x, std::min(circle.Center.x, Max.x));
			float cy = std::max(Min.y, std::min(circle.Center.y, Max.y));

			float dx = cx - circle.Center.x;
			float dy = cy - circle.Center.y;
			return (dx * dx + dy * dy) <= circle.Radius * circle.Radius;
		}

		void Aabb2f::Expand(float amount)
		{
			Min.x -= amount;
			Min.y -= amount;
			Max.x += amount;
			Max.y += amount;
		}

		void Aabb2f::ExpandToInclude(const Vector2f& point)
		{
			if (point.x < Min.x) Min.x = point.x;
			if (point.y < Min.y) Min.y = point.y;
			if (point.x > Max.x) Max.x = point.x;
			if (point.y > Max.y) Max.y = point.y;
		}

		void Aabb2f::ExpandToInclude(const Aabb2f& other)
		{
			ExpandToInclude(other.Min);
			ExpandToInclude(other.Max);
		}

		void Aabb2f::ExpandToInclude(const Sphere2f& circle)
		{
			Vector2f r(circle.Radius, circle.Radius);
			ExpandToInclude(circle.Center - r);
			ExpandToInclude(circle.Center + r);
		}

		void Aabb2f::GetCorners(Vector2f outCorners[4]) const
		{
			outCorners[0] = Vector2f(Min.x, Min.y);
			outCorners[1] = Vector2f(Max.x, Min.y);
			outCorners[2] = Vector2f(Min.x, Max.y);
			outCorners[3] = Vector2f(Max.x, Max.y);
		}

		Sphere2f Aabb2f::ToBoundingCircle() const
		{
			Vector2f c = Center();
			Vector2f e = Extents();
			return Sphere2f(c, e.Magnitude());
		}
	}
}
