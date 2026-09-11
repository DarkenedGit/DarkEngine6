#include "Capsule3f.h"
#include "MathDefines.h"
#include "MathHelper.h"
#include <cmath>

namespace Dark::Math
{
	Capsule3f::Capsule3f(): PointA(Vector3f::ZERO), PointB(Vector3f::ZERO), Radius(0.0f)
	{
	}

	Capsule3f::Capsule3f(const Vector3f& a, const Vector3f& b, float radius)
	{
		Update(a, b, radius);
	}

	void Capsule3f::Update(const Vector3f& a, const Vector3f& b, float radius)
	{
		PointA = a;
		PointB = b;
		Radius = radius;
	}

	void Capsule3f::UpdateRadius(float radius)
	{
		Radius = radius;
	}

	Vector3f Capsule3f::Center() const
	{
		return (PointA + PointB) * 0.5f;
	}

	Vector3f Capsule3f::Axis() const
	{
		Vector3f d = PointB - PointA;
		float lenSq = d.MagnitudeSqrd();
		if (lenSq <= Epsilon * Epsilon)
			return Vector3f::ZERO;
		d *= 1.0f / sqrtf(lenSq);
		return d;
	}

	float Capsule3f::SegmentLength() const
	{
		return (PointB - PointA).Magnitude();
	}

	float Capsule3f::HalfHeight() const
	{
		return SegmentLength() * 0.5f;
	}

	float Capsule3f::Height() const
	{
		return SegmentLength() + 2.0f * Radius;
	}

	Capsule3f Capsule3f::FromCenterAxis(const Vector3f& center, const Vector3f& unitAxis, float halfHeight, float radius)
	{
		Vector3f axis = unitAxis;
		float magSq = axis.MagnitudeSqrd();
		if (magSq > Epsilon * Epsilon)
			axis *= 1.0f / sqrtf(magSq);
		else
			axis = Vector3f::Y_AXIS;
		const Vector3f offset = axis * halfHeight;
		return Capsule3f(center - offset, center + offset, radius);
	}

	Vector3f Capsule3f::ClosestPointOnSegment(const Vector3f& point) const
	{
		Vector3f ab = PointB - PointA;
		float abLenSq = ab.MagnitudeSqrd();
		if (abLenSq <= Epsilon * Epsilon)
			return PointA;
		float t = (point - PointA).Dot(ab) / abLenSq;
		t = Clamp(t, 0.0f, 1.0f);
		return PointA + ab * t;
	}

	bool Capsule3f::Contains(const Vector3f& point) const
	{
		Vector3f closest = ClosestPointOnSegment(point);
		return (point - closest).MagnitudeSqrd() <= Radius * Radius;
	}

	Vector3f Capsule3f::ClosestPoint(const Vector3f& point) const
	{
		Vector3f onSeg = ClosestPointOnSegment(point);
		Vector3f d = point - onSeg;
		float distSq = d.MagnitudeSqrd();
		if (distSq <= Radius * Radius || distSq <= Epsilon * Epsilon)
			return point;
		float dist = sqrtf(distSq);
		return onSeg + d * (Radius / dist);
	}

	float Capsule3f::SignedDistance(const Vector3f& point) const
	{
		Vector3f onSeg = ClosestPointOnSegment(point);
		return (point - onSeg).Magnitude() - Radius;
	}

	float Capsule3f::Distance(const Vector3f& point) const
	{
		return Max(0.0f, SignedDistance(point));
	}

	float Capsule3f::DistanceSq(const Vector3f& point) const
	{
		float d = Distance(point);
		return d * d;
	}

	float Capsule3f::Distance(const Sphere3f& sphere) const
	{
		Vector3f onSeg = ClosestPointOnSegment(sphere.Center);
		float d = (sphere.Center - onSeg).Magnitude() - Radius - sphere.Radius;
		return Max(0.0f, d);
	}

	float Capsule3f::Distance(const Capsule3f& other) const
	{
		Vector3f pa, pb;
		ClosestPointsOnSegments(PointA, PointB, other.PointA, other.PointB, pa, pb);
		float d = (pa - pb).Magnitude() - Radius - other.Radius;
		return Max(0.0f, d);
	}

	bool Capsule3f::Intersects(const Capsule3f& other) const
	{
		Vector3f pa, pb;
		ClosestPointsOnSegments(PointA, PointB, other.PointA, other.PointB, pa, pb);
		float r = Radius + other.Radius;
		return (pa - pb).MagnitudeSqrd() <= r * r;
	}

	bool Capsule3f::Intersects(const Sphere3f& sphere) const
	{
		Vector3f onSeg = ClosestPointOnSegment(sphere.Center);
		float r = Radius + sphere.Radius;
		return (sphere.Center - onSeg).MagnitudeSqrd() <= r * r;
	}

	Sphere3f Capsule3f::ToBoundingSphere() const
	{
		return Sphere3f(Center(), HalfHeight() + Radius);
	}

	AABox3f Capsule3f::ToAABox() const
	{
		Vector3f r(Radius, Radius, Radius);
		Vector3f mn(
			Min(PointA.x, PointB.x),
			Min(PointA.y, PointB.y),
			Min(PointA.z, PointB.z));
		Vector3f mx(
			Max(PointA.x, PointB.x),
			Max(PointA.y, PointB.y),
			Max(PointA.z, PointB.z));
		return AABox3f(mn - r, mx + r);
	}

	float ClosestPointsOnSegments(
		const Vector3f& a0, const Vector3f& a1,
		const Vector3f& b0, const Vector3f& b1,
		Vector3f& outA, Vector3f& outB,
		float* sa, float* sb)
	{
		// Ericson Real-Time Collision Detection, §5.1.9
		Vector3f d1 = a1 - a0;
		Vector3f d2 = b1 - b0;
		Vector3f r = a0 - b0;
		float a = d1.Dot(d1);
		float e = d2.Dot(d2);
		float f = d2.Dot(r);
		float s = 0.0f;
		float t = 0.0f;

		if (a <= Epsilon && e <= Epsilon)
		{
			outA = a0;
			outB = b0;
			if (sa) *sa = 0.0f;
			if (sb) *sb = 0.0f;
			return (outA - outB).MagnitudeSqrd();
		}

		if (a <= Epsilon)
		{
			s = 0.0f;
			t = Clamp(f / e, 0.0f, 1.0f);
		}
		else
		{
			float c = d1.Dot(r);
			if (e <= Epsilon)
			{
				t = 0.0f;
				s = Clamp(-c / a, 0.0f, 1.0f);
			}
			else
			{
				float b = d1.Dot(d2);
				float denom = a * e - b * b;
				if (denom != 0.0f)
					s = Clamp((b * f - c * e) / denom, 0.0f, 1.0f);
				else
					s = 0.0f;
				t = (b * s + f) / e;
				if (t < 0.0f)
				{
					t = 0.0f;
					s = Clamp(-c / a, 0.0f, 1.0f);
				}
				else if (t > 1.0f)
				{
					t = 1.0f;
					s = Clamp((b - c) / a, 0.0f, 1.0f);
				}
			}
		}

		outA = a0 + d1 * s;
		outB = b0 + d2 * t;
		if (sa) *sa = s;
		if (sb) *sb = t;
		return (outA - outB).MagnitudeSqrd();
	}
}
