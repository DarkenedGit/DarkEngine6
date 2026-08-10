#pragma once

#include "Vector2f.h"
#include "Sphere2f.h"

namespace Dark
{
	namespace Math
	{
		// Axis-aligned box in 2D (min/max corners).
		class Aabb2f
		{
		public:
			Vector2f Min;
			Vector2f Max;

			Aabb2f();
			Aabb2f(const Vector2f& min, const Vector2f& max);

			static Aabb2f FromCenterExtents(const Vector2f& center, const Vector2f& halfExtents);
			static Aabb2f FromPoints(const Vector2f* points, int count);
			static Aabb2f Empty(); // inverted empty box for progressive expansion

			Vector2f Center() const;
			Vector2f Extents() const; // half-size
			Vector2f Size() const;    // full size
			float    Perimeter() const;
			float    Area() const;

			bool IsValid() const; // Min <= Max on both axes

			bool Contains(const Vector2f& point) const;
			bool Contains(const Aabb2f& other) const;
			bool Intersects(const Aabb2f& other) const;
			bool Intersects(const Sphere2f& circle) const;

			void Expand(float amount);
			void ExpandToInclude(const Vector2f& point);
			void ExpandToInclude(const Aabb2f& other);
			void ExpandToInclude(const Sphere2f& circle);

			void GetCorners(Vector2f outCorners[4]) const;
			Sphere2f ToBoundingCircle() const;
		};

		// Historical alias used by some includes.
		using AABox2f = Aabb2f;
	}
}
