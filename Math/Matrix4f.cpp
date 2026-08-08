#include "Matrix4f.h"
#include <cmath>
#include <cstring>

namespace Dark
{
	namespace Math
	{
		const Mat4f Mat4f::ZERO = {
			0.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 0.0f
		};

		const Mat4f Mat4f::IDENTITY = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};

		Matrix4f::Matrix4f()
		{
			*this = IDENTITY;
		}

		Matrix4f::Matrix4f(const Matrix4f& Matrix)
		{
			memcpy(m_afEntry, Matrix.m_afEntry, 16 * sizeof(float));
		}

		Matrix4f::Matrix4f(const Mat4f& Matrix)
		{
			memcpy(m_afEntry, Matrix.m_afEntry, 16 * sizeof(float));
		}

		Matrix4f::Matrix4f(float fM11, float fM12, float fM13, float fM14,
		                   float fM21, float fM22, float fM23, float fM24,
		                   float fM31, float fM32, float fM33, float fM34,
		                   float fM41, float fM42, float fM43, float fM44)
		{
			m_afEntry[m11] = fM11;
			m_afEntry[m12] = fM12;
			m_afEntry[m13] = fM13;
			m_afEntry[m14] = fM14;

			m_afEntry[m21] = fM21;
			m_afEntry[m22] = fM22;
			m_afEntry[m23] = fM23;
			m_afEntry[m24] = fM24;

			m_afEntry[m31] = fM31;
			m_afEntry[m32] = fM32;
			m_afEntry[m33] = fM33;
			m_afEntry[m34] = fM34;

			m_afEntry[m41] = fM41;
			m_afEntry[m42] = fM42;
			m_afEntry[m43] = fM43;
			m_afEntry[m44] = fM44;
		}

		Matrix4f& Matrix4f::operator=(const Matrix4f& Matrix)
		{
			memcpy(m_afEntry, Matrix.m_afEntry, 16 * sizeof(float));
			return *this;
		}

		Matrix4f& Matrix4f::operator=(const Mat4f& Matrix)
		{
			memcpy(m_afEntry, Matrix.m_afEntry, 16 * sizeof(float));
			return *this;
		}

		void Matrix4f::RotationX(float fRadians)
		{
			float fSin = sinf(fRadians);
			float fCos = cosf(fRadians);

			m_afEntry[m11] = 1.0f;
			m_afEntry[m12] = 0.0f;
			m_afEntry[m13] = 0.0f;
			m_afEntry[m14] = 0.0f;

			m_afEntry[m21] = 0.0f;
			m_afEntry[m22] = fCos;
			m_afEntry[m23] = fSin;
			m_afEntry[m24] = 0.0f;

			m_afEntry[m31] = 0.0f;
			m_afEntry[m32] = -fSin;
			m_afEntry[m33] = fCos;
			m_afEntry[m34] = 0.0f;

			m_afEntry[m41] = 0.0f;
			m_afEntry[m42] = 0.0f;
			m_afEntry[m43] = 0.0f;
			m_afEntry[m44] = 1.0f;
		}

		void Matrix4f::RotationY(float fRadians)
		{
			float fSin = sinf(fRadians);
			float fCos = cosf(fRadians);

			m_afEntry[m11] = fCos;
			m_afEntry[m12] = 0.0f;
			m_afEntry[m13] = -fSin;
			m_afEntry[m14] = 0.0f;

			m_afEntry[m21] = 0.0f;
			m_afEntry[m22] = 1.0f;
			m_afEntry[m23] = 0.0f;
			m_afEntry[m24] = 0.0f;

			m_afEntry[m31] = fSin;
			m_afEntry[m32] = 0.0f;
			m_afEntry[m33] = fCos;
			m_afEntry[m34] = 0.0f;

			m_afEntry[m41] = 0.0f;
			m_afEntry[m42] = 0.0f;
			m_afEntry[m43] = 0.0f;
			m_afEntry[m44] = 1.0f;
		}

