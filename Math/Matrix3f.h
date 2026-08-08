#pragma once

#include "Vector3f.h"

namespace Dark
{
	namespace Math
	{
		// Row-major 3x3 matrix (matches original DarkEngine storage).
		struct Mat3f
		{
			static const Mat3f ZERO;
			static const Mat3f IDENTITY;

			static const int m11 = 0;
			static const int m12 = 1;
			static const int m13 = 2;

			static const int m21 = 3;
			static const int m22 = 4;
			static const int m23 = 5;

			static const int m31 = 6;
			static const int m32 = 7;
			static const int m33 = 8;

			float m_afEntry[9];
		};

		class Matrix3f final : public Mat3f
		{
		public:
			Matrix3f();
			Matrix3f(const Matrix3f& Matrix);
			Matrix3f(const Mat3f& Matrix);
			Matrix3f(float fM11, float fM12, float fM13,
			         float fM21, float fM22, float fM23,
			         float fM31, float fM32, float fM33);

			void RotationX(float fRadians);
			void RotationY(float fRadians);
			void RotationZ(float fRadians);
			void Rotation(const Vector3f& Rot);
			void RotationEuler(const Vector3f& Axis, float Angle);
			void Orthonormalize();
			void Transpose();
			void Scale(float fScale);
			void Scale(float fXScale, float fYScale, float fZScale);

			// Operators
			Matrix3f& operator=(const Matrix3f& Matrix);
			Matrix3f& operator=(const Mat3f& Matrix);

			// member access
			float  operator()(int iRow, int iCol) const;
			float& operator()(int iRow, int iCol);
			float  operator[](int iPos) const;
			float& operator[](int iPos);

			void     SetRow(int iRow, const Vector3f& Vector);
			Vector3f GetRow(int iRow) const;
			void     SetColumn(int iCol, const Vector3f& Vector);
			Vector3f GetColumn(int iCol) const;

			// comparison
			bool operator==(const Matrix3f& Matrix) const;
			bool operator!=(const Matrix3f& Matrix) const;

			// arithmetic updates
			Matrix3f& operator+=(const Matrix3f& Matrix);
			Matrix3f& operator-=(const Matrix3f& Matrix);
			Matrix3f& operator*=(const Matrix3f& Matrix);
			Matrix3f& operator*=(float fScalar);
			Matrix3f& operator/=(float fScalar);

			// matrix - vector operations
			Vector3f operator*(const Vector3f& rkV) const; // M * v

		protected:
			static int I(int iRow, int iCol); // iRow*N + iCol
		};
	}
}
