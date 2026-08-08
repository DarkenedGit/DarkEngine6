#pragma once

#include "Vector2f.h"
#include "Vector3f.h"
#include "MathHelper.h"

namespace Dark
{
	namespace Math
	{
		// ---------------------------------------------------------------------------
		// Parametric curves. Parameter t is typically in [0, 1].
		// ---------------------------------------------------------------------------

		// Linear
		inline Vector3f Lerp(const Vector3f& a, const Vector3f& b, float t)
		{
			return a + (b - a) * t;
		}

		inline Vector2f Lerp(const Vector2f& a, const Vector2f& b, float t)
		{
			return a + (b - a) * t;
		}

		// 2D Bezier / Hermite / Catmull-Rom (same formulas as 3D)
		inline Vector2f BezierQuadratic(const Vector2f& p0, const Vector2f& p1, const Vector2f& p2, float t)
		{
			float u  = 1.0f - t;
			float uu = u * u;
			float tt = t * t;
			return p0 * uu + p1 * (2.0f * u * t) + p2 * tt;
		}

		inline Vector2f BezierCubic(const Vector2f& p0, const Vector2f& p1,
		                            const Vector2f& p2, const Vector2f& p3, float t)
		{
			float u   = 1.0f - t;
			float uu  = u * u;
			float uuu = uu * u;
			float tt  = t * t;
			float ttt = tt * t;
			return p0 * uuu + p1 * (3.0f * uu * t) + p2 * (3.0f * u * tt) + p3 * ttt;
		}

		inline Vector2f Hermite(const Vector2f& p0, const Vector2f& m0,
		                        const Vector2f& p1, const Vector2f& m1, float t)
		{
			float t2 = t * t;
			float t3 = t2 * t;
			float h00 =  2.0f * t3 - 3.0f * t2 + 1.0f;
			float h10 =         t3 - 2.0f * t2 + t;
			float h01 = -2.0f * t3 + 3.0f * t2;
			float h11 =         t3 -        t2;
			return p0 * h00 + m0 * h10 + p1 * h01 + m1 * h11;
		}

		inline Vector2f CatmullRom(const Vector2f& p0, const Vector2f& p1,
		                           const Vector2f& p2, const Vector2f& p3,
		                           float t, float tautness = 0.5f)
		{
			Vector2f m1 = (p2 - p0) * tautness;
			Vector2f m2 = (p3 - p1) * tautness;
			return Hermite(p1, m1, p2, m2, t);
		}

		// Quadratic Bezier: B(t) = (1-t)^2 P0 + 2(1-t)t P1 + t^2 P2
		inline Vector3f BezierQuadratic(const Vector3f& p0, const Vector3f& p1, const Vector3f& p2, float t)
		{
			float u  = 1.0f - t;
			float uu = u * u;
			float tt = t * t;
			return p0 * uu + p1 * (2.0f * u * t) + p2 * tt;
		}

		// Cubic Bezier: B(t) = (1-t)^3 P0 + 3(1-t)^2 t P1 + 3(1-t) t^2 P2 + t^3 P3
		inline Vector3f BezierCubic(const Vector3f& p0, const Vector3f& p1,
		                            const Vector3f& p2, const Vector3f& p3, float t)
		{
			float u   = 1.0f - t;
			float uu  = u * u;
			float uuu = uu * u;
			float tt  = t * t;
			float ttt = tt * t;
			return p0 * uuu + p1 * (3.0f * uu * t) + p2 * (3.0f * u * tt) + p3 * ttt;
		}

		// Quadratic Bezier derivative (tangent direction, not unit)
		inline Vector3f BezierQuadraticDerivative(const Vector3f& p0, const Vector3f& p1,
		                                          const Vector3f& p2, float t)
		{
			// B'(t) = 2(1-t)(P1-P0) + 2t(P2-P1)
			return (p1 - p0) * (2.0f * (1.0f - t)) + (p2 - p1) * (2.0f * t);
		}

		// Cubic Bezier derivative (tangent direction, not unit)
		inline Vector3f BezierCubicDerivative(const Vector3f& p0, const Vector3f& p1,
		                                      const Vector3f& p2, const Vector3f& p3, float t)
		{
			// B'(t) = 3(1-t)^2 (P1-P0) + 6(1-t)t (P2-P1) + 3t^2 (P3-P2)
			float u = 1.0f - t;
			return (p1 - p0) * (3.0f * u * u)
			     + (p2 - p1) * (6.0f * u * t)
			     + (p3 - p2) * (3.0f * t * t);
		}

		// Hermite spline: endpoints p0,p1 with tangents m0,m1
		// H(t) = (2t^3-3t^2+1)p0 + (t^3-2t^2+t)m0 + (-2t^3+3t^2)p1 + (t^3-t^2)m1
		inline Vector3f Hermite(const Vector3f& p0, const Vector3f& m0,
		                        const Vector3f& p1, const Vector3f& m1, float t)
		{
			float t2 = t * t;
			float t3 = t2 * t;
			float h00 =  2.0f * t3 - 3.0f * t2 + 1.0f;
			float h10 =         t3 - 2.0f * t2 + t;
			float h01 = -2.0f * t3 + 3.0f * t2;
			float h11 =         t3 -        t2;
			return p0 * h00 + m0 * h10 + p1 * h01 + m1 * h11;
		}

		// Catmull-Rom through p1->p2, with neighbors p0 and p3 for tangents.
		// tautness: standard is 0.5
		inline Vector3f CatmullRom(const Vector3f& p0, const Vector3f& p1,
		                           const Vector3f& p2, const Vector3f& p3,
		                           float t, float tautness = 0.5f)
		{
			Vector3f m1 = (p2 - p0) * tautness;
			Vector3f m2 = (p3 - p1) * tautness;
			return Hermite(p1, m1, p2, m2, t);
		}

		// Approximate arc-length of cubic Bezier by uniform samples.
		float BezierCubicLength(const Vector3f& p0, const Vector3f& p1,
		                        const Vector3f& p2, const Vector3f& p3,
		                        int segments = 16);
	}
}
