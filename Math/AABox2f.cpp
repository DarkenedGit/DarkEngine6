#include "AABox2f.h"
#include "MathDefines.h"
#include <algorithm>
#include <cmath>

namespace Dark::Math
{
	AABox2f::AABox2f(): Min(Vector2f::ZERO), Max(Vector2f::ZERO)
	{
	}

	AABox2f::AABox2f(const Vector2f& min, const Vector2f& max): Min(min), Max(max)
	{
	}

	AABox2f AABox2f::FromCenterExtents(const Vector2f& center, const Vector2f& halfExtents)
	{
		return AABox2f(center - halfExtents, center + halfExtents);
	}

	AABox2f AABox2f::FromPoints(const Vector2f* points, int count)
	{
		AABox2f box = Empty();
		for (int i = 0; i < count; ++i)
			box.ExpandToInclude(points[i]);
		return box;
	}

	AABox2f AABox2f::Empty()
	{
		return AABox2f(Vector2f(Infinity, Infinity),Vector2f(NegInfinity, NegInfinity));
	}

	Vector2f AABox2f::Center() const
	{
		return (Min + Max) * 0.5f;
	}

	Vector2f AABox2f::Extents() const
	{
		return (Max - Min) * 0.5f;
	}

	Vector2f AABox2f::Size() const
	{
		return Max - Min;
	}

	float AABox2f::Perimeter() const
	{
		Vector2f s = Size();
		return 2.0f * (s.x + s.y);
	}

	float AABox2f::Area() const
	{
		Vector2f s = Size();
		return s.x * s.y;
	}

	bool AABox2f::IsValid() const
	{
		return Min.x <= Max.x && Min.y <= Max.y;
	}

	bool AABox2f::Contains(const Vector2f& point) const
	{
		return point.x >= Min.x && point.x <= Max.x && point.y >= Min.y && point.y <= Max.y;
	}

	bool AABox2f::Contains(const AABox2f& other) const
	{
		return other.Min.x >= Min.x && other.Max.x <= Max.x && other.Min.y >= Min.y && other.Max.y <= Max.y;
	}

	bool AABox2f::Intersects(const AABox2f& other) const
	{
		return Min.x <= other.Max.x && Max.x >= other.Min.x && Min.y <= other.Max.y && Max.y >= other.Min.y;
	}

	bool AABox2f::Intersects(const Sphere2f& circle) const
	{
		float cx = std::max(Min.x, std::min(circle.Center.x, Max.x));
		float cy = std::max(Min.y, std::min(circle.Center.y, Max.y));

		float dx = cx - circle.Center.x;
		float dy = cy - circle.Center.y;
		return (dx * dx + dy * dy) <= circle.Radius * circle.Radius;
	}

	void AABox2f::Expand(float amount)
	{
		Min.x -= amount;
		Min.y -= amount;
		Max.x += amount;
		Max.y += amount;
	}

	void AABox2f::ExpandToInclude(const Vector2f& point)
	{
		if (point.x < Min.x) Min.x = point.x;
		if (point.y < Min.y) Min.y = point.y;
		if (point.x > Max.x) Max.x = point.x;
		if (point.y > Max.y) Max.y = point.y;
	}

	void AABox2f::ExpandToInclude(const AABox2f& other)
	{
		ExpandToInclude(other.Min);
		ExpandToInclude(other.Max);
	}

	void AABox2f::ExpandToInclude(const Sphere2f& circle)
	{
		Vector2f r(circle.Radius, circle.Radius);
		ExpandToInclude(circle.Center - r);
		ExpandToInclude(circle.Center + r);
	}

	void AABox2f::GetCorners(Vector2f outCorners[4]) const
	{
		outCorners[0] = Vector2f(Min.x, Min.y);
		outCorners[1] = Vector2f(Max.x, Min.y);
		outCorners[2] = Vector2f(Min.x, Max.y);
		outCorners[3] = Vector2f(Max.x, Max.y);
	}

	Sphere2f AABox2f::ToBoundingCircle() const
	{
		Vector2f c = Center();
		Vector2f e = Extents();
		return Sphere2f(c, e.Magnitude());
	}
}
