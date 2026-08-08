#include "Plane4f.h"
#include "MathDefines.h"
#include <cmath>

namespace Dark
{
	namespace Math
	{
		Plane4f::Plane4f()
		{
			x = 0.0f;
			y = 1.0f;
			z = 0.0f;
			w = 0.0f;
		}

		Plane4f::Plane4f(float nx, float ny, float nz, float d)
		{
			x = nx;
			y = ny;
			z = nz;
			w = d;
		}

		Plane4f::Plane4f(const Vector3f& normal, float d)
		{
			x = normal.x;
			y = normal.y;
			z = normal.z;
			w = d;
		}

		Plane4f::Plane4f(const Vector3f& normal, const Vector3f& pointOnPlane)
		{
			Vector3f n = normal;
			n.Normalize();
			x = n.x;
			y = n.y;
			z = n.z;
			w = -n.Dot(pointOnPlane);
		}

		Plane4f::Plane4f(const Vector3f& p0, const Vector3f& p1, const Vector3f& p2)
		{
			Vector3f n = (p1 - p0).Cross(p2 - p0);
			n.Normalize();
			x = n.x;
			y = n.y;
			z = n.z;
			w = -n.Dot(p0);
		}

		Plane4f::Plane4f(const Vect4f& plane)
		{
			x = plane.x;
			y = plane.y;
			z = plane.z;
			w = plane.w;
		}

		Plane4f::Plane4f(const Plane4f& plane)
		{
			x = plane.x;
			y = plane.y;
			z = plane.z;
			w = plane.w;
		}

		Plane4f& Plane4f::operator=(const Plane4f& plane)
		{
			x = plane.x;
			y = plane.y;
			z = plane.z;
			w = plane.w;
			return *this;
		}

		Plane4f& Plane4f::operator=(const Vect4f& plane)
		{
			x = plane.x;
			y = plane.y;
			z = plane.z;
			w = plane.w;
			return *this;
		}

		float Plane4f::operator[](int iPos) const
		{
			if (iPos == 0) return x;
			if (iPos == 1) return y;
			if (iPos == 2) return z;
			return w;
		}

		float& Plane4f::operator[](int iPos)
		{
			if (iPos == 0) return x;
			if (iPos == 1) return y;
			if (iPos == 2) return z;
			return w;
		}

		bool Plane4f::operator==(const Plane4f& plane) const
		{
			if ((x - plane.x) * (x - plane.x) > 0.01f)
				return false;
			if ((y - plane.y) * (y - plane.y) > 0.01f)
				return false;
			if ((z - plane.z) * (z - plane.z) > 0.01f)
				return false;
			if ((w - plane.w) * (w - plane.w) > 0.01f)
				return false;
			return true;
		}

		bool Plane4f::operator!=(const Plane4f& plane) const
		{
			return !(*this == plane);
		}

		Plane4f Plane4f::operator-() const
		{
			return Plane4f(-x, -y, -z, -w);
		}

		Plane4f Plane4f::operator*(float fScalar) const
		{
			return Plane4f(x * fScalar, y * fScalar, z * fScalar, w * fScalar);
		}

		Plane4f Plane4f::operator/(float fScalar) const
		{
			if (fScalar != 0.0f)
			{
				float inv = 1.0f / fScalar;
				return Plane4f(x * inv, y * inv, z * inv, w * inv);
			}
			return Plane4f();
		}

		Plane4f& Plane4f::operator*=(float fScalar)
		{
			x *= fScalar;
			y *= fScalar;
			z *= fScalar;
			w *= fScalar;
			return *this;
		}

		Plane4f& Plane4f::operator/=(float fScalar)
		{
			if (fScalar != 0.0f)
			{
				float inv = 1.0f / fScalar;
				x *= inv;
				y *= inv;
				z *= inv;
				w *= inv;
			}
			return *this;
		}

		Vector3f Plane4f::Normal() const
		{
			return Vector3f(x, y, z);
		}

		void Plane4f::SetNormal(const Vector3f& n)
		{
			x = n.x;
			y = n.y;
			z = n.z;
		}

		void Plane4f::Normalize()
		{
			float mag = sqrtf(x * x + y * y + z * z);
			if (mag > Epsilon)
			{
				float inv = 1.0f / mag;
				x *= inv;
				y *= inv;
				z *= inv;
				w *= inv;
			}
		}

		float Plane4f::DistanceToPoint(const Vector3f& pt) const
		{
			return x * pt.x + y * pt.y + z * pt.z + w;
		}

		int Plane4f::ClassifyPoint(const Vector3f& pt, float epsilon) const
		{
			float d = DistanceToPoint(pt);
			if (d > epsilon)
				return 1;
			if (d < -epsilon)
				return -1;
			return 0;
		}
	}
}
