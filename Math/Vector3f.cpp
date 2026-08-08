#include "Vector3f.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace Dark
{
	namespace Math
	{
		const Vect3f Vect3f::ZERO	= { 0.0f, 0.0f, 0.0f };
		const Vect3f Vect3f::ONE	= { 1.0f, 1.0f, 1.0f };
		const Vect3f Vect3f::X_AXIS = { 1.0f, 0.0f, 0.0f };
		const Vect3f Vect3f::Y_AXIS = { 0.0f, 1.0f, 0.0f };
		const Vect3f Vect3f::Z_AXIS = { 0.0f, 0.0f, 1.0f };

		Vector3f::Vector3f()
		{
		}

		Vector3f::Vector3f(float X, float Y, float Z)
		{
			x = X;
			y = Y;
			z = Z;
		}

		Vector3f::Vector3f(const Vector3f& Vector) noexcept
		{
			x = Vector.x;
			y = Vector.y;
			z = Vector.z;
		}

		Vector3f::Vector3f(const Vect3f& Vector)
		{
			x = Vector.x;
			y = Vector.y;
			z = Vector.z;
		}

		void Vector3f::Normalize()
		{
			float Mag = Magnitude();
			if (Mag == 0.0f)
				Mag = 0.0001f;

			float fInvMag = (1.0f / Mag);

			x *= fInvMag;
			y *= fInvMag;
			z *= fInvMag;
		}

		float Vector3f::Magnitude() const
		{
			float fLength = 0.0f;

			fLength += x * x;
			fLength += y * y;
			fLength += z * z;

			return (sqrtf(fLength));
		}

		float Vector3f::MagnitudeSqrd() const
		{
			float fLength = 0.0f;

			fLength += x * x;
			fLength += y * y;
			fLength += z * z;

			return (fLength);
		}

		Vector3f Vector3f::Perpendicular()
		{
			float xAbs = fabsf(x);
			float yAbs = fabsf(y);
			float zAbs = fabsf(z);
			float minVal = std::min(std::min(xAbs, yAbs), zAbs);

			if (xAbs == minVal)
				return Cross(Vector3f::X_AXIS);
			else if (yAbs == minVal)
				return Cross(Vector3f::Y_AXIS);
			else
				return Cross(Vector3f::Z_AXIS);
		}

		Vector3f Vector3f::Cross(const Vector3f& Vector) const
		{
			Vector3f vRet;

			vRet.x = y * Vector.z - z * Vector.y;
			vRet.y = z * Vector.x - x * Vector.z;
			vRet.z = x * Vector.y - y * Vector.x;

			return (vRet);
		}

		float Vector3f::Dot(const Vector3f& Vector) const
		{
			float ret = 0.0f;

			ret  = x * Vector.x;
			ret += y * Vector.y;
			ret += z * Vector.z;

			return ret;
		}

		void Vector3f::Clamp()
		{
			if (x > 1.0f) x = 1.0f;
			if (x < 0.0f) x = 0.0f;

			if (y > 1.0f) y = 1.0f;
			if (y < 0.0f) y = 0.0f;

			if (z > 1.0f) z = 1.0f;
			if (z < 0.0f) z = 0.0f;
		}

		Vector3f Vector3f::RandUnitPoint()
		{
			float rx = static_cast<float>((double)rand() / RAND_MAX) * 2.0f - 1.0f;
			float ry = static_cast<float>((double)rand() / RAND_MAX) * 2.0f - 1.0f;
			float rz = static_cast<float>((double)rand() / RAND_MAX) * 2.0f - 1.0f;

			Vector3f random = Vector3f(rx, ry, rz);
			random.Normalize();

			return (random);
		}

		Vector3f Vector3f::RandomPoint()
		{
			float rx = static_cast<float>((double)rand() / RAND_MAX) * 2.0f - 1.0f;
			float ry = static_cast<float>((double)rand() / RAND_MAX) * 2.0f - 1.0f;
			float rz = static_cast<float>((double)rand() / RAND_MAX) * 2.0f - 1.0f;

			return Vector3f(rx, ry, rz);
		}

		bool Vector3f::IsNaN()
		{
			return std::isnan(x) || std::isnan(y) || std::isnan(z);
		}

		Vector3f& Vector3f::operator=(const Vector3f& Vector) noexcept
		{
			x = Vector.x;
			y = Vector.y;
			z = Vector.z;

			return (*this);
		}

		Vector3f& Vector3f::operator=(const Vect3f& Vector)
		{
			x = Vector.x;
			y = Vector.y;
			z = Vector.z;

			return (*this);
		}

		float Vector3f::operator[](int iPos) const
		{
			if (iPos == 0) return (x);
			if (iPos == 1) return (y);
			return (z);
		}

		float& Vector3f::operator[](int iPos)
		{
			if (iPos == 0) return (x);
			if (iPos == 1) return (y);
			return (z);
		}

		bool Vector3f::operator==(const Vector3f& Vector) const
		{
			if ((x - Vector.x) * (x - Vector.x) > 0.01f)
				return false;
			if ((y - Vector.y) * (y - Vector.y) > 0.01f)
				return false;
			if ((z - Vector.z) * (z - Vector.z) > 0.01f)
				return false;

			return true;
		}

		bool Vector3f::operator!=(const Vector3f& Vector) const
		{
			return (!(*this == Vector));
		}

		Vector3f Vector3f::operator+(const Vector3f& Vector) const
		{
			Vector3f sum;

			sum.x = x + Vector.x;
			sum.y = y + Vector.y;
			sum.z = z + Vector.z;

			return (sum);
		}

		Vector3f Vector3f::operator-(const Vector3f& Vector) const
		{
			Vector3f diff;

			diff.x = x - Vector.x;
			diff.y = y - Vector.y;
			diff.z = z - Vector.z;

			return (diff);
		}

		Vector3f Vector3f::operator*(float fScalar) const
		{
			Vector3f prod;

			prod.x = x * fScalar;
			prod.y = y * fScalar;
			prod.z = z * fScalar;

			return (prod);
		}

		Vector3f Vector3f::operator*(const Vector3f& Vector) const
		{
			Vector3f prod;

			prod.x = x * Vector.x;
			prod.y = y * Vector.y;
			prod.z = z * Vector.z;

			return (prod);
		}

		Vector3f Vector3f::operator+(float fScalar) const
		{
			return Vector3f(x + fScalar, y + fScalar, z + fScalar);
		}

		Vector3f Vector3f::operator-(float fScalar) const
		{
			return Vector3f(x - fScalar, y - fScalar, z - fScalar);
		}

		Vector3f Vector3f::operator/(float fScalar) const
		{
			Vector3f quot;
			if (fScalar != 0.0f)
			{
				float fInvScalar = 1.0f / fScalar;
				quot.x = x * fInvScalar;
				quot.y = y * fInvScalar;
				quot.z = z * fInvScalar;
			}
			else
			{
				quot = ZERO;
			}

			return (quot);
		}

		Vector3f Vector3f::operator/(const Vector3f& Vector) const
		{
			Vector3f quot;
			quot.x = Vector.x != 0.0f ? x / Vector.x : 0.0f;
			quot.y = Vector.y != 0.0f ? y / Vector.y : 0.0f;
			quot.z = Vector.z != 0.0f ? z / Vector.z : 0.0f;

			return (quot);
		}

		Vector3f Vector3f::operator-() const
		{
			Vector3f neg;

			neg.x = -x;
			neg.y = -y;
			neg.z = -z;

			return (neg);
		}

		Vector3f& Vector3f::operator+=(const Vector3f& Vector)
		{
			x += Vector.x;
			y += Vector.y;
			z += Vector.z;

			return (*this);
		}

		Vector3f& Vector3f::operator-=(const Vector3f& Vector)
		{
			x -= Vector.x;
			y -= Vector.y;
			z -= Vector.z;

			return (*this);
		}

		Vector3f& Vector3f::operator*=(float fScalar)
		{
			x *= fScalar;
			y *= fScalar;
			z *= fScalar;

			return (*this);
		}

		Vector3f& Vector3f::operator*=(const Vector3f& Vector)
		{
			x *= Vector.x;
			y *= Vector.y;
			z *= Vector.z;

			return (*this);
		}

		Vector3f& Vector3f::operator+=(float fScalar)
		{
			x += fScalar;
			y += fScalar;
			z += fScalar;
			return (*this);
		}

		Vector3f& Vector3f::operator-=(float fScalar)
		{
			x -= fScalar;
			y -= fScalar;
			z -= fScalar;
			return (*this);
		}

		Vector3f& Vector3f::operator/=(float fScalar)
		{
			if (fScalar != 0.0f)
			{
				float fInvScalar = 1.0f / fScalar;
				x *= fInvScalar;
				y *= fInvScalar;
				z *= fInvScalar;
			}
			else
			{
				*this = ZERO;
			}

			return (*this);
		}

		Vector3f& Vector3f::operator/=(const Vector3f& Vector)
		{
			x = Vector.x != 0.0f ? x / Vector.x : 0.0f;
			y = Vector.y != 0.0f ? y / Vector.y : 0.0f;
			z = Vector.z != 0.0f ? z / Vector.z : 0.0f;

			return (*this);
		}
	}
}
