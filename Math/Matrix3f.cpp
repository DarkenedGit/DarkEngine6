#include "Matrix3f.h"
#include <cmath>
#include <cstring>

namespace Dark
{
	namespace Math
	{
		const Mat3f Mat3f::ZERO     = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
		const Mat3f Mat3f::IDENTITY = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };

		Matrix3f::Matrix3f()
		{
			*this = IDENTITY;
		}

		Matrix3f::Matrix3f(const Matrix3f& Matrix)
		{
			memcpy(m_afEntry, Matrix.m_afEntry, 9 * sizeof(float));
		}

		Matrix3f::Matrix3f(const Mat3f& Matrix)
		{
			memcpy(m_afEntry, Matrix.m_afEntry, 9 * sizeof(float));
		}

		Matrix3f::Matrix3f(float fM11, float fM12, float fM13,
		                   float fM21, float fM22, float fM23,
		                   float fM31, float fM32, float fM33)
		{
			m_afEntry[m11] = fM11;
			m_afEntry[m12] = fM12;
			m_afEntry[m13] = fM13;

			m_afEntry[m21] = fM21;
			m_afEntry[m22] = fM22;
			m_afEntry[m23] = fM23;

			m_afEntry[m31] = fM31;
			m_afEntry[m32] = fM32;
			m_afEntry[m33] = fM33;
		}

		Matrix3f& Matrix3f::operator=(const Matrix3f& Matrix)
		{
			memcpy(m_afEntry, Matrix.m_afEntry, 9 * sizeof(float));
			return *this;
		}

		Matrix3f& Matrix3f::operator=(const Mat3f& Matrix)
		{
			memcpy(m_afEntry, Matrix.m_afEntry, 9 * sizeof(float));
			return *this;
		}

		void Matrix3f::RotationX(float fRadians)
		{
			float fSin = sinf(fRadians);
			float fCos = cosf(fRadians);

			m_afEntry[m11] = 1.0f;
			m_afEntry[m12] = 0.0f;
			m_afEntry[m13] = 0.0f;

			m_afEntry[m21] = 0.0f;
			m_afEntry[m22] = fCos;
			m_afEntry[m23] = fSin;

			m_afEntry[m31] = 0.0f;
			m_afEntry[m32] = -fSin;
			m_afEntry[m33] = fCos;
		}

		void Matrix3f::RotationY(float fRadians)
		{
			float fSin = sinf(fRadians);
			float fCos = cosf(fRadians);

			m_afEntry[m11] = fCos;
			m_afEntry[m12] = 0.0f;
			m_afEntry[m13] = -fSin;

			m_afEntry[m21] = 0.0f;
			m_afEntry[m22] = 1.0f;
			m_afEntry[m23] = 0.0f;

			m_afEntry[m31] = fSin;
			m_afEntry[m32] = 0.0f;
			m_afEntry[m33] = fCos;
		}

		void Matrix3f::RotationZ(float fRadians)
		{
			float fSin = sinf(fRadians);
			float fCos = cosf(fRadians);

			m_afEntry[m11] = fCos;
			m_afEntry[m12] = fSin;
			m_afEntry[m13] = 0.0f;

			m_afEntry[m21] = -fSin;
			m_afEntry[m22] = fCos;
			m_afEntry[m23] = 0.0f;

			m_afEntry[m31] = 0.0f;
			m_afEntry[m32] = 0.0f;
			m_afEntry[m33] = 1.0f;
		}

		void Matrix3f::Rotation(const Vector3f& Rot)
		{
			Matrix3f mRot1;
			Matrix3f mRot2;

			mRot1.RotationZ(Rot.z);
			mRot2.RotationX(Rot.x);
			mRot1 *= mRot2;
			mRot2.RotationY(Rot.y);
			mRot1 *= mRot2;
			*this = mRot1;
		}

		void Matrix3f::RotationEuler(const Vector3f& Axis, float Angle)
		{
			float s = sinf(Angle);
			float c = cosf(Angle);
			float t = 1.0f - c;

			m_afEntry[m11] = t * Axis.x * Axis.x + c;
			m_afEntry[m12] = t * Axis.x * Axis.y + s * Axis.z;
			m_afEntry[m13] = t * Axis.x * Axis.z - s * Axis.y;

			m_afEntry[m21] = t * Axis.x * Axis.y - s * Axis.z;
			m_afEntry[m22] = t * Axis.y * Axis.y + c;
			m_afEntry[m23] = t * Axis.y * Axis.z + s * Axis.x;

			m_afEntry[m31] = t * Axis.x * Axis.z + s * Axis.y;
			m_afEntry[m32] = t * Axis.y * Axis.z - s * Axis.x;
			m_afEntry[m33] = t * Axis.z * Axis.z + c;
		}

