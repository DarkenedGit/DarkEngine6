#pragma once

#include "Vector2f.h"

namespace Dark
{
	namespace Math
	{
		// 2D circle (named Sphere2f to mirror Sphere3f).
		class Sphere2f
		{
		public:
			Vector2f Center;
			float    Radius;

			Sphere2f();
			Sphere2f(const Vector2f& center, float radius);

			void Update(const Vector2f& center, float radius);
			void UpdateCenter(const Vector2f& center);
			void UpdateRadius(float radius);

			bool Intersects(const Sphere2f& test) const;
			bool Envelops(const Sphere2f& test) const;
			bool Contains(const Vector2f& point) const;

			void ExpandToInclude(const Vector2f& point);
			void ExpandToInclude(const Sphere2f& other);

			// Sample on the circumference; theta in radians.
			void SamplePosition(Vector2f& position, float theta) const;
			void SampleNormal(Vector2f& normal, float theta) const;
			void SamplePositionAndNormal(Vector2f& position, Vector2f& normal, float theta) const;
		};

		// Alias for clarity when "circle" is preferred in 2D code.
		using Circle2f = Sphere2f;
	}
}
