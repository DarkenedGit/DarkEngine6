#include "StaticCollision.h"
#include "SweptCollision.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
#include "Math/Ray2f.h"
#include "Math/Ray3f.h"
#include <cmath>
#include <algorithm>

namespace Dark::Collision
{
	using namespace Math;

	// ---------------------------------------------------------------------
	// Helpers
	// ---------------------------------------------------------------------

	static Vector3f ClosestPointOnAabb(const Vector3f& p, const AABox3f& box)
	{
		return Vector3f(
			std::max(box.Min.x, std::min(p.x, box.Max.x)),
			std::max(box.Min.y, std::min(p.y, box.Max.y)),
			std::max(box.Min.z, std::min(p.z, box.Max.z)));
	}

	static Vector3f ClosestPointOnObb(const Vector3f& p, const Box3f& box)
	{
		Vector3f d = p - box.Center;
		Vector3f result = box.Center;
		for (int i = 0; i < 3; ++i)
		{
			float dist = d.Dot(box.Axis[i]);
			dist = std::max(-box.Extent[i], std::min(dist, box.Extent[i]));
			result += box.Axis[i] * dist;
		}
		return result;
	}

	// Squared distance from point to AABB (0 if inside).
	static float DistanceSqPointAabb(const Vector3f& p, const AABox3f& box)
	{
		Vector3f c = ClosestPointOnAabb(p, box);
		return (p - c).MagnitudeSqrd();
	}

	static float DistanceSqPointObb(const Vector3f& p, const Box3f& box)
	{
		Vector3f c = ClosestPointOnObb(p, box);
		return (p - c).MagnitudeSqrd();
	}

	// Distance from a point on a segment to a convex set is a convex function of t,
	// so ternary search finds the global minimum.
	static float DistanceSqSegmentAabb(const Vector3f& a0, const Vector3f& a1, const AABox3f& box)
	{
		Vector3f ab = a1 - a0;
		float lo = 0.0f;
		float hi = 1.0f;
		for (int i = 0; i < 32; ++i)
		{
			float m1 = lo + (hi - lo) * (1.0f / 3.0f);
			float m2 = lo + (hi - lo) * (2.0f / 3.0f);
			float d1 = DistanceSqPointAabb(a0 + ab * m1, box);
			float d2 = DistanceSqPointAabb(a0 + ab * m2, box);
			if (d1 < d2)
				hi = m2;
			else
				lo = m1;
		}
		float t = 0.5f * (lo + hi);
		return DistanceSqPointAabb(a0 + ab * t, box);
	}

	static float DistanceSqSegmentObb(const Vector3f& a0, const Vector3f& a1, const Box3f& box)
	{
		Vector3f ab = a1 - a0;
		float lo = 0.0f;
		float hi = 1.0f;
		for (int i = 0; i < 32; ++i)
		{
			float m1 = lo + (hi - lo) * (1.0f / 3.0f);
			float m2 = lo + (hi - lo) * (2.0f / 3.0f);
			float d1 = DistanceSqPointObb(a0 + ab * m1, box);
			float d2 = DistanceSqPointObb(a0 + ab * m2, box);
			if (d1 < d2)
				hi = m2;
			else
				lo = m1;
		}
		float t = 0.5f * (lo + hi);
		return DistanceSqPointObb(a0 + ab * t, box);
	}

	static Capsule3f CapsuleTranslated(const Capsule3f& c, const Vector3f& delta, float t)
	{
		return Capsule3f(c.PointA + delta * t, c.PointB + delta * t, c.Radius);
	}

	static void ContactNormalFromPoints(const Vector3f& fromB, const Vector3f& toA, Vector3f& outNormal)
	{
		Vector3f n = toA - fromB;
		if (n.MagnitudeSqrd() > Epsilon)
		{
			n.Normalize();
			outNormal = n;
		}
	}

	// Binary-search first TOI in [0,1] given a static overlap predicate.
	template <typename OverlapFn>
	static bool BinarySearchToi(OverlapFn&& overlaps, float& tOut)
	{
		if (overlaps(0.0f))
		{
			tOut = 0.0f;
			return true;
		}

		float lo = 0.0f;
		float hi = 1.0f;
		bool found = false;
		const int samples = 16;
		for (int i = 1; i <= samples; ++i)
		{
			float t = static_cast<float>(i) / static_cast<float>(samples);
			if (overlaps(t))
			{
				found = true;
				hi = t;
				break;
			}
			lo = t;
		}
		if (!found)
			return false;

		for (int i = 0; i < 24; ++i)
		{
			float mid = 0.5f * (lo + hi);
			if (overlaps(mid))
				hi = mid;
			else
				lo = mid;
		}

		tOut = hi;
		return true;
	}

