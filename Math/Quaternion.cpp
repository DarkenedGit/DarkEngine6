#include "Quaternion.h"
#include "MathDefines.h"
#include <cmath>

namespace Dark
{
	namespace Math
	{
		const Quaternion Quaternion::IDENTITY = Quaternion(1.0f, 0.0f, 0.0f, 0.0f);

		Quaternion::Quaternion(): w(1.0f), x(0.0f), y(0.0f), z(0.0f)
		{
		}

		Quaternion::Quaternion(float W, float X, float Y, float Z): w(W), x(X), y(Y), z(Z)
		{
		}

		Quaternion::Quaternion(const Quaternion& q): w(q.w), x(q.x), y(q.y), z(q.z)
		{
		}

		Quaternion& Quaternion::operator=(const Quaternion& q)
		{
			w = q.w;
			x = q.x;
			y = q.y;
			z = q.z;
			return *this;
		}

		float Quaternion::Length() const
		{
			return sqrtf(w * w + x * x + y * y + z * z);
		}

		float Quaternion::LengthSquared() const
		{
			return w * w + x * x + y * y + z * z;
		}

		float Quaternion::Dot(const Quaternion& a) const
		{
			return w * a.w + x * a.x + y * a.y + z * a.z;
		}

		void Quaternion::Normalize()
		{
			float len = Length();
			if (len > Epsilon)
			{
				float inv = 1.0f / len;
				w *= inv;
				x *= inv;
				y *= inv;
				z *= inv;
			}
			else
			{
				*this = IDENTITY;
			}
		}

		Quaternion Quaternion::Conjugate() const
		{
			return Quaternion(w, -x, -y, -z);
		}

		Quaternion Quaternion::Inverse() const
		{
			float lenSq = LengthSquared();
			if (lenSq <= Epsilon)
				return IDENTITY;

			float inv = 1.0f / lenSq;
			return Quaternion(w * inv, -x * inv, -y * inv, -z * inv);
		}

		Vector3f Quaternion::Rotate(const Vector3f& v) const
		{
			// q * (0,v) * q^-1 for unit quaternion
			Quaternion p(0.0f, v.x, v.y, v.z);
			Quaternion r = (*this) * p * Conjugate();
			return Vector3f(r.x, r.y, r.z);
		}

