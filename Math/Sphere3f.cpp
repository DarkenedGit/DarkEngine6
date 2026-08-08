#include "Sphere3f.h"
#include <cmath>

namespace Dark
{
	namespace Math
	{
		Sphere3f::Sphere3f()
			: Center(Vector3f::ZERO)
			, Radius(0.0f)
		{
		}

		Sphere3f::Sphere3f(const Vector3f& center, float radius)
		{
			Update(center, radius);
		}

		void Sphere3f::Update(const Vector3f& center, float radius)
		{
			Center = center;
			Radius = radius;
		}

		void Sphere3f::UpdateCenter(const Vector3f& center)
		{
			Center = center;
		}

		void Sphere3f::UpdateRadius(float radius)
		{
			Radius = radius;
		}

		bool Sphere3f::Intersects(const Sphere3f& test) const
		{
			Vector3f dist = Center - test.Center;
			float r = Radius + test.Radius;
			return dist.MagnitudeSqrd() <= r * r;
		}

		bool Sphere3f::Envelops(const Sphere3f& test) const
		{
			Vector3f dist = Center - test.Center;
			float r = Radius - test.Radius;
			if (r < 0.0f)
				return false;
			return dist.MagnitudeSqrd() <= r * r;
		}

		bool Sphere3f::Contains(const Vector3f& point) const
		{
			Vector3f dist = point - Center;
			return dist.MagnitudeSqrd() <= Radius * Radius;
		}

		void Sphere3f::ExpandToInclude(const Vector3f& point)
		{
			Vector3f d = point - Center;
			float distSq = d.MagnitudeSqrd();
			if (distSq > Radius * Radius)
			{
				float dist = sqrtf(distSq);
				Radius = dist;
			}
		}

		void Sphere3f::ExpandToInclude(const Sphere3f& other)
		{
			Vector3f d = other.Center - Center;
			float dist = d.Magnitude();
			float needed = dist + other.Radius;
			if (needed > Radius)
				Radius = needed;
		}

		void Sphere3f::SamplePosition(Vector3f& position, float theta, float phi) const
		{
			position.x = Radius * sinf(phi) * cosf(theta) + Center.x;
			position.y = Radius * cosf(phi) + Center.y;
			position.z = Radius * sinf(phi) * sinf(theta) + Center.z;
		}

		void Sphere3f::SampleNormal(Vector3f& normal, float theta, float phi) const
		{
			normal.x = sinf(phi) * cosf(theta);
			normal.y = cosf(phi);
			normal.z = sinf(phi) * sinf(theta);
			normal.Normalize();
		}

		void Sphere3f::SamplePositionAndNormal(Vector3f& position, Vector3f& normal, float theta, float phi) const
		{
			normal.x = Radius * sinf(phi) * cosf(theta);
			normal.y = Radius * cosf(phi);
			normal.z = Radius * sinf(phi) * sinf(theta);

			position = normal + Center;
			normal.Normalize();
		}
	}
}