	// ---------------------------------------------------------------------
	// Static capsule tests
	// ---------------------------------------------------------------------

	bool Intersects(const Vector3f& point, const Capsule3f& capsule)
	{
		return capsule.Contains(point);
	}

	bool Intersects(const Capsule3f& a, const Capsule3f& b)
	{
		return a.Intersects(b);
	}

	bool Intersects(const Capsule3f& capsule, const Sphere3f& sphere)
	{
		return capsule.Intersects(sphere);
	}

	bool Intersects(const Capsule3f& capsule, const AABox3f& box)
	{
		float r = capsule.Radius;
		return DistanceSqSegmentAabb(capsule.PointA, capsule.PointB, box) <= r * r;
	}

	bool Intersects(const Capsule3f& capsule, const Box3f& box)
	{
		float r = capsule.Radius;
		return DistanceSqSegmentObb(capsule.PointA, capsule.PointB, box) <= r * r;
	}

	RayHit3D Intersect(const Ray3f& ray, const Capsule3f& capsule)
	{
		// Inigo Quilez-style ray/capsule: finite cylinder + hemispherical caps.
		RayHit3D hit;

		const Vector3f ba = capsule.PointB - capsule.PointA;
		const Vector3f oa = ray.Origin - capsule.PointA;
		const float baba = ba.Dot(ba);
		const float bard = ba.Dot(ray.Direction);
		const float baoa = ba.Dot(oa);
		const float rdoa = ray.Direction.Dot(oa);
		const float oaoa = oa.Dot(oa);
		const float r2 = capsule.Radius * capsule.Radius;

		// Degenerate capsule -> sphere
		if (baba <= Epsilon * Epsilon)
		{
			float t = 0.0f;
			if (!ray.IntersectSphere(Sphere3f(capsule.PointA, capsule.Radius), t))
				return hit;
			hit.hit = true;
			hit.t = t;
			hit.point = ray.PointAt(t);
			hit.normal = hit.point - capsule.PointA;
			if (hit.normal.MagnitudeSqrd() > Epsilon)
				hit.normal.Normalize();
			return hit;
		}

		float bestT = 1.0e30f;
		bool found = false;
		Vector3f bestNormal = Vector3f::ZERO;

		auto considerT = [&](float t, const Vector3f& normalSrc)
		{
			if (t < 0.0f || t >= bestT)
				return;
			bestT = t;
			found = true;
			bestNormal = normalSrc;
		};

		// Infinite cylinder quadratic, then clip to segment body.
		const float a = baba - bard * bard;
		const float b = baba * rdoa - baoa * bard;
		const float c = baba * oaoa - baoa * baoa - r2 * baba;
		const float discr = b * b - a * c;
		if (discr >= 0.0f && fabsf(a) > Epsilon)
		{
			const float sqrtD = sqrtf(discr);
			float t = (-b - sqrtD) / a;
			float y = baoa + t * bard;
			if (y > 0.0f && y < baba)
			{
				Vector3f p = ray.PointAt(t);
				Vector3f onAxis = capsule.PointA + ba * (y / baba);
				considerT(t, p - onAxis);
			}
		}

		// Cap A (y <= 0)
		{
			float t = 0.0f;
			if (ray.IntersectSphere(Sphere3f(capsule.PointA, capsule.Radius), t))
			{
				Vector3f p = ray.PointAt(t);
				float y = (p - capsule.PointA).Dot(ba);
				if (y <= 0.0f)
					considerT(t, p - capsule.PointA);
			}
		}

		// Cap B (y >= baba)
		{
			float t = 0.0f;
			if (ray.IntersectSphere(Sphere3f(capsule.PointB, capsule.Radius), t))
			{
				Vector3f p = ray.PointAt(t);
				float y = (p - capsule.PointA).Dot(ba);
				if (y >= baba)
					considerT(t, p - capsule.PointB);
			}
		}

		if (!found)
			return hit;

		hit.hit = true;
		hit.t = bestT;
		hit.point = ray.PointAt(bestT);
		hit.normal = bestNormal;
		if (hit.normal.MagnitudeSqrd() > Epsilon)
			hit.normal.Normalize();
		return hit;
	}