		Matrix3f Quaternion::ToMatrix3() const
		{
			float xx = x * x;
			float yy = y * y;
			float zz = z * z;
			float xy = x * y;
			float xz = x * z;
			float yz = y * z;
			float wx = w * x;
			float wy = w * y;
			float wz = w * z;

			return Matrix3f(
				1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),       2.0f * (xz - wy),
				2.0f * (xy - wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),
				2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy));
		}

		Matrix4f Quaternion::ToMatrix4() const
		{
			Matrix3f r = ToMatrix3();
			Matrix4f m;
			m.SetRotation(r);
			m.SetTranslation(Vector3f::ZERO);
			m.m_afEntry[Mat4f::m44] = 1.0f;
			return m;
		}

		Quaternion Quaternion::FromAxisAngle(const Vector3f& axis, float radians)
		{
			Vector3f n = axis;
			n.Normalize();
			float half = radians * 0.5f;
			float s = sinf(half);
			return Quaternion(cosf(half), n.x * s, n.y * s, n.z * s);
		}

		Quaternion Quaternion::FromEulerXYZ(float pitchX, float yawY, float rollZ)
		{
			// Intrinsic XYZ: pitch (X), yaw (Y), roll (Z)
			float hx = pitchX * 0.5f;
			float hy = yawY * 0.5f;
			float hz = rollZ * 0.5f;

			float cx = cosf(hx);
			float sx = sinf(hx);
			float cy = cosf(hy);
			float sy = sinf(hy);
			float cz = cosf(hz);
			float sz = sinf(hz);

			Quaternion q;
			q.w = cx * cy * cz + sx * sy * sz;
			q.x = sx * cy * cz - cx * sy * sz;
			q.y = cx * sy * cz + sx * cy * sz;
			q.z = cx * cy * sz - sx * sy * cz;
			return q;
		}

		Quaternion Quaternion::FromMatrix3(const Matrix3f& m)
		{
			// Shepperd's method
			float trace = m(0, 0) + m(1, 1) + m(2, 2);
			Quaternion q;

			if (trace > 0.0f)
			{
				float s = sqrtf(trace + 1.0f) * 2.0f;
				q.w = 0.25f * s;
				q.x = (m(1, 2) - m(2, 1)) / s;
				q.y = (m(2, 0) - m(0, 2)) / s;
				q.z = (m(0, 1) - m(1, 0)) / s;
			}
			else if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2))
			{
				float s = sqrtf(1.0f + m(0, 0) - m(1, 1) - m(2, 2)) * 2.0f;
				q.w = (m(1, 2) - m(2, 1)) / s;
				q.x = 0.25f * s;
				q.y = (m(0, 1) + m(1, 0)) / s;
				q.z = (m(0, 2) + m(2, 0)) / s;
			}
			else if (m(1, 1) > m(2, 2))
			{
				float s = sqrtf(1.0f + m(1, 1) - m(0, 0) - m(2, 2)) * 2.0f;
				q.w = (m(2, 0) - m(0, 2)) / s;
				q.x = (m(0, 1) + m(1, 0)) / s;
				q.y = 0.25f * s;
				q.z = (m(1, 2) + m(2, 1)) / s;
			}
			else
			{
				float s = sqrtf(1.0f + m(2, 2) - m(0, 0) - m(1, 1)) * 2.0f;
				q.w = (m(0, 1) - m(1, 0)) / s;
				q.x = (m(0, 2) + m(2, 0)) / s;
				q.y = (m(1, 2) + m(2, 1)) / s;
				q.z = 0.25f * s;
			}

			q.Normalize();
			return q;
		}

		Quaternion Quaternion::Slerp(const Quaternion& a, const Quaternion& b, float t)
		{
			float cosTheta = a.Dot(b);
			Quaternion end = b;

			// Take shortest path
			if (cosTheta < 0.0f)
			{
				cosTheta = -cosTheta;
				end = Quaternion(-b.w, -b.x, -b.y, -b.z);
			}

			if (cosTheta > 0.9995f)
			{
				// Nearly parallel — linear
				Quaternion r = Lerp(a, end, t);
				r.Normalize();
				return r;
			}

			float theta = acosf(cosTheta);
			float sinTheta = sinf(theta);
			float w1 = sinf((1.0f - t) * theta) / sinTheta;
			float w2 = sinf(t * theta) / sinTheta;

			return Quaternion(
				w1 * a.w + w2 * end.w,
				w1 * a.x + w2 * end.x,
				w1 * a.y + w2 * end.y,
				w1 * a.z + w2 * end.z);
		}

		Quaternion Quaternion::Lerp(const Quaternion& a, const Quaternion& b, float t)
		{
			return Quaternion(
				a.w + (b.w - a.w) * t,
				a.x + (b.x - a.x) * t,
				a.y + (b.y - a.y) * t,
				a.z + (b.z - a.z) * t);
		}

		Quaternion Quaternion::operator+(const Quaternion& a) const
		{
			return Quaternion(w + a.w, x + a.x, y + a.y, z + a.z);
		}

		Quaternion Quaternion::operator-(const Quaternion& a) const
		{
			return Quaternion(w - a.w, x - a.x, y - a.y, z - a.z);
		}

		Quaternion Quaternion::operator*(const Quaternion& a) const
		{
			// Hamilton product: this * a
			return Quaternion(
				w * a.w - x * a.x - y * a.y - z * a.z,
				w * a.x + x * a.w + y * a.z - z * a.y,
				w * a.y - x * a.z + y * a.w + z * a.x,
				w * a.z + x * a.y - y * a.x + z * a.w);
		}

		Quaternion Quaternion::operator/(const Quaternion& a) const
		{
			return (*this) * a.Inverse();
		}

		Quaternion Quaternion::operator*(float real) const
		{
			return Quaternion(w * real, x * real, y * real, z * real);
		}

		Quaternion Quaternion::operator/(float real) const
		{
			float inv = 1.0f / real;
			return Quaternion(w * inv, x * inv, y * inv, z * inv);
		}

		Quaternion& Quaternion::operator*=(const Quaternion& a)
		{
			*this = (*this) * a;
			return *this;
		}

		Quaternion& Quaternion::operator*=(float real)
		{
			w *= real;
			x *= real;
			y *= real;
			z *= real;
			return *this;
		}
	}
}
