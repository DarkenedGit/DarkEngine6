#include "Capsule2f.h"
#include "MathDefines.h"
#include "MathHelper.h"
#include <cmath>

namespace Dark::Math
{
	Capsule2f::Capsule2f(): PointA(Vector2f::ZERO), PointB(Vector2f::ZERO), Radius(0.0f)
	{
	}

	Capsule2f::Capsule2f(const Vector2f& a, const Vector2f& b, float radius)
	{
		Update(a, b, radius);
	}

	void Capsule2f::Update(const Vector2f& a, const Vector2f& b, float radius)
	{
		PointA = a;
		PointB = b;
		Radius = radius;
	}

	void Capsule2f::UpdateRadius(float radius)
	{
		Radius = radius;
	}

	Vector2f Capsule2f::Center() const
	{
		return (PointA + PointB) * 0.5f;
	}

	Vector2f Capsule2f::Axis() const
	{
		Vector2f d = PointB - PointA;
		float lenSq = d.MagnitudeSqrd();
		if (lenSq <= Epsilon * Epsilon)
			return Vector2f::ZERO;
		d *= 1.0f / sqrtf(lenSq);
		return d;
	}

	float Capsule2f::SegmentLength() const
	{
		return (PointB - PointA).Magnitude();
	}

	float Capsule2f::HalfHeight() const
	{
		return SegmentLength() * 0.5f;
	}

	float Capsule2f::Height() const
	{
		return SegmentLength() + 2.0f * Radius;
	}

	Capsule2f Capsule2f::FromCenterAxis(const Vector2f& center, const Vector2f& unitAxis, float halfHeight, float radius)
	{
		Vector2f axis = unitAxis;
		float magSq = axis.MagnitudeSqrd();
		if (magSq > Epsilon * Epsilon)
			axis *= 1.0f / sqrtf(magSq);
		else
			axis = Vector2f::Y_AXIS;
		const Vector2f offset = axis * halfHeight;
		return Capsule2f(center - offset, center + offset, radius);
	}

	Vector2f Capsule2f::ClosestPointOnSegment(const Vector2f& point) const
	{
		Vector2f ab = PointB - PointA;
		float abLenSq = ab.MagnitudeSqrd();
		if (abLenSq <= Epsilon * Epsilon)
			return PointA;
		float t = (point - PointA).Dot(ab) / abLenSq;
		t = Clamp(t, 0.0f, 1.0f);
		return PointA + ab * t;
	}

	bool Capsule2f::Contains(const Vector2f& point) const
	{
		Vector2f closest = ClosestPointOnSegment(point);
		return (point - closest).MagnitudeSqrd() <= Radius * Radius;
	}

	Vector2f Capsule2f::ClosestPoint(const Vector2f& point) const
	{
		Vector2f onSeg = ClosestPointOnSegment(point);
		Vector2f d = point - onSeg;
		float distSq = d.MagnitudeSqrd();
		if (distSq <= Radius * Radius || distSq <= Epsilon * Epsilon)
			return point;
		float dist = sqrtf(distSq);
		return onSeg + d * (Radius / dist);
	}

	float Capsule2f::SignedDistance(const Vector2f& point) const
	{
		Vector2f onSeg = ClosestPointOnSegment(point);
		return (point - onSeg).Magnitude() - Radius;
	}

	float Capsule2f::Distance(const Vector2f& point) const
	{
		return Max(0.0f, SignedDistance(point));
	}

	float Capsule2f::DistanceSq(const Vector2f& point) const
	{
		float d = Distance(point);
		return d * d;
	}

	float Capsule2f::Distance(const Sphere2f& circle) const
	{
		Vector2f onSeg = ClosestPointOnSegment(circle.Center);
		float d = (circle.Center - onSeg).Magnitude() - Radius - circle.Radius;
		return Max(0.0f, d);
	}

	float Capsule2f::Distance(const Capsule2f& other) const
	{
		Vector2f pa, pb;
		ClosestPointsOnSegments(PointA, PointB, other.PointA, other.PointB, pa, pb);
		float d = (pa - pb).Magnitude() - Radius - other.Radius;
		return Max(0.0f, d);
	}

	bool Capsule2f::Intersects(const Capsule2f& other) const
	{
		Vector2f pa, pb;
		ClosestPointsOnSegments(PointA, PointB, other.PointA, other.PointB, pa, pb);
		float r = Radius + other.Radius;
		return (pa - pb).MagnitudeSqrd() <= r * r;
	}

	bool Capsule2f::Intersects(const Sphere2f& circle) const
	{
		Vector2f onSeg = ClosestPointOnSegment(circle.Center);
		float r = Radius + circle.Radius;
		return (circle.Center - onSeg).MagnitudeSqrd() <= r * r;
	}

	Sphere2f Capsule2f::ToBoundingCircle() const
	{
		return Sphere2f(Center(), HalfHeight() + Radius);
	}

	AABox2f Capsule2f::ToAABox() const
	{
		Vector2f r(Radius, Radius);
		Vector2f mn(Min(PointA.x, PointB.x), Min(PointA.y, PointB.y));
		Vector2f mx(Max(PointA.x, PointB.x), Max(PointA.y, PointB.y));
		return AABox2f(mn - r, mx + r);
	}

	float ClosestPointsOnSegments(
		const Vector2f& a0, const Vector2f& a1,
		const Vector2f& b0, const Vector2f& b1,
		Vector2f& outA, Vector2f& outB,
		float* sa, float* sb)
	{
		// Ericson Real-Time Collision Detection, §5.1.9
		Vector2f d1 = a1 - a0;
		Vector2f d2 = b1 - b0;
		Vector2f r = a0 - b0;
		float a = d1.Dot(d1);
		float e = d2.Dot(d2);
		float f = d2.Dot(r);
		float s = 0.0f;
		float t = 0.0f;

		if (a <= Epsilon && e <= Epsilon)
		{
			outA = a0;
			outB = b0;
			if (sa)
				*sa = 0.0f;
			if (sb)
				*sb = 0.0f;
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
		if (sa)
			*sa = s;
		if (sb)
			*sb = t;
		return (outA - outB).MagnitudeSqrd();
	}
}
