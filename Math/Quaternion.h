#pragma once

#include "Vector3f.h"
#include "Matrix3f.h"
#include "Matrix4f.h"

namespace Dark
{
	namespace Math
	{
		class Quaternion
		{
		public:
			float w;
			float x;
			float y;
			float z;

			static const Quaternion IDENTITY;

			Quaternion();
			Quaternion(float w, float x, float y, float z);
			Quaternion(const Quaternion& q);

			Quaternion& operator=(const Quaternion& q);

			float Length() const;
			float LengthSquared() const;
			float Dot(const Quaternion& a) const;

			void       Normalize();
			Quaternion Conjugate() const;
			Quaternion Inverse() const;

			// Rotate a vector by this quaternion (unit assumed after Normalize).
			Vector3f Rotate(const Vector3f& v) const;

			// Conversions
			Matrix3f ToMatrix3() const;
			Matrix4f ToMatrix4() const;

			static Quaternion FromAxisAngle(const Vector3f& axis, float radians);
			static Quaternion FromEulerXYZ(float pitchX, float yawY, float rollZ);
			static Quaternion FromMatrix3(const Matrix3f& m);
			static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t);
			static Quaternion Lerp(const Quaternion& a, const Quaternion& b, float t);

			Quaternion operator+(const Quaternion& a) const;
			Quaternion operator-(const Quaternion& a) const;
			Quaternion operator*(const Quaternion& a) const;
			Quaternion operator/(const Quaternion& a) const;
			Quaternion operator*(float real) const;
			Quaternion operator/(float real) const;

			Quaternion& operator*=(const Quaternion& a);
			Quaternion& operator*=(float real);
		};
	}
}
