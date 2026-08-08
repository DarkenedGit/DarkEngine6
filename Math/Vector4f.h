#pragma once 

namespace Dark
{
	namespace Math
	{

		class Vector2f;
		class Vector3f;

		struct Vect4f
		{
			float x;
			float y;
			float z;
			float w;

			static const Vect4f ZERO;
			static const Vect4f ONE;
			static const Vect4f X_AXIS;
			static const Vect4f Y_AXIS;
			static const Vect4f Z_AXIS;
			static const Vect4f W_AXIS;

			static const Vect4f BLACK;
			static const Vect4f RED;
			static const Vect4f GREEN;
			static const Vect4f BLUE;
			static const Vect4f WHITE;
		};

		class Vector4f final : public Vect4f
		{
		public:
			Vector4f();
			Vector4f(float x, float y, float z, float w);
			Vector4f(const Vector3f& vector, float w);
			Vector4f(const Vect4f& Vector);
			Vector4f(const Vector4f& Vector) noexcept;

			// Operators
			Vector4f& operator= (const Vector4f& Vector) noexcept;
			Vector4f& operator= (const Vect4f& Vector);

			// member access
			float operator[] (int iPos) const;
			float& operator[] (int iPos);

			// comparison
			bool operator== (const Vector4f& Vector) const;
			bool operator!= (const Vector4f& Vector) const;

			// arithmetic operations
			Vector4f operator+ (const Vector4f& Vector) const;
			Vector4f operator- (const Vector4f& Vector) const;
			Vector4f operator* (const Vector4f& Vector) const;
			Vector4f operator/ (const Vector4f& Vector) const;

			Vector4f operator+ (float fScalar) const;
			Vector4f operator- (float fScalar) const;
			Vector4f operator* (float fScalar) const;
			Vector4f operator/ (float fScalar) const;

			Vector4f operator- () const;

			// arithmetic updates
			Vector4f& operator+= (const Vector4f& Vector);
			Vector4f& operator-= (const Vector4f& Vector);
			Vector4f& operator*= (const Vector4f& Vector);
			Vector4f& operator/= (const Vector4f& Vector);

			Vector4f& operator+= (float fScalar);
			Vector4f& operator-= (float fScalar);
			Vector4f& operator*= (float fScalar);
			Vector4f& operator/= (float fScalar);

			unsigned int toARGB();
			unsigned int toRGBA();
			void fromARGB(unsigned int color);

			Vector3f xyz() const;
			Vector2f xy() const;

			// vector operations
			void  Clamp();
			float Dot(const Vector4f& vector) const;	// Dot is the sameness of two vectors, 1 is the 100% same, 0 is 100% different.
			void  Normalize();							// Make length 1
			float Magnitude() const;
			bool  IsNaN() const;

		};
	}
}