	// ---------------------------------------------------------------------
	// Swept capsule tests
	// ---------------------------------------------------------------------

	SweptHit3D SweptIntersects(const Vector3f& p0, const Vector3f& delta, const Capsule3f& capsule)
	{
		SweptHit3D hit;
		float len = delta.Magnitude();
		if (len < Epsilon)
		{
			if (Intersects(p0, capsule))
			{
				hit.hit = true;
				hit.t = 0.0f;
				hit.point = p0;
			}
			return hit;
		}

		Vector3f dir = delta * (1.0f / len);
		RayHit3D rh = Intersect(Ray3f(p0, dir), capsule);
		if (!rh.hit)
			return hit;

		float t = rh.t / len;
		if (t < 0.0f || t > 1.0f)
			return hit;

		hit.hit = true;
		hit.t = t;
		hit.point = p0 + delta * t;
		hit.normal = rh.normal;
		return hit;
	}

	SweptHit3D SweptIntersects(const Sphere3f& sphere, const Vector3f& delta, const Capsule3f& capsule)
	{
		// Inflate capsule by sphere radius; sweep sphere center as a point.
		Capsule3f inflated(capsule.PointA, capsule.PointB, capsule.Radius + sphere.Radius);
		SweptHit3D hit = SweptIntersects(sphere.Center, delta, inflated);
		if (!hit.hit)
			return hit;

		// Rebuild contact from sphere center at TOI toward capsule segment.
		Vector3f centerAt = sphere.Center + delta * hit.t;
		Vector3f onSeg = capsule.ClosestPointOnSegment(centerAt);
		ContactNormalFromPoints(onSeg, centerAt, hit.normal);
		hit.point = onSeg + hit.normal * capsule.Radius;
		return hit;
	}

	SweptHit3D SweptIntersects(const Capsule3f& capsule, const Vector3f& delta, const Sphere3f& sphere)
	{
		// Relative: move capsule, keep sphere fixed = move sphere opposite.
		SweptHit3D hit = SweptIntersects(sphere, -delta, capsule);
		if (!hit.hit)
			return hit;

		// Flip normal so it points from sphere (B) toward capsule (A).
		hit.normal = -hit.normal;
		hit.point = sphere.Center + hit.normal * sphere.Radius;
		return hit;
	}

	SweptHit3D SweptIntersects(const Capsule3f& a, const Vector3f& deltaA,
		                        const Capsule3f& b, const Vector3f& deltaB)
	{
		SweptHit3D hit;

		Sphere3f sa = a.ToBoundingSphere();
		Sphere3f sb = b.ToBoundingSphere();
		SweptHit3D broad = SweptIntersects(sa, deltaA, sb, deltaB);
		if (!broad.hit)
			return hit;

		auto overlaps = [&](float t) -> bool
		{
			Capsule3f at = CapsuleTranslated(a, deltaA, t);
			Capsule3f bt = CapsuleTranslated(b, deltaB, t);
			return Intersects(at, bt);
		};

		float toi = 0.0f;
		if (!BinarySearchToi(overlaps, toi))
			return hit;

		hit.hit = true;
		hit.t = toi;
		Capsule3f at = CapsuleTranslated(a, deltaA, toi);
		Capsule3f bt = CapsuleTranslated(b, deltaB, toi);
		Vector3f pa, pb;
		ClosestPointsOnSegments(at.PointA, at.PointB, bt.PointA, bt.PointB, pa, pb);
		ContactNormalFromPoints(pb, pa, hit.normal);
		hit.point = pb + hit.normal * bt.Radius;
		return hit;
	}

