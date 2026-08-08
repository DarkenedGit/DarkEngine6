#include "Vector2f.h"
#include <cmath>
#include <cstdlib>
#include <limits>

namespace Dark
{
	namespace Math
	{
		const Vect2f Vect2f::ZERO	= { 0.0f, 0.0f };
		const Vect2f Vect2f::ONE	= { 1.0f, 1.0f };
		const Vect2f Vect2f::QNAN	= { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN() };
		const Vect2f Vect2f::X_AXIS = { 1.0f, 0.0f };
		const Vect2f Vect2f::Y_AXIS = { 0.0f, 1.0f };

		Vector2f::Vector2f()
		{}

		Vector2f::Vector2f(float X, float Y)
		{
			x = X;
			y = Y;
		}

		Vector2f::Vector2f(const Vector2f& Vector) noexcept
		{
			x = Vector.x;
			y = Vector.y;
		}

		Vector2f::Vector2f(const Vect2f& Vector)
		{
			x = Vector.x;
			y = Vector.y;
		}

		Vector2f& Vector2f::operator= (const Vector2f& Vector) noexcept
		{
			x = Vector.x;
			y = Vector.y;

			return(*this);
		}

		Vector2f& Vector2f::operator=(const Vect2f& Vector)
		{
			x = Vector.x;
			y = Vector.y;

			return(*this);
		}

		void Vector2f::Normalize()
		{
			float fInvMag = (1.0f / Magnitude());

			x *= fInvMag;
			y *= fInvMag;
		}

		float Vector2f::Magnitude() const
		{
			float fLength = 0.0f;

			fLength += x * x;
			fLength += y * y;

			return(sqrtf(fLength));
		}

		float Vector2f::MagnitudeSqrd() const
		{
			float fLength = 0.0f;

			fLength += x * x;
			fLength += y * y;

			return(fLength);
		}

		void Vector2f::Clamp()
		{
			if (x > 1.0f)
				x = 1.0f;
			if (x < 0.0f)
				x = 0.0f;

			if (y > 1.0f)
				y = 1.0f;
			if (y < 0.0f)
				y = 0.0f;
		}

		bool Vector2f::IsNaN() const
		{
			return std::isnan(x) || std::isnan(y);
		}

		float Vector2f::Cross(const Vector2f& Vector) const
		{
			return x * Vector.y - y * Vector.x;
		}

		Vector2f Vector2f::Perpendicular() const
		{
			return Vector2f(-y, x);
		}

		Vector2f Vector2f::RandUnitPoint()
		{
			float rx = static_cast<float>((double)rand() / RAND_MAX) * 2.0f - 1.0f;
			float ry = static_cast<float>((double)rand() / RAND_MAX) * 2.0f - 1.0f;
			Vector2f v(rx, ry);
			v.Normalize();
			return v;
		}

		Vector2f Vector2f::RandomPoint()
		{
			float rx = static_cast<float>((double)rand() / RAND_MAX) * 2.0f - 1.0f;
			float ry = static_cast<float>((double)rand() / RAND_MAX) * 2.0f - 1.0f;
			return Vector2f(rx, ry);
		}

		void Vector2f::Min(const Vector2f& Vector)
		{
			if (Vector.x < x)
				x = Vector.x;

			if (Vector.y < y)
				y = Vector.y;
		}

		void Vector2f::Max(const Vector2f& Vector)
		{
			if (Vector.x > x)
				x = Vector.x;

			if (Vector.y > y)
				y = Vector.y;
		}

		float Vector2f::Dot(const Vect2f& Vector) const
		{
			return x * Vector.x + y * Vector.y;
		}

		Vector2f Vector2f::Reflect(const Vector2f& Normal)
		{
			return Vector2f(*this - (Normal * (2 * this->Dot(Normal))));
		}

		float Vector2f::operator[] (int iPos) const
		{
			if (iPos == 0) return(x);
			return(y);
		}

		float& Vector2f::operator[] (int iPos)
		{
			if (iPos == 0) return(x);
			return(y);
		}

		bool Vector2f::operator== (const Vector2f& Vector) const
		{
			if ((x - Vector.x) * (x - Vector.x) > 0.01f)
				return false;
			if ((y - Vector.y) * (y - Vector.y) > 0.01f)
				return false;

			return(true);
		}

		bool Vector2f::operator!= (const Vector2f& Vector) const
		{
			return(!(*this == Vector));
		}

		Vector2f Vector2f::operator+ (const Vector2f& Vector) const
		{
			Vector2f sum;

			sum.x = x + Vector.x;
			sum.y = y + Vector.y;

			return(sum);
		}

		Vector2f Vector2f::operator- (const Vector2f& Vector) const
		{
			Vector2f diff;

			diff.x = x - Vector.x;
			diff.y = y - Vector.y;

			return(diff);
		}

		Vector2f Vector2f::operator* (const Vector2f& Vector) const
		{
			Vector2f prod;

			prod.x = x * Vector.x;
			prod.y = y * Vector.y;

			return(prod);
		}

		Vector2f Vector2f::operator+(float fScalar) const
		{
			return Vector2f(x + fScalar, y + fScalar);
		}

		Vector2f Vector2f::operator-(float fScalar) const
		{
			return Vector2f(x - fScalar, y - fScalar);
		}

		Vector2f Vector2f::operator* (float fScalar) const
		{
			Vector2f prod;

			prod.x = x * fScalar;
			prod.y = y * fScalar;

			return(prod);
		}

		Vector2f Vector2f::operator/ (float fScalar) const
		{
			Vector2f quot;
			if (fScalar != 0.0f)
			{
				float fInvScalar = 1.0f / fScalar;
				quot.x = x * fInvScalar;
				quot.y = y * fInvScalar;
			}
			else
			{
				quot = ZERO;
			}

			return(quot);
		}

		Vector2f Vector2f::operator- () const
		{
			Vector2f neg;

			neg.x = -x;
			neg.y = -y;

			return(neg);
		}

		Vector2f& Vector2f::operator+= (const Vector2f& Vector)
		{
			x += Vector.x;
			y += Vector.y;

			return(*this);
		}

		Vector2f& Vector2f::operator-= (const Vector2f& Vector)
		{
			x -= Vector.x;
			y -= Vector.y;

			return(*this);
		}


		Vector2f& Vector2f::operator*= (const Vector2f& Vector)
		{
			x *= Vector.x;
			y *= Vector.y;

			return(*this);
		}

		Vector2f& Vector2f::operator+=(float fScalar)
		{
			x += fScalar;
			y += fScalar;
			return (*this);
		}

		Vector2f& Vector2f::operator-=(float fScalar)
		{
			x -= fScalar;
			y -= fScalar;
			return (*this);
		}

		Vector2f& Vector2f::operator*= (float fScalar)
		{
			x *= fScalar;
			y *= fScalar;

			return(*this);
		}

		Vector2f& Vector2f::operator/= (float fScalar)
		{
			if (fScalar != 0.0f)
			{
				float fInvScalar = 1.0f / fScalar;
				x *= fInvScalar;
				y *= fInvScalar;
			}
			else
			{
				*this = ZERO;
			}

			return(*this);
		}
	}
}