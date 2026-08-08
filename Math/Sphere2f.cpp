#include "Sphere2f.h"
#include <cmath>

namespace Dark
{
	namespace Math
	{
		Sphere2f::Sphere2f()
			: Center(Vector2f::ZERO)
			, Radius(0.0f)
		{
		}

		Sphere2f::Sphere2f(const Vector2f& center, float radius)
		{
			Update(center, radius);
		}

		void Sphere2f::Update(const Vector2f& center, float radius)
		{
			Center = center;
			Radius = radius;
		}

		void Sphere2f::UpdateCenter(const Vector2f& center)
		{
			Center = center;
		}

		void Sphere2f::UpdateRadius(float radius)
		{
			Radius = radius;
		}

		bool Sphere2f::Intersects(const Sphere2f& test) const
		{
			Vector2f dist = Center - test.Center;
			float r = Radius + test.Radius;
			return dist.MagnitudeSqrd() <= r * r;
		}

		bool Sphere2f::Envelops(const Sphere2f& test) const
		{
			Vector2f dist = Center - test.Center;
			float r = Radius - test.Radius;
			if (r < 0.0f)
				return false;
			return dist.MagnitudeSqrd() <= r * r;
		}

		bool Sphere2f::Contains(const Vector2f& point) const
		{
			Vector2f dist = point - Center;
			return dist.MagnitudeSqrd() <= Radius * Radius;
		}

		void Sphere2f::ExpandToInclude(const Vector2f& point)
		{
			Vector2f d = point - Center;
			float distSq = d.MagnitudeSqrd();
			if (distSq > Radius * Radius)
				Radius = sqrtf(distSq);
		}

		void Sphere2f::ExpandToInclude(const Sphere2f& other)
		{
			Vector2f d = other.Center - Center;
			float needed = d.Magnitude() + other.Radius;
			if (needed > Radius)
				Radius = needed;
		}

		void Sphere2f::SamplePosition(Vector2f& position, float theta) const
		{
			position.x = Radius * cosf(theta) + Center.x;
			position.y = Radius * sinf(theta) + Center.y;
		}

		void Sphere2f::SampleNormal(Vector2f& normal, float theta) const
		{
			normal.x = cosf(theta);
			normal.y = sinf(theta);
			normal.Normalize();
		}

		void Sphere2f::SamplePositionAndNormal(Vector2f& position, Vector2f& normal, float theta) const
		{
			normal.x = cosf(theta);
			normal.y = sinf(theta);
			position = Center + normal * Radius;
		}
	}
}