		void Matrix4f::RotationZ(float fRadians)
		{
			float fSin = sinf(fRadians);
			float fCos = cosf(fRadians);

			m_afEntry[m11] = fCos;
			m_afEntry[m12] = fSin;
			m_afEntry[m13] = 0.0f;
			m_afEntry[m14] = 0.0f;

			m_afEntry[m21] = -fSin;
			m_afEntry[m22] = fCos;
			m_afEntry[m23] = 0.0f;
			m_afEntry[m24] = 0.0f;

			m_afEntry[m31] = 0.0f;
			m_afEntry[m32] = 0.0f;
			m_afEntry[m33] = 1.0f;
			m_afEntry[m34] = 0.0f;

			m_afEntry[m41] = 0.0f;
			m_afEntry[m42] = 0.0f;
			m_afEntry[m43] = 0.0f;
			m_afEntry[m44] = 1.0f;
		}

		void Matrix4f::Scale(float fScale)
		{
			Scale(fScale, fScale, fScale);
		}

		void Matrix4f::Scale(float fXScale, float fYScale, float fZScale)
		{
			m_afEntry[m11] = fXScale;
			m_afEntry[m12] = 0.0f;
			m_afEntry[m13] = 0.0f;
			m_afEntry[m14] = 0.0f;

			m_afEntry[m21] = 0.0f;
			m_afEntry[m22] = fYScale;
			m_afEntry[m23] = 0.0f;
			m_afEntry[m24] = 0.0f;

			m_afEntry[m31] = 0.0f;
			m_afEntry[m32] = 0.0f;
			m_afEntry[m33] = fZScale;
			m_afEntry[m34] = 0.0f;

			m_afEntry[m41] = 0.0f;
			m_afEntry[m42] = 0.0f;
			m_afEntry[m43] = 0.0f;
			m_afEntry[m44] = 1.0f;
		}

		void Matrix4f::Translate(float fX, float fY, float fZ)
		{
			m_afEntry[m11] = 1.0f;
			m_afEntry[m12] = 0.0f;
			m_afEntry[m13] = 0.0f;
			m_afEntry[m14] = 0.0f;

			m_afEntry[m21] = 0.0f;
			m_afEntry[m22] = 1.0f;
			m_afEntry[m23] = 0.0f;
			m_afEntry[m24] = 0.0f;

			m_afEntry[m31] = 0.0f;
			m_afEntry[m32] = 0.0f;
			m_afEntry[m33] = 1.0f;
			m_afEntry[m34] = 0.0f;

			m_afEntry[m41] = fX;
			m_afEntry[m42] = fY;
			m_afEntry[m43] = fZ;
			m_afEntry[m44] = 1.0f;
		}