		void Matrix3f::Orthonormalize()
		{
			// Gram-Schmidt (Wild Magic / Geometric Tools style)

			// compute q0
			float fInvLength = static_cast<float>(1.0 / sqrt((double)(m_afEntry[0] * m_afEntry[0]
			                                                         + m_afEntry[3] * m_afEntry[3]
			                                                         + m_afEntry[6] * m_afEntry[6])));

			m_afEntry[0] *= fInvLength;
			m_afEntry[3] *= fInvLength;
			m_afEntry[6] *= fInvLength;

			// compute q1
			float fDot0 = m_afEntry[0] * m_afEntry[1] + m_afEntry[3] * m_afEntry[4] + m_afEntry[6] * m_afEntry[7];

			m_afEntry[1] -= fDot0 * m_afEntry[0];
			m_afEntry[4] -= fDot0 * m_afEntry[3];
			m_afEntry[7] -= fDot0 * m_afEntry[6];

			fInvLength = static_cast<float>(1.0 / sqrt((double)(m_afEntry[1] * m_afEntry[1]
			                                                     + m_afEntry[4] * m_afEntry[4]
			                                                     + m_afEntry[7] * m_afEntry[7])));

			m_afEntry[1] *= fInvLength;
			m_afEntry[4] *= fInvLength;
			m_afEntry[7] *= fInvLength;

			// compute q2
			float fDot1 = m_afEntry[1] * m_afEntry[2] + m_afEntry[4] * m_afEntry[5] + m_afEntry[7] * m_afEntry[8];
			fDot0 = m_afEntry[0] * m_afEntry[2] + m_afEntry[3] * m_afEntry[5] + m_afEntry[6] * m_afEntry[8];

			m_afEntry[2] -= fDot0 * m_afEntry[0] + fDot1 * m_afEntry[1];
			m_afEntry[5] -= fDot0 * m_afEntry[3] + fDot1 * m_afEntry[4];
			m_afEntry[8] -= fDot0 * m_afEntry[6] + fDot1 * m_afEntry[7];

			fInvLength = static_cast<float>(1.0 / sqrt((double)(m_afEntry[2] * m_afEntry[2]
			                                                     + m_afEntry[5] * m_afEntry[5]
			                                                     + m_afEntry[8] * m_afEntry[8])));

			m_afEntry[2] *= fInvLength;
			m_afEntry[5] *= fInvLength;
			m_afEntry[8] *= fInvLength;
		}

		void Matrix3f::Scale(float fScale)
		{
			m_afEntry[m11] = fScale;
			m_afEntry[m12] = 0.0f;
			m_afEntry[m13] = 0.0f;

			m_afEntry[m21] = 0.0f;
			m_afEntry[m22] = fScale;
			m_afEntry[m23] = 0.0f;

			m_afEntry[m31] = 0.0f;
			m_afEntry[m32] = 0.0f;
			m_afEntry[m33] = fScale;
		}

		void Matrix3f::Scale(float fXScale, float fYScale, float fZScale)
		{
			m_afEntry[m11] = fXScale;
			m_afEntry[m12] = 0.0f;
			m_afEntry[m13] = 0.0f;

			m_afEntry[m21] = 0.0f;
			m_afEntry[m22] = fYScale;
			m_afEntry[m23] = 0.0f;

			m_afEntry[m31] = 0.0f;
			m_afEntry[m32] = 0.0f;
			m_afEntry[m33] = fZScale;
		}

		float Matrix3f::operator[](int iPos) const
		{
			return m_afEntry[iPos];
		}

		float& Matrix3f::operator[](int iPos)
		{
			return m_afEntry[iPos];
		}

		float Matrix3f::operator()(int iRow, int iCol) const
		{
			return m_afEntry[I(iRow, iCol)];
		}

		float& Matrix3f::operator()(int iRow, int iCol)
		{
			return m_afEntry[I(iRow, iCol)];
		}