	SweptHit3D SweptIntersects(const Capsule3f& capsule, const Vector3f& delta, const AABox3f& box)
	{
		SweptHit3D hit;

		Sphere3f sa = capsule.ToBoundingSphere();
		Sphere3f sb = box.ToBoundingSphere();
		SweptHit3D broad = SweptIntersects(sa, delta, sb, Vector3f::ZERO);
		if (!broad.hit)
			return hit;

		auto overlaps = [&](float t) -> bool
		{
			return Intersects(CapsuleTranslated(capsule, delta, t), box);
		};

		float toi = 0.0f;
		if (!BinarySearchToi(overlaps, toi))
			return hit;

		hit.hit = true;
		hit.t = toi;
		Capsule3f at = CapsuleTranslated(capsule, delta, toi);
		Vector3f ab = at.PointB - at.PointA;
		float bestT = 0.0f;
		float bestD = DistanceSqPointAabb(at.PointA, box);
		float d1 = DistanceSqPointAabb(at.PointB, box);
		if (d1 < bestD)
		{
			bestD = d1;
			bestT = 1.0f;
		}
		float lo = 0.0f, hi = 1.0f;
		for (int i = 0; i < 16; ++i)
		{
			float m1 = lo + (hi - lo) * (1.0f / 3.0f);
			float m2 = lo + (hi - lo) * (2.0f / 3.0f);
			float dm1 = DistanceSqPointAabb(at.PointA + ab * m1, box);
			float dm2 = DistanceSqPointAabb(at.PointA + ab * m2, box);
			if (dm1 < dm2)
				hi = m2;
			else
				lo = m1;
		}
		bestT = 0.5f * (lo + hi);
		Vector3f onSeg = at.PointA + ab * bestT;
		Vector3f onBox = ClosestPointOnAabb(onSeg, box);
		ContactNormalFromPoints(onBox, onSeg, hit.normal);
		hit.point = onBox;
		return hit;
	}

	SweptHit3D SweptIntersects(const Capsule3f& capsule, const Vector3f& delta, const Box3f& box)
	{
		SweptHit3D hit;

		Sphere3f sa = capsule.ToBoundingSphere();
		float rb = Vector3f(box.Extent[0], box.Extent[1], box.Extent[2]).Magnitude();
		Sphere3f sb(box.Center, rb);
		SweptHit3D broad = SweptIntersects(sa, delta, sb, Vector3f::ZERO);
		if (!broad.hit)
			return hit;

		auto overlaps = [&](float t) -> bool
		{
			return Intersects(CapsuleTranslated(capsule, delta, t), box);
		};

		float toi = 0.0f;
		if (!BinarySearchToi(overlaps, toi))
			return hit;

		hit.hit = true;
		hit.t = toi;
		Capsule3f at = CapsuleTranslated(capsule, delta, toi);
		Vector3f ab = at.PointB - at.PointA;
		float lo = 0.0f, hi = 1.0f;
		for (int i = 0; i < 16; ++i)
		{
			float m1 = lo + (hi - lo) * (1.0f / 3.0f);
			float m2 = lo + (hi - lo) * (2.0f / 3.0f);
			float dm1 = DistanceSqPointObb(at.PointA + ab * m1, box);
			float dm2 = DistanceSqPointObb(at.PointA + ab * m2, box);
			if (dm1 < dm2)
				hi = m2;
			else
				lo = m1;
		}
		float bestT = 0.5f * (lo + hi);
		Vector3f onSeg = at.PointA + ab * bestT;
		Vector3f onBox = ClosestPointOnObb(onSeg, box);
		ContactNormalFromPoints(onBox, onSeg, hit.normal);
		hit.point = onBox;
		return hit;
	}

	// ---------------------------------------------------------------------
	// 2D helpers
	// ---------------------------------------------------------------------

	static Vector2f ClosestPointOnAabb2(const Vector2f& p, const AABox2f& box)
	{
		return Vector2f(
			std::max(box.Min.x, std::min(p.x, box.Max.x)),
			std::max(box.Min.y, std::min(p.y, box.Max.y)));
	}

	static Vector2f ClosestPointOnObb2(const Vector2f& p, const Box2f& box)
	{
		Vector2f d = p - box.Center;
		Vector2f result = box.Center;
		for (int i = 0; i < 2; ++i)
		{
			float dist = d.Dot(box.Axis[i]);
			dist = std::max(-box.Extent[i], std::min(dist, box.Extent[i]));
			result += box.Axis[i] * dist;
		}
		return result;
	}

	static float DistanceSqPointAabb2(const Vector2f& p, const AABox2f& box)
	{
		Vector2f c = ClosestPointOnAabb2(p, box);
		return (p - c).MagnitudeSqrd();
	}