		Matrix4f Matrix4f::Inverse() const
		{
			float fA0 = m_afEntry[m11] * m_afEntry[m22] - m_afEntry[m12] * m_afEntry[m21];
			float fA1 = m_afEntry[m11] * m_afEntry[m23] - m_afEntry[m13] * m_afEntry[m21];
			float fA2 = m_afEntry[m11] * m_afEntry[m24] - m_afEntry[m14] * m_afEntry[m21];
			float fA3 = m_afEntry[m12] * m_afEntry[m23] - m_afEntry[m13] * m_afEntry[m22];
			float fA4 = m_afEntry[m12] * m_afEntry[m24] - m_afEntry[m14] * m_afEntry[m22];
			float fA5 = m_afEntry[m13] * m_afEntry[m24] - m_afEntry[m14] * m_afEntry[m23];
			float fB0 = m_afEntry[m31] * m_afEntry[m42] - m_afEntry[m32] * m_afEntry[m41];
			float fB1 = m_afEntry[m31] * m_afEntry[m43] - m_afEntry[m33] * m_afEntry[m41];
			float fB2 = m_afEntry[m31] * m_afEntry[m44] - m_afEntry[m34] * m_afEntry[m41];
			float fB3 = m_afEntry[m32] * m_afEntry[m43] - m_afEntry[m33] * m_afEntry[m42];
			float fB4 = m_afEntry[m32] * m_afEntry[m44] - m_afEntry[m34] * m_afEntry[m42];
			float fB5 = m_afEntry[m33] * m_afEntry[m44] - m_afEntry[m34] * m_afEntry[m43];

			float fDet = fA0 * fB5 - fA1 * fB4 + fA2 * fB3 + fA3 * fB2 - fA4 * fB1 + fA5 * fB0;

			Matrix4f kInv;
			kInv(0, 0) = +m_afEntry[m22] * fB5 - m_afEntry[m23] * fB4 + m_afEntry[m24] * fB3;
			kInv(1, 0) = -m_afEntry[m21] * fB5 + m_afEntry[m23] * fB2 - m_afEntry[m24] * fB1;
			kInv(2, 0) = +m_afEntry[m21] * fB4 - m_afEntry[m22] * fB2 + m_afEntry[m24] * fB0;
			kInv(3, 0) = -m_afEntry[m21] * fB3 + m_afEntry[m22] * fB1 - m_afEntry[m23] * fB0;
			kInv(0, 1) = -m_afEntry[m12] * fB5 + m_afEntry[m13] * fB4 - m_afEntry[m14] * fB3;
			kInv(1, 1) = +m_afEntry[m11] * fB5 - m_afEntry[m13] * fB2 + m_afEntry[m14] * fB1;
			kInv(2, 1) = -m_afEntry[m11] * fB4 + m_afEntry[m12] * fB2 - m_afEntry[m14] * fB0;
			kInv(3, 1) = +m_afEntry[m11] * fB3 - m_afEntry[m12] * fB1 + m_afEntry[m13] * fB0;
			kInv(0, 2) = +m_afEntry[m42] * fA5 - m_afEntry[m43] * fA4 + m_afEntry[m44] * fA3;
			kInv(1, 2) = -m_afEntry[m41] * fA5 + m_afEntry[m43] * fA2 - m_afEntry[m44] * fA1;
			kInv(2, 2) = +m_afEntry[m41] * fA4 - m_afEntry[m42] * fA2 + m_afEntry[m44] * fA0;
			kInv(3, 2) = -m_afEntry[m41] * fA3 + m_afEntry[m42] * fA1 - m_afEntry[m43] * fA0;
			kInv(0, 3) = -m_afEntry[m32] * fA5 + m_afEntry[m33] * fA4 - m_afEntry[m34] * fA3;
			kInv(1, 3) = +m_afEntry[m31] * fA5 - m_afEntry[m33] * fA2 + m_afEntry[m34] * fA1;
			kInv(2, 3) = -m_afEntry[m31] * fA4 + m_afEntry[m32] * fA2 - m_afEntry[m34] * fA0;
			kInv(3, 3) = +m_afEntry[m31] * fA3 - m_afEntry[m32] * fA1 + m_afEntry[m33] * fA0;

			float fInvDet = 1.0f / fDet;
			for (int iRow = 0; iRow < 4; iRow++)
			{
				for (int iCol = 0; iCol < 4; iCol++)
					kInv(iRow, iCol) *= fInvDet;
			}

			return kInv;
		}

		void Matrix4f::Rotation(const Vector3f& Rot)
		{
			Matrix4f mRot1;
			Matrix4f mRot2;

			mRot1.RotationZ(Rot.z);
			mRot2.RotationX(Rot.x);
			mRot1 *= mRot2;
			mRot2.RotationY(Rot.y);
			mRot1 *= mRot2;
			*this = mRot1;
		}

		Vector3f Matrix4f::GetBasisX() const
		{
			Vector3f Basis;
			for (int i = 0; i < 3; i++)
				Basis[i] = m_afEntry[I(0, i)];
			return Basis;
		}

		Vector3f Matrix4f::GetBasisY() const
		{
			Vector3f Basis;
			for (int i = 0; i < 3; i++)
				Basis[i] = m_afEntry[I(1, i)];
			return Basis;
		}

		Vector3f Matrix4f::GetBasisZ() const
		{
			Vector3f Basis;
			for (int i = 0; i < 3; i++)
				Basis[i] = m_afEntry[I(2, i)];
			return Basis;
		}

		Vector3f Matrix4f::GetTranslation() const
		{
			Vector3f Pos;
			for (int i = 0; i < 3; i++)
				Pos[i] = m_afEntry[I(3, i)];
			return Pos;
		}

		void Matrix4f::SetTranslation(const Vector3f& Trans)
		{
			for (int i = 0; i < 3; i++)
				m_afEntry[I(3, i)] = Trans[i];
		}

