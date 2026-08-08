#pragma once

#include "Vector3f.h"

namespace Dark
{
	namespace Math
	{
		// Sphere with center and radius — used for intersection and culling.
		class Sphere3f
		{
		public:
			Vector3f Center;
			float    Radius;

			Sphere3f();
			Sphere3f(const Vector3f& center, float radius);

			void Update(const Vector3f& center, float radius);
			void UpdateCenter(const Vector3f& center);
			void UpdateRadius(float radius);

			bool Intersects(const Sphere3f& test) const;
			bool Envelops(const Sphere3f& test) const;
			bool Contains(const Vector3f& point) const;

			// Expand so this sphere contains the given point / sphere.
			void ExpandToInclude(const Vector3f& point);
			void ExpandToInclude(const Sphere3f& other);

			void SamplePosition(Vector3f& position, float theta, float phi) const;
			void SampleNormal(Vector3f& normal, float theta, float phi) const;
			void SamplePositionAndNormal(Vector3f& position, Vector3f& normal, float theta, float phi) const;
		};
	}
}