	static float DistanceSqPointObb2(const Vector2f& p, const Box2f& box)
	{
		Vector2f c = ClosestPointOnObb2(p, box);
		return (p - c).MagnitudeSqrd();
	}

	static float DistanceSqSegmentAabb2(const Vector2f& a0, const Vector2f& a1, const AABox2f& box)
	{
		Vector2f ab = a1 - a0;
		float lo = 0.0f;
		float hi = 1.0f;
		for (int i = 0; i < 32; ++i)
		{
			float m1 = lo + (hi - lo) * (1.0f / 3.0f);
			float m2 = lo + (hi - lo) * (2.0f / 3.0f);
			float d1 = DistanceSqPointAabb2(a0 + ab * m1, box);
			float d2 = DistanceSqPointAabb2(a0 + ab * m2, box);
			if (d1 < d2)
				hi = m2;
			else
				lo = m1;
		}
		float t = 0.5f * (lo + hi);
		return DistanceSqPointAabb2(a0 + ab * t, box);
	}

	static float DistanceSqSegmentObb2(const Vector2f& a0, const Vector2f& a1, const Box2f& box)
	{
		Vector2f ab = a1 - a0;
		float lo = 0.0f;
		float hi = 1.0f;
		for (int i = 0; i < 32; ++i)
		{
			float m1 = lo + (hi - lo) * (1.0f / 3.0f);
			float m2 = lo + (hi - lo) * (2.0f / 3.0f);
			float d1 = DistanceSqPointObb2(a0 + ab * m1, box);
			float d2 = DistanceSqPointObb2(a0 + ab * m2, box);
			if (d1 < d2)
				hi = m2;
			else
				lo = m1;
		}
		float t = 0.5f * (lo + hi);
		return DistanceSqPointObb2(a0 + ab * t, box);
	}

	static Capsule2f CapsuleTranslated2(const Capsule2f& c, const Vector2f& delta, float t)
	{
		return Capsule2f(c.PointA + delta * t, c.PointB + delta * t, c.Radius);
	}

	static void ContactNormalFromPoints2(const Vector2f& fromB, const Vector2f& toA, Vector2f& outNormal)
	{
		Vector2f n = toA - fromB;
		if (n.MagnitudeSqrd() > Epsilon)
		{
			n.Normalize();
			outNormal = n;
		}
	}

	// ---------------------------------------------------------------------
	// Static capsule2 tests
	// ---------------------------------------------------------------------

	bool Intersects(const Vector2f& point, const Capsule2f& capsule)
	{
		return capsule.Contains(point);
	}

	bool Intersects(const Capsule2f& a, const Capsule2f& b)
	{
		return a.Intersects(b);
	}

	bool Intersects(const Capsule2f& capsule, const Sphere2f& circle)
	{
		return capsule.Intersects(circle);
	}

	bool Intersects(const Capsule2f& capsule, const AABox2f& box)
	{
		float r = capsule.Radius;
		return DistanceSqSegmentAabb2(capsule.PointA, capsule.PointB, box) <= r * r;
	}

	bool Intersects(const Capsule2f& capsule, const Box2f& box)
	{
		float r = capsule.Radius;
		return DistanceSqSegmentObb2(capsule.PointA, capsule.PointB, box) <= r * r;
	}