		Matrix3f Matrix4f::GetRotation() const
		{
			Matrix3f mRet;
			mRet.SetRow(0, GetBasisX());
			mRet.SetRow(1, GetBasisY());
			mRet.SetRow(2, GetBasisZ());
			return mRet;
		}

		void Matrix4f::SetRotation(const Matrix3f& Rot)
		{
			for (int i = 0; i < 3; i++)
				for (int j = 0; j < 3; j++)
					m_afEntry[I(i, j)] = Rot[(3 * i + j)];
		}

		Matrix4f Matrix4f::RotationMatrixXYZ(float fRadiansX, float fRadiansY, float fRadiansZ)
		{
			return RotationMatrixZ(fRadiansZ) * RotationMatrixX(fRadiansX) * RotationMatrixY(fRadiansY);
		}

		Matrix4f Matrix4f::RotationMatrixX(float fRadians)
		{
			Matrix4f ret;
			ret.RotationX(fRadians);
			return ret;
		}

		Matrix4f Matrix4f::RotationMatrixY(float fRadians)
		{
			Matrix4f ret;
			ret.RotationY(fRadians);
			return ret;
		}

		Matrix4f Matrix4f::RotationMatrixZ(float fRadians)
		{
			Matrix4f ret;
			ret.RotationZ(fRadians);
			return ret;
		}

		Matrix4f Matrix4f::ScaleMatrix(float fScale)
		{
			Matrix4f ret;
			ret.Scale(fScale);
			return ret;
		}

		Matrix4f Matrix4f::ScaleMatrix(const Vector3f& scale)
		{
			return ScaleMatrixXYZ(scale.x, scale.y, scale.z);
		}

		Matrix4f Matrix4f::ScaleMatrixXYZ(float fX, float fY, float fZ)
		{
			Matrix4f ret;
			ret.Scale(fX, fY, fZ);
			return ret;
		}

		Matrix4f Matrix4f::TranslationMatrix(float fX, float fY, float fZ)
		{
			Matrix4f ret;
			ret.Translate(fX, fY, fZ);
			return ret;
		}

		Matrix4f Matrix4f::LookAtLHMatrix(const Vector3f& eye, const Vector3f& at, const Vector3f& up)
		{
			// D3DX-style left-handed look-at
			Matrix4f ret;

			Vector3f zaxis = at - eye;
			zaxis.Normalize();

			Vector3f xaxis = up.Cross(zaxis);
			xaxis.Normalize();

			Vector3f yaxis = zaxis.Cross(xaxis);

			ret.m_afEntry[m11] = xaxis.x;
			ret.m_afEntry[m12] = yaxis.x;
			ret.m_afEntry[m13] = zaxis.x;
			ret.m_afEntry[m14] = 0.0f;

			ret.m_afEntry[m21] = xaxis.y;
			ret.m_afEntry[m22] = yaxis.y;
			ret.m_afEntry[m23] = zaxis.y;
			ret.m_afEntry[m24] = 0.0f;

			ret.m_afEntry[m31] = xaxis.z;
			ret.m_afEntry[m32] = yaxis.z;
			ret.m_afEntry[m33] = zaxis.z;
			ret.m_afEntry[m34] = 0.0f;

			ret.m_afEntry[m41] = -(xaxis.Dot(eye));
			ret.m_afEntry[m42] = -(yaxis.Dot(eye));
			ret.m_afEntry[m43] = -(zaxis.Dot(eye));
			ret.m_afEntry[m44] = 1.0f;

			return ret;
		}

