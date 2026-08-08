#pragma once

namespace Dark
{
	namespace Math
	{
		// POD as base for statics
		struct Vect2f
		{
			float x;
			float y;

			static const Vect2f ZERO;		// Tex Coords
			static const Vect2f ONE;		// Tex Coords
			static const Vect2f QNAN;		// quiet NaN (avoids Windows NAN macro)
			static const Vect2f X_AXIS;		// Vect, Tex Coords
			static const Vect2f Y_AXIS;		// Vect, Tex Coords
		};

		// Non POD for operations
		class Vector2f final : public Vect2f
		{
		public:
			Vector2f();
			Vector2f(float x, float y);
			Vector2f(const Vect2f& Vector);
			Vector2f(const Vector2f& Vector) noexcept;

			// vector operations
			void  Clamp();
			void  Normalize();							// Make length 1
			float Magnitude() const;
			float MagnitudeSqrd() const;
			bool  IsNaN() const;
			float Dot(const Vect2f& Vector) const;		// Dot = sameness of two vectors, 1 is the 100% same, 0 is 100% different.

			float Cross(const Vector2f& Vector) const;	// 2D cross (scalar): x*Vy - y*Vx
			Vector2f Perpendicular() const;				// rotated 90° CCW: (-y, x)
			void  Min(const Vector2f& Vector);			// Min of each component of the vectors
			void  Max(const Vector2f& Vector);			// Max of each component of the vectors
			Vector2f Reflect(const Vector2f& Normal);	// Reflect the vector around the normal

			static Vector2f RandUnitPoint(); // point on unit circle
			static Vector2f RandomPoint();   // point in unit box [-1,1]^2

			// assignment
			Vector2f& operator=(const Vector2f& Vector) noexcept;
			Vector2f& operator=(const Vect2f& Vector);

			// accessors
			float  operator[](int iPos) const;
			float& operator[](int iPos);

			// boolean comparison
			bool operator==(const Vector2f& Vector) const;
			bool operator!=(const Vector2f& Vector) const;

			// arithmetic operations
			Vector2f operator+(const Vector2f& Vector) const;
			Vector2f operator-(const Vector2f& Vector) const;
			Vector2f operator*(const Vector2f& Vector) const;// Non uniform scaling

			Vector2f operator+(float fScalar) const;
			Vector2f operator-(float fScalar) const;
			Vector2f operator*(float fScalar) const;
			Vector2f operator/(float fScalar) const;
			Vector2f operator-() const;

			// arithmetic updates
			Vector2f& operator+=(const Vector2f& Vector);
			Vector2f& operator-=(const Vector2f& Vector);
			Vector2f& operator*=(const Vector2f& Vector);		//Non uniform scaling
			Vector2f& operator+=(float fScalar);
			Vector2f& operator-=(float fScalar);
			Vector2f& operator*=(float fScalar);
			Vector2f& operator/=(float fScalar);
		};
	}
}