	bool Intersect(const Ray2f& ray, const Capsule2f& capsule, RayHit2D& results)
	{
		results = RayHit2D{};

		const Vector2f ba = capsule.PointB - capsule.PointA;
		const Vector2f oa = ray.Origin - capsule.PointA;
		const float baba = ba.Dot(ba);
		const float bard = ba.Dot(ray.Direction);
		const float baoa = ba.Dot(oa);
		const float rdoa = ray.Direction.Dot(oa);
		const float oaoa = oa.Dot(oa);
		const float r2 = capsule.Radius * capsule.Radius;

		if (baba <= Epsilon * Epsilon)
		{
			float t = 0.0f;
			if (!ray.IntersectCircle(Sphere2f(capsule.PointA, capsule.Radius), t))
				return false;
			results.hit = true;
			results.t = t;
			results.point = ray.PointAt(t);
			results.normal = results.point - capsule.PointA;
			if (results.normal.MagnitudeSqrd() > Epsilon)
				results.normal.Normalize();
			return true;
		}

		float bestT = 1.0e30f;
		bool found = false;
		Vector2f bestNormal = Vector2f::ZERO;

		auto considerT = [&](float t, const Vector2f& normalSrc)
		{
			if (t < 0.0f || t >= bestT)
				return;
			bestT = t;
			found = true;
			bestNormal = normalSrc;
		};

		const float a = baba - bard * bard;
		const float b = baba * rdoa - baoa * bard;
		const float c = baba * oaoa - baoa * baoa - r2 * baba;
		const float discr = b * b - a * c;
		if (discr >= 0.0f && fabsf(a) > Epsilon)
		{
			const float sqrtD = sqrtf(discr);
			float t = (-b - sqrtD) / a;
			float y = baoa + t * bard;
			if (y > 0.0f && y < baba)
			{
				Vector2f p = ray.PointAt(t);
				Vector2f onAxis = capsule.PointA + ba * (y / baba);
				considerT(t, p - onAxis);
			}
		}

		{
			float t = 0.0f;
			if (ray.IntersectCircle(Sphere2f(capsule.PointA, capsule.Radius), t))
			{
				Vector2f p = ray.PointAt(t);
				float y = (p - capsule.PointA).Dot(ba);
				if (y <= 0.0f)
					considerT(t, p - capsule.PointA);
			}
		}

		{
			float t = 0.0f;
			if (ray.IntersectCircle(Sphere2f(capsule.PointB, capsule.Radius), t))
			{
				Vector2f p = ray.PointAt(t);
				float y = (p - capsule.PointA).Dot(ba);
				if (y >= baba)
					considerT(t, p - capsule.PointB);
			}
		}

		if (!found)
			return false;

		results.hit = true;
		results.t = bestT;
		results.point = ray.PointAt(bestT);
		results.normal = bestNormal;
		if (results.normal.MagnitudeSqrd() > Epsilon)
			results.normal.Normalize();
		return true;
	}

	// ---------------------------------------------------------------------
	// Swept capsule2 tests
	// ---------------------------------------------------------------------

	SweptHit2D SweptIntersects(const Vector2f& p0, const Vector2f& delta, const Capsule2f& capsule)
	{
		SweptHit2D hit;
		float len = delta.Magnitude();
		if (len < Epsilon)
		{
			if (Intersects(p0, capsule))
			{
				hit.hit = true;
				hit.t = 0.0f;
				hit.point = p0;
			}
			return hit;
		}

		Vector2f dir = delta * (1.0f / len);
		RayHit2D rh;
		if (!Intersect(Ray2f(p0, dir), capsule, rh))
			return hit;

		float t = rh.t / len;
		if (t < 0.0f || t > 1.0f)
			return hit;

		hit.hit = true;
		hit.t = t;
		hit.point = p0 + delta * t;
		hit.normal = rh.normal;
		return hit;
	}

	SweptHit2D SweptIntersects(const Sphere2f& circle, const Vector2f& delta, const Capsule2f& capsule)
	{
		Capsule2f inflated(capsule.PointA, capsule.PointB, capsule.Radius + circle.Radius);
		SweptHit2D hit = SweptIntersects(circle.Center, delta, inflated);
		if (!hit.hit)
			return hit;

		Vector2f centerAt = circle.Center + delta * hit.t;
		Vector2f onSeg = capsule.ClosestPointOnSegment(centerAt);
		ContactNormalFromPoints2(onSeg, centerAt, hit.normal);
		hit.point = onSeg + hit.normal * capsule.Radius;
		return hit;
	}

	SweptHit2D SweptIntersects(const Capsule2f& capsule, const Vector2f& delta, const Sphere2f& circle)
	{
		SweptHit2D hit = SweptIntersects(circle, -delta, capsule);
		if (!hit.hit)
			return hit;

		hit.normal = -hit.normal;
		hit.point = circle.Center + hit.normal * circle.Radius;
		return hit;
	}

