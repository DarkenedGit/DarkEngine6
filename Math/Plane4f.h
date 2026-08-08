#pragma once

#include "Vector3f.h"
#include "Vector4f.h"

namespace Dark
{
	namespace Math
	{
		// Plane stored as (nx, ny, nz, d) where nx*x + ny*y + nz*z + d = 0.
		// (Positive side is where the normal points.)
		class Plane4f final : public Vect4f
		{
		public:
			Plane4f();
			Plane4f(float nx, float ny, float nz, float d);
			Plane4f(const Vector3f& normal, float d);
			Plane4f(const Vector3f& normal, const Vector3f& pointOnPlane);
			Plane4f(const Vector3f& p0, const Vector3f& p1, const Vector3f& p2); // from triangle
			Plane4f(const Vect4f& plane);
			Plane4f(const Plane4f& plane);

			Plane4f& operator=(const Plane4f& plane);
			Plane4f& operator=(const Vect4f& plane);

			float  operator[](int iPos) const;
			float& operator[](int iPos);

			bool operator==(const Plane4f& plane) const;
			bool operator!=(const Plane4f& plane) const;

			Plane4f  operator-() const;
			Plane4f  operator*(float fScalar) const;
			Plane4f  operator/(float fScalar) const;
			Plane4f& operator*=(float fScalar);
			Plane4f& operator/=(float fScalar);

			Vector3f Normal() const;
			void     SetNormal(const Vector3f& n);

			void  Normalize();
			float DistanceToPoint(const Vector3f& pt) const;
			int   ClassifyPoint(const Vector3f& pt, float epsilon = 1.0e-6f) const; // -1, 0, +1
		};
	}
}