		bool Matrix3f::operator==(const Matrix3f& Matrix) const
		{
			return memcmp(m_afEntry, Matrix.m_afEntry, 9 * sizeof(float)) == 0;
		}

		bool Matrix3f::operator!=(const Matrix3f& Matrix) const
		{
			return memcmp(m_afEntry, Matrix.m_afEntry, 9 * sizeof(float)) != 0;
		}

		Matrix3f& Matrix3f::operator+=(const Matrix3f& Matrix)
		{
			for (int i = 0; i < 9; i++)
				m_afEntry[i] += Matrix.m_afEntry[i];
			return *this;
		}

		Matrix3f& Matrix3f::operator-=(const Matrix3f& Matrix)
		{
			for (int i = 0; i < 9; i++)
				m_afEntry[i] -= Matrix.m_afEntry[i];
			return *this;
		}

		Matrix3f& Matrix3f::operator*=(float fScalar)
		{
			for (int i = 0; i < 9; i++)
				m_afEntry[i] *= fScalar;
			return *this;
		}

		Matrix3f& Matrix3f::operator*=(const Matrix3f& Matrix)
		{
			Matrix3f mProd(*this);
			*this = ZERO;

			for (int iRow = 0; iRow < 3; iRow++)
			{
				for (int iCol = 0; iCol < 3; iCol++)
				{
					int i = I(iRow, iCol);
					m_afEntry[i] += mProd.m_afEntry[I(iRow, 0)] * Matrix.m_afEntry[I(0, iCol)];
					m_afEntry[i] += mProd.m_afEntry[I(iRow, 1)] * Matrix.m_afEntry[I(1, iCol)];
					m_afEntry[i] += mProd.m_afEntry[I(iRow, 2)] * Matrix.m_afEntry[I(2, iCol)];
				}
			}
			return *this;
		}

		Matrix3f& Matrix3f::operator/=(float fScalar)
		{
			if (fScalar != 0.0f)
			{
				float fInvScalar = 1.0f / fScalar;
				for (int i = 0; i < 9; i++)
					m_afEntry[i] *= fInvScalar;
			}
			else
			{
				*this = ZERO;
			}
			return *this;
		}

		void Matrix3f::Transpose()
		{
			Matrix3f mTranspose;

			for (int iRow = 0; iRow < 3; iRow++)
			{
				for (int iCol = 0; iCol < 3; iCol++)
					mTranspose.m_afEntry[I(iRow, iCol)] = m_afEntry[I(iCol, iRow)];
			}

			memcpy(m_afEntry, mTranspose.m_afEntry, 9 * sizeof(float));
		}

		int Matrix3f::I(int iRow, int iCol)
		{
			return 3 * iRow + iCol;
		}

		Vector3f Matrix3f::operator*(const Vector3f& Vector) const
		{
			Vector3f vProd;
			for (int iCol = 0; iCol < 3; iCol++)
			{
				vProd[iCol] = 0.0f;
				for (int iRow = 0; iRow < 3; iRow++)
					vProd[iCol] += m_afEntry[I(iRow, iCol)] * Vector[iRow];
			}
			return vProd;
		}

		void Matrix3f::SetRow(int iRow, const Vector3f& Vector)
		{
			int startIdx = I(iRow, 0);
			m_afEntry[startIdx]     = Vector[0];
			m_afEntry[startIdx + 1] = Vector[1];
			m_afEntry[startIdx + 2] = Vector[2];
		}

		Vector3f Matrix3f::GetRow(int iRow) const
		{
			int startIdx = I(iRow, 0);
			return Vector3f(m_afEntry[startIdx], m_afEntry[startIdx + 1], m_afEntry[startIdx + 2]);
		}

		void Matrix3f::SetColumn(int iCol, const Vector3f& Vector)
		{
			int startIdx = I(0, iCol);
			m_afEntry[startIdx]     = Vector[0];
			m_afEntry[startIdx + 3] = Vector[1];
			m_afEntry[startIdx + 6] = Vector[2];
		}

		Vector3f Matrix3f::GetColumn(int iCol) const
		{
			int startIdx = I(0, iCol);
			return Vector3f(m_afEntry[startIdx], m_afEntry[startIdx + 3], m_afEntry[startIdx + 6]);
		}
	}
}