		Matrix4f Matrix4f::PerspectiveFovLHMatrix(float fovy, float aspect, float zn, float zf)
		{
			// D3DX-style left-handed perspective FOV
			Matrix4f ret;

			float tanY = tanf(fovy / 2.0f);
			if (tanY == 0.0f)
				tanY = 0.001f;
			float yScale = 1.0f / tanY;

			if (aspect == 0.0f)
				aspect = 0.001f;
			float xScale = yScale / aspect;

			ret.m_afEntry[m11] = xScale;
			ret.m_afEntry[m12] = 0.0f;
			ret.m_afEntry[m13] = 0.0f;
			ret.m_afEntry[m14] = 0.0f;

			ret.m_afEntry[m21] = 0.0f;
			ret.m_afEntry[m22] = yScale;
			ret.m_afEntry[m23] = 0.0f;
			ret.m_afEntry[m24] = 0.0f;

			ret.m_afEntry[m31] = 0.0f;
			ret.m_afEntry[m32] = 0.0f;
			ret.m_afEntry[m33] = zf / (zf - zn);
			ret.m_afEntry[m34] = 1.0f;

			ret.m_afEntry[m41] = 0.0f;
			ret.m_afEntry[m42] = 0.0f;
			ret.m_afEntry[m43] = -zn * zf / (zf - zn);
			ret.m_afEntry[m44] = 0.0f;

			return ret;
		}

		Matrix4f Matrix4f::OrthographicLHMatrix(float zn, float zf, float width, float height)
		{
			// D3DX-style left-handed orthographic
			Matrix4f ret;

			if (zn == zf)
				zf = zn + 0.1f;
			if (width <= 0.0f)
				width = 1.0f;
			if (height <= 0.0f)
				height = 1.0f;

			ret.m_afEntry[m11] = 2.0f / width;
			ret.m_afEntry[m12] = 0.0f;
			ret.m_afEntry[m13] = 0.0f;
			ret.m_afEntry[m14] = 0.0f;

			ret.m_afEntry[m21] = 0.0f;
			ret.m_afEntry[m22] = 2.0f / height;
			ret.m_afEntry[m23] = 0.0f;
			ret.m_afEntry[m24] = 0.0f;

			ret.m_afEntry[m31] = 0.0f;
			ret.m_afEntry[m32] = 0.0f;
			ret.m_afEntry[m33] = 1.0f / (zf - zn);
			ret.m_afEntry[m34] = 0.0f;

			ret.m_afEntry[m41] = 0.0f;
			ret.m_afEntry[m42] = 0.0f;
			ret.m_afEntry[m43] = zn / (zn - zf);
			ret.m_afEntry[m44] = 1.0f;

			return ret;
		}

		float Matrix4f::operator[](int iPos) const
		{
			return m_afEntry[iPos];
		}

		float& Matrix4f::operator[](int iPos)
		{
			return m_afEntry[iPos];
		}

		float Matrix4f::operator()(int iRow, int iCol) const
		{
			return m_afEntry[I(iRow, iCol)];
		}

		float& Matrix4f::operator()(int iRow, int iCol)
		{
			return m_afEntry[I(iRow, iCol)];
		}

		bool Matrix4f::operator==(const Matrix4f& Matrix) const
		{
			return memcmp(m_afEntry, Matrix.m_afEntry, 16 * sizeof(float)) == 0;
		}

		bool Matrix4f::operator!=(const Matrix4f& Matrix) const
		{
			return memcmp(m_afEntry, Matrix.m_afEntry, 16 * sizeof(float)) != 0;
		}

		Matrix4f Matrix4f::operator*(const Matrix4f& Matrix) const
		{
			Matrix4f mProd(ZERO);

			for (int iRow = 0; iRow < 4; iRow++)
			{
				for (int iCol = 0; iCol < 4; iCol++)
				{
					int i = I(iRow, iCol);
					mProd.m_afEntry[i] += m_afEntry[I(iRow, 0)] * Matrix.m_afEntry[I(0, iCol)];
					mProd.m_afEntry[i] += m_afEntry[I(iRow, 1)] * Matrix.m_afEntry[I(1, iCol)];
					mProd.m_afEntry[i] += m_afEntry[I(iRow, 2)] * Matrix.m_afEntry[I(2, iCol)];
					mProd.m_afEntry[i] += m_afEntry[I(iRow, 3)] * Matrix.m_afEntry[I(3, iCol)];
				}
			}
			return mProd;
		}

		Matrix4f& Matrix4f::operator+=(const Matrix4f& Matrix)
		{
			for (int i = 0; i < 16; i++)
				m_afEntry[i] += Matrix.m_afEntry[i];
			return *this;
		}

		Matrix4f& Matrix4f::operator-=(const Matrix4f& Matrix)
		{
			for (int i = 0; i < 16; i++)
				m_afEntry[i] -= Matrix.m_afEntry[i];
			return *this;
		}