	SweptHit2D SweptIntersects(const Capsule2f& a, const Vector2f& deltaA,
		                        const Capsule2f& b, const Vector2f& deltaB)
	{
		SweptHit2D hit;

		Sphere2f sa = a.ToBoundingCircle();
		Sphere2f sb = b.ToBoundingCircle();
		SweptHit2D broad = SweptIntersects(sa, deltaA, sb, deltaB);
		if (!broad.hit)
			return hit;

		auto overlaps = [&](float t) -> bool
		{
			Capsule2f at = CapsuleTranslated2(a, deltaA, t);
			Capsule2f bt = CapsuleTranslated2(b, deltaB, t);
			return Intersects(at, bt);
		};

		float toi = 0.0f;
		if (!BinarySearchToi(overlaps, toi))
			return hit;

		hit.hit = true;
		hit.t = toi;
		Capsule2f at = CapsuleTranslated2(a, deltaA, toi);
		Capsule2f bt = CapsuleTranslated2(b, deltaB, toi);
		Vector2f pa, pb;
		ClosestPointsOnSegments(at.PointA, at.PointB, bt.PointA, bt.PointB, pa, pb);
		ContactNormalFromPoints2(pb, pa, hit.normal);
		hit.point = pb + hit.normal * bt.Radius;
		return hit;
	}

	SweptHit2D SweptIntersects(const Capsule2f& capsule, const Vector2f& delta, const AABox2f& box)
	{
		SweptHit2D hit;

		Sphere2f sa = capsule.ToBoundingCircle();
		Sphere2f sb = box.ToBoundingCircle();
		SweptHit2D broad = SweptIntersects(sa, delta, sb, Vector2f::ZERO);
		if (!broad.hit)
			return hit;

		auto overlaps = [&](float t) -> bool
		{
			return Intersects(CapsuleTranslated2(capsule, delta, t), box);
		};

		float toi = 0.0f;
		if (!BinarySearchToi(overlaps, toi))
			return hit;

		hit.hit = true;
		hit.t = toi;
		Capsule2f at = CapsuleTranslated2(capsule, delta, toi);
		Vector2f ab = at.PointB - at.PointA;
		float lo = 0.0f, hi = 1.0f;
		for (int i = 0; i < 16; ++i)
		{
			float m1 = lo + (hi - lo) * (1.0f / 3.0f);
			float m2 = lo + (hi - lo) * (2.0f / 3.0f);
			float dm1 = DistanceSqPointAabb2(at.PointA + ab * m1, box);
			float dm2 = DistanceSqPointAabb2(at.PointA + ab * m2, box);
			if (dm1 < dm2)
				hi = m2;
			else
				lo = m1;
		}
		float bestT = 0.5f * (lo + hi);
		Vector2f onSeg = at.PointA + ab * bestT;
		Vector2f onBox = ClosestPointOnAabb2(onSeg, box);
		ContactNormalFromPoints2(onBox, onSeg, hit.normal);
		hit.point = onBox;
		return hit;
	}

	SweptHit2D SweptIntersects(const Capsule2f& capsule, const Vector2f& delta, const Box2f& box)
	{
		SweptHit2D hit;

		Sphere2f sa = capsule.ToBoundingCircle();
		float rb = Vector2f(box.Extent[0], box.Extent[1]).Magnitude();
		Sphere2f sb(box.Center, rb);
		SweptHit2D broad = SweptIntersects(sa, delta, sb, Vector2f::ZERO);
		if (!broad.hit)
			return hit;

		auto overlaps = [&](float t) -> bool
		{
			return Intersects(CapsuleTranslated2(capsule, delta, t), box);
		};

		float toi = 0.0f;
		if (!BinarySearchToi(overlaps, toi))
			return hit;

		hit.hit = true;
		hit.t = toi;
		Capsule2f at = CapsuleTranslated2(capsule, delta, toi);
		Vector2f ab = at.PointB - at.PointA;
		float lo = 0.0f, hi = 1.0f;
		for (int i = 0; i < 16; ++i)
		{
			float m1 = lo + (hi - lo) * (1.0f / 3.0f);
			float m2 = lo + (hi - lo) * (2.0f / 3.0f);
			float dm1 = DistanceSqPointObb2(at.PointA + ab * m1, box);
			float dm2 = DistanceSqPointObb2(at.PointA + ab * m2, box);
			if (dm1 < dm2)
				hi = m2;
			else
				lo = m1;
		}
		float bestT = 0.5f * (lo + hi);
		Vector2f onSeg = at.PointA + ab * bestT;
		Vector2f onBox = ClosestPointOnObb2(onSeg, box);
		ContactNormalFromPoints2(onBox, onSeg, hit.normal);
		hit.point = onBox;
		return hit;
	}
} // Dark::Collision
