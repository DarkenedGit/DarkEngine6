#pragma once

#include "Matrix3f.h"
#include "Vector4f.h"

namespace Dark
{
	namespace Math
	{
		// Row-major 4x4 matrix (D3D-friendly: translation in m41,m42,m43).
		struct Mat4f
		{
			static const Mat4f ZERO;
			static const Mat4f IDENTITY;

			static const int m11 = 0;
			static const int m12 = 1;
			static const int m13 = 2;
			static const int m14 = 3;

			static const int m21 = 4;
			static const int m22 = 5;
			static const int m23 = 6;
			static const int m24 = 7;

			static const int m31 = 8;
			static const int m32 = 9;
			static const int m33 = 10;
			static const int m34 = 11;

			static const int m41 = 12;
			static const int m42 = 13;
			static const int m43 = 14;
			static const int m44 = 15;

			float m_afEntry[16];
		};

		class Matrix4f final : public Mat4f
		{
		public:
			Matrix4f();
			Matrix4f(const Matrix4f& Matrix);
			Matrix4f(const Mat4f& Matrix);
			Matrix4f(float fM11, float fM12, float fM13, float fM14,
			         float fM21, float fM22, float fM23, float fM24,
			         float fM31, float fM32, float fM33, float fM34,
			         float fM41, float fM42, float fM43, float fM44);

			Matrix4f Inverse() const;

			void RotationX(float fRadians);
			void RotationY(float fRadians);
			void RotationZ(float fRadians);
			void Rotation(const Vector3f& Rot);
			void Scale(float fScale);
			void Scale(float fXScale, float fYScale, float fZScale);
			void Translate(float fX, float fY, float fZ);
			void Transpose();

			Vector3f GetBasisX() const;
			Vector3f GetBasisY() const;
			Vector3f GetBasisZ() const;
			Vector3f GetTranslation() const;
			Matrix3f GetRotation() const;

			void SetRotation(const Matrix3f& Rot);
			void SetTranslation(const Vector3f& Trans);

			static Matrix4f RotationMatrixXYZ(float fRadiansX, float fRadiansY, float fRadiansZ);
			static Matrix4f RotationMatrixX(float fRadians);
			static Matrix4f RotationMatrixY(float fRadians);
			static Matrix4f RotationMatrixZ(float fRadians);
			static Matrix4f ScaleMatrix(float fScale);
			static Matrix4f ScaleMatrix(const Vector3f& scale);
			static Matrix4f ScaleMatrixXYZ(float fX, float fY, float fZ);
			static Matrix4f TranslationMatrix(float fX, float fY, float fZ);
			static Matrix4f LookAtLHMatrix(const Vector3f& eye, const Vector3f& at, const Vector3f& up);
			static Matrix4f PerspectiveFovLHMatrix(float fovy, float aspect, float zn, float zf);
			static Matrix4f OrthographicLHMatrix(float zn, float zf, float width, float height);
			static Matrix4f OrthographicOffCenterLHMatrix(float l, float r, float b, float t, float zn, float zf);

			// Operators
			Matrix4f& operator=(const Matrix4f& Matrix);
			Matrix4f& operator=(const Mat4f& Matrix);

			// member access
			float  operator()(int iRow, int iCol) const;
			float& operator()(int iRow, int iCol);
			float  operator[](int iPos) const;
			float& operator[](int iPos);

			void     SetRow(int iRow, const Vector4f& Vector);
			void     SetRow(int iRow, const Vector3f& Vector);
			Vector4f GetRow(int iRow) const;
			void     SetColumn(int iCol, const Vector4f& Vector);
			Vector4f GetColumn(int iCol) const;

			// comparison
			bool operator==(const Matrix4f& Matrix) const;
			bool operator!=(const Matrix4f& Matrix) const;

			// arithmetic operations
			Matrix4f operator*(const Matrix4f& Matrix) const;

			// arithmetic updates
			Matrix4f& operator+=(const Matrix4f& Matrix);
			Matrix4f& operator-=(const Matrix4f& Matrix);
			Matrix4f& operator*=(const Matrix4f& Matrix);
			Matrix4f& operator*=(float fScalar);
			Matrix4f& operator/=(float fScalar);

			// matrix - vector operations
			Vector4f operator*(const Vector4f& V) const; // M * v

		protected:
			static int I(int iRow, int iCol); // iRow*N + iCol
		};
	}
}