		Matrix4f& Matrix4f::operator*=(float fScalar)
		{
			for (int i = 0; i < 16; i++)
				m_afEntry[i] *= fScalar;
			return *this;
		}

		Matrix4f& Matrix4f::operator*=(const Matrix4f& Matrix)
		{
			Matrix4f mProd(*this);
			*this = ZERO;

			for (int iRow = 0; iRow < 4; iRow++)
			{
				for (int iCol = 0; iCol < 4; iCol++)
				{
					int i = I(iRow, iCol);
					m_afEntry[i] += mProd.m_afEntry[I(iRow, 0)] * Matrix.m_afEntry[I(0, iCol)];
					m_afEntry[i] += mProd.m_afEntry[I(iRow, 1)] * Matrix.m_afEntry[I(1, iCol)];
					m_afEntry[i] += mProd.m_afEntry[I(iRow, 2)] * Matrix.m_afEntry[I(2, iCol)];
					m_afEntry[i] += mProd.m_afEntry[I(iRow, 3)] * Matrix.m_afEntry[I(3, iCol)];
				}
			}
			return *this;
		}

		Matrix4f& Matrix4f::operator/=(float fScalar)
		{
			if (fScalar != 0.0f)
			{
				float fInvScalar = 1.0f / fScalar;
				for (int i = 0; i < 16; i++)
					m_afEntry[i] *= fInvScalar;
			}
			else
			{
				*this = ZERO;
			}
			return *this;
		}

		void Matrix4f::Transpose()
		{
			Matrix4f mTranspose;

			for (int iRow = 0; iRow < 4; iRow++)
			{
				for (int iCol = 0; iCol < 4; iCol++)
					mTranspose.m_afEntry[I(iRow, iCol)] = m_afEntry[I(iCol, iRow)];
			}

			memcpy(m_afEntry, mTranspose.m_afEntry, 16 * sizeof(float));
		}

		int Matrix4f::I(int iRow, int iCol)
		{
			return 4 * iRow + iCol;
		}

		Vector4f Matrix4f::operator*(const Vector4f& Vector) const
		{
			Vector4f vProd(Vector4f::ZERO);
			for (int iCol = 0; iCol < 4; iCol++)
			{
				for (int iRow = 0; iRow < 4; iRow++)
					vProd[iCol] += m_afEntry[I(iRow, iCol)] * Vector[iRow];
			}
			return vProd;
		}

		void Matrix4f::SetRow(int iRow, const Vector4f& Vector)
		{
			int startIdx = I(iRow, 0);
			m_afEntry[startIdx]     = Vector[0];
			m_afEntry[startIdx + 1] = Vector[1];
			m_afEntry[startIdx + 2] = Vector[2];
			m_afEntry[startIdx + 3] = Vector[3];
		}

		void Matrix4f::SetRow(int iRow, const Vector3f& Vector)
		{
			int startIdx = I(iRow, 0);
			m_afEntry[startIdx]     = Vector[0];
			m_afEntry[startIdx + 1] = Vector[1];
			m_afEntry[startIdx + 2] = Vector[2];
		}

		Vector4f Matrix4f::GetRow(int iRow) const
		{
			int startIdx = I(iRow, 0);
			return Vector4f(m_afEntry[startIdx], m_afEntry[startIdx + 1],
			               m_afEntry[startIdx + 2], m_afEntry[startIdx + 3]);
		}

		void Matrix4f::SetColumn(int iCol, const Vector4f& Vector)
		{
			int startIdx = I(0, iCol);
			m_afEntry[startIdx]      = Vector[0];
			m_afEntry[startIdx + 4]  = Vector[1];
			m_afEntry[startIdx + 8]  = Vector[2];
			m_afEntry[startIdx + 12] = Vector[3];
		}

		Vector4f Matrix4f::GetColumn(int iCol) const
		{
			int startIdx = I(0, iCol);
			return Vector4f(m_afEntry[startIdx], m_afEntry[startIdx + 4],
			               m_afEntry[startIdx + 8], m_afEntry[startIdx + 12]);
		}
	}
}
