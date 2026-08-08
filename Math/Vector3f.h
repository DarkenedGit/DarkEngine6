#pragma once 

namespace Dark
{
	namespace Math
	{
		struct Vect3f
		{
			float x;
			float y;
			float z;
			static const Vect3f ZERO;
			static const Vect3f ONE;
			static const Vect3f X_AXIS;
			static const Vect3f Y_AXIS;
			static const Vect3f Z_AXIS;
		};

		class Vector3f final : public Vect3f
		{
		public:
			Vector3f();
			Vector3f(float x, float y, float z);
			Vector3f(const Vect3f& Vector);
			Vector3f(const Vector3f& Vector) noexcept;

			// assignment
			Vector3f& operator= (const Vector3f& Vector) noexcept;
			Vector3f& operator= (const Vect3f& Vector);

			// Array style access
			float  operator[] (int iPos) const;
			float& operator[] (int iPos);

			// comparison
			bool operator==(const Vector3f& Vector) const;
			bool operator!=(const Vector3f& Vector) const;

			// arithmetic operations
			Vector3f operator+(const Vector3f& Vector) const;
			Vector3f operator-(const Vector3f& Vector) const;
			Vector3f operator*(const Vector3f& Vector) const;
			Vector3f operator/(const Vector3f& Vector) const;

			Vector3f operator+(float fScalar) const;
			Vector3f operator-(float fScalar) const;
			Vector3f operator*(float fScalar) const;
			Vector3f operator/(float fScalar) const;
			Vector3f operator-() const;

			// arithmetic updates
			Vector3f& operator+=(const Vector3f& Vector);
			Vector3f& operator-=(const Vector3f& Vector);
			Vector3f& operator*=(const Vector3f& Vector);
			Vector3f& operator/=(const Vector3f& Vector);

			Vector3f& operator+=(float fScalar);
			Vector3f& operator-=(float fScalar);
			Vector3f& operator*=(float fScalar);
			Vector3f& operator/=(float fScalar);

			// Geo operations
			void		Clamp();
			Vector3f	Cross(const Vector3f& A) const;
			float		Dot(const Vector3f& A) const;		// Dot is the sameness of two vectors, 1 is the 100% same, 0 is 100% different.
			float		Magnitude() const;
			float		MagnitudeSqrd() const;
			void		Normalize();						// Make length 1
			Vector3f	Perpendicular();
			bool		IsNaN();

			// static vector ops
			static Vector3f RandUnitPoint();  // Point on unit sphere
			static Vector3f RandomPoint();    // Point withhin unit box
		};
	}
}
