#include "SweptCollision.h"
#include "StaticCollision.h"
#include "Math/MathDefines.h"
#include "Math/Ray3f.h"
#include "Math/Ray2f.h"
#include <cmath>
#include <algorithm>

namespace Dark::Collision
{
	using namespace Math;

	// ─────────────────────────────────────────────────────────────────────
	// Shared solvers
	// ─────────────────────────────────────────────────────────────────────

	// Moving sphere centers: C0 + V t, radii ra+rb. First root in [0,1].
	static bool SolveSweptSpheres(const Vector3f& c0, const Vector3f& v,
		                            float radiusSum, float& tOut)
	{
		// |c0 + v t|^2 = R^2
		const float R2 = radiusSum * radiusSum;
		const float a = v.Dot(v);
		const float b = 2.0f * c0.Dot(v);
		const float c = c0.Dot(c0) - R2;

		if (c <= 0.0f)
		{
			// Already overlapping
			tOut = 0.0f;
			return true;
		}

		if (a < Epsilon)
		{
			// No relative motion
			return false;
		}

		const float discr = b * b - 4.0f * a * c;
		if (discr < 0.0f)
			return false;

		const float sqrtD = sqrtf(discr);
		float t = (-b - sqrtD) / (2.0f * a);
		if (t < 0.0f)
			t = (-b + sqrtD) / (2.0f * a);

		if (t < 0.0f || t > 1.0f)
			return false;

		tOut = t;
		return true;
	}

	static bool SolveSweptSpheres2(const Vector2f& c0, const Vector2f& v,
		                            float radiusSum, float& tOut)
	{
		const float R2 = radiusSum * radiusSum;
		const float a = v.Dot(v);
		const float b = 2.0f * c0.Dot(v);
		const float c = c0.Dot(c0) - R2;

		if (c <= 0.0f)
		{
			tOut = 0.0f;
			return true;
		}
		if (a < Epsilon)
			return false;

		const float discr = b * b - 4.0f * a * c;
		if (discr < 0.0f)
			return false;

		const float sqrtD = sqrtf(discr);
		float t = (-b - sqrtD) / (2.0f * a);
		if (t < 0.0f)
			t = (-b + sqrtD) / (2.0f * a);
		if (t < 0.0f || t > 1.0f)
			return false;
		tOut = t;
		return true;
	}

	// Swept AABB vs AABB with relative velocity v (A moves relative to B).
	// Slab method in time.
	static bool SweptAabbAabb(const Aabb3f& a, const Aabb3f& b, const Vector3f& v, float& tOut)
	{
		// Already overlapping?
		if (a.Intersects(b))
		{
			tOut = 0.0f;
			return true;
		}

		float tEnter = 0.0f;
		float tLeave = 1.0f;

		for (int i = 0; i < 3; ++i)
		{
			float minA = a.Min[i], maxA = a.Max[i];
			float minB = b.Min[i], maxB = b.Max[i];
			float vi = v[i];

			if (fabsf(vi) < Epsilon)
			{
				if (maxA < minB || maxB < minA)
					return false;
				continue;
			}

			// Times when edges touch
			float t0 = (minB - maxA) / vi; // A.max meets B.min
			float t1 = (maxB - minA) / vi; // A.min meets B.max
			if (t0 > t1)
				std::swap(t0, t1);

			tEnter = std::max(tEnter, t0);
			tLeave = std::min(tLeave, t1);
			if (tEnter > tLeave)
				return false;
		}

		if (tEnter > 1.0f || tLeave < 0.0f)
			return false;

		tOut = std::max(tEnter, 0.0f);
		return tOut <= 1.0f;
	}

	static bool SweptAabbAabb2(const AABox2f& a, const AABox2f& b, const Vector2f& v, float& tOut)
	{
		if (a.Intersects(b))
		{
			tOut = 0.0f;
			return true;
		}

		float tEnter = 0.0f;
		float tLeave = 1.0f;

		for (int i = 0; i < 2; ++i)
		{
			float minA = a.Min[i], maxA = a.Max[i];
			float minB = b.Min[i], maxB = b.Max[i];
			float vi = v[i];

			if (fabsf(vi) < Epsilon)
			{
				if (maxA < minB || maxB < minA)
					return false;
				continue;
			}

			float t0 = (minB - maxA) / vi;
			float t1 = (maxB - minA) / vi;
			if (t0 > t1)
				std::swap(t0, t1);

			tEnter = std::max(tEnter, t0);
			tLeave = std::min(tLeave, t1);
			if (tEnter > tLeave)
				return false;
		}

		if (tEnter > 1.0f || tLeave < 0.0f)
			return false;

		tOut = std::max(tEnter, 0.0f);
		return tOut <= 1.0f;
	}

	// Sphere vs frustum with linear motion: half-space constraints on t.
	static bool SweptSphereFrustum(const Sphere3f& s, const Vector3f& delta,
		                            const Frustum3f& f, float& tOut)
	{
		// At time t: n·(c + t v) + d >= -r  for all planes
		// n·c + d + r + t (n·v) >= 0
		float tEnter = 0.0f;
		float tLeave = 1.0f;

		for (const auto& plane : f.Planes)
		{
			float nd = plane.Normal().Dot(delta);
			float base = plane.DistanceToPoint(s.Center) + s.Radius; // need base + t*nd >= 0

			if (nd > Epsilon)
			{
				// t >= -base/nd
				float te = -base / nd;
				tEnter = std::max(tEnter, te);
			}
			else if (nd < -Epsilon)
			{
				// t <= -base/nd
				float tl = -base / nd;
				tLeave = std::min(tLeave, tl);
			}
			else if (base < 0.0f)
			{
				return false; // parallel and outside for all t
			}

			if (tEnter > tLeave)
				return false;
		}

		if (tLeave < 0.0f || tEnter > 1.0f)
			return false;

		tOut = std::max(tEnter, 0.0f);
		return tOut <= 1.0f && tOut <= tLeave;
	}

	// ─────────────────────────────────────────────────────────────────────
	// Point sweeps
	// ─────────────────────────────────────────────────────────────────────

	SweptHit3D SweptIntersects(const Vector3f& p0, const Vector3f& delta, const Sphere3f& sphere)
	{
		SweptHit3D hit;
		Ray3f ray(p0, delta);
		// Direction not unit: scale t from ray param
		float len = delta.Magnitude();
		if (len < Epsilon)
		{
			if (Intersects(p0, sphere))
			{
				hit.hit = true;
				hit.t = 0.0f;
				hit.point = p0;
			}
			return hit;
		}

		Vector3f dir = delta * (1.0f / len);
		Ray3f unitRay(p0, dir);
		RayHit3D rh = Intersect(unitRay, sphere);
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

	SweptHit3D SweptIntersects(const Vector3f& p0, const Vector3f& delta, const Aabb3f& box)
	{
		SweptHit3D hit;
		float len = delta.Magnitude();
		if (len < Epsilon)
		{
			if (Intersects(p0, box))
			{
				hit.hit = true;
				hit.t = 0.0f;
				hit.point = p0;
			}
			return hit;
		}

		Vector3f dir = delta * (1.0f / len);
		RayHit3D rh = Intersect(Ray3f(p0, dir), box);
		if (!rh.hit)
			return hit;

		float t = rh.t / len;
		if (t > 1.0f)
			return hit;

		hit.hit = true;
		hit.t = std::max(t, 0.0f);
		hit.point = p0 + delta * hit.t;
		hit.normal = rh.normal;
		return hit;
	}

	SweptHit3D SweptIntersects(const Vector3f& p0, const Vector3f& delta, const Box3f& box)
	{
		SweptHit3D hit;
		float len = delta.Magnitude();
		if (len < Epsilon)
		{
			if (Intersects(p0, box))
			{
				hit.hit = true;
				hit.t = 0.0f;
				hit.point = p0;
			}
			return hit;
		}

		Vector3f dir = delta * (1.0f / len);
		RayHit3D rh = Intersect(Ray3f(p0, dir), box);
		if (!rh.hit)
			return hit;

		float t = rh.t / len;
		if (t > 1.0f)
			return hit;

		hit.hit = true;
		hit.t = std::max(t, 0.0f);
		hit.point = p0 + delta * hit.t;
		hit.normal = rh.normal;
		return hit;
	}

	SweptHit3D SweptIntersects(const Vector3f& p0, const Vector3f& delta, const Frustum3f& frustum)
	{
		SweptHit3D hit;
		float len = delta.Magnitude();
		if (len < Epsilon)
		{
			if (Intersects(p0, frustum))
			{
				hit.hit = true;
				hit.t = 0.0f;
				hit.point = p0;
			}
			return hit;
		}

		Vector3f dir = delta * (1.0f / len);
		RayHit3D rh = Intersect(Ray3f(p0, dir), frustum);
		if (!rh.hit)
			return hit;

		float t = rh.t / len;
		if (t > 1.0f)
			return hit;

		hit.hit = true;
		hit.t = std::max(t, 0.0f);
		hit.point = p0 + delta * hit.t;
		return hit;
	}

	// ─────────────────────────────────────────────────────────────────────
	// Sphere sweeps
	// ─────────────────────────────────────────────────────────────────────

	SweptHit3D SweptIntersects(const Sphere3f& a, const Vector3f& deltaA,
		                        const Sphere3f& b, const Vector3f& deltaB)
	{
		SweptHit3D hit;
		Vector3f c0 = a.Center - b.Center;
		Vector3f v  = deltaA - deltaB;
		float t = 0.0f;
		if (!SolveSweptSpheres(c0, v, a.Radius + b.Radius, t))
			return hit;

		hit.hit = true;
		hit.t = t;
		Vector3f ca = a.Center + deltaA * t;
		Vector3f cb = b.Center + deltaB * t;
		hit.point = ca + (cb - ca) * (a.Radius / std::max(a.Radius + b.Radius, Epsilon));
		hit.normal = ca - cb;
		if (hit.normal.MagnitudeSqrd() > Epsilon)
			hit.normal.Normalize();
		return hit;
	}

	SweptHit3D SweptIntersects(const Sphere3f& sphere, const Vector3f& delta, const Aabb3f& box)
	{
		// Expand AABB by radius, then raycast center
		Aabb3f expanded(
			box.Min - Vector3f(sphere.Radius, sphere.Radius, sphere.Radius),
			box.Max + Vector3f(sphere.Radius, sphere.Radius, sphere.Radius));

		return SweptIntersects(sphere.Center, delta, expanded);
	}

	SweptHit3D SweptIntersects(const Sphere3f& sphere, const Vector3f& delta, const Box3f& box)
	{
		// Conservative: sweep bounding sphere of OBB, then refine with
		// binary search using static sphere-OBB tests.
		SweptHit3D hit;
		Vector3f extents(
			box.Extent[0], box.Extent[1], box.Extent[2]);
		float cornerR = extents.Magnitude();
		Sphere3f outer(box.Center, cornerR);

		SweptHit3D broad = SweptIntersects(sphere, delta, outer, Vector3f::ZERO);
		if (!broad.hit)
			return hit;

		// Binary search first TOI with exact static test
		float lo = 0.0f, hi = 1.0f;
		if (Intersects(sphere, box))
		{
			hit.hit = true;
			hit.t = 0.0f;
			hit.point = sphere.Center;
			return hit;
		}

		// If no hit at end either, may still clip mid-path
		bool endHit = false;
		{
			Sphere3f endS(sphere.Center + delta, sphere.Radius);
			endHit = Intersects(endS, box);
		}

		if (!broad.hit && !endHit)
		{
			// sample
			bool any = false;
			for (int i = 1; i <= 8; ++i)
			{
				float t = static_cast<float>(i) / 8.0f;
				Sphere3f s(sphere.Center + delta * t, sphere.Radius);
				if (Intersects(s, box))
				{
					any = true;
					hi = t;
					break;
				}
			}
			if (!any)
				return hit;
		}
		else
		{
			lo = broad.hit ? broad.t : 0.0f;
			hi = 1.0f;
		}

		for (int i = 0; i < 24; ++i)
		{
			float mid = 0.5f * (lo + hi);
			Sphere3f s(sphere.Center + delta * mid, sphere.Radius);
			if (Intersects(s, box))
				hi = mid;
			else
				lo = mid;
		}

		hit.hit = true;
		hit.t = hi;
		hit.point = sphere.Center + delta * hi;
		return hit;
	}

	SweptHit3D SweptIntersects(const Sphere3f& sphere, const Vector3f& delta,
		                        const Frustum3f& frustum)
	{
		SweptHit3D hit;
		float t = 0.0f;
		if (!SweptSphereFrustum(sphere, delta, frustum, t))
			return hit;
		hit.hit = true;
		hit.t = t;
		hit.point = sphere.Center + delta * t;
		return hit;
	}

	// ─────────────────────────────────────────────────────────────────────
	// AABB / OBB sweeps
	// ─────────────────────────────────────────────────────────────────────

	SweptHit3D SweptIntersects(const Aabb3f& a, const Vector3f& deltaA,
		                        const Aabb3f& b, const Vector3f& deltaB)
	{
		SweptHit3D hit;
		Vector3f v = deltaA - deltaB;
		float t = 0.0f;
		if (!SweptAabbAabb(a, b, v, t))
			return hit;

		hit.hit = true;
		hit.t = t;
		Vector3f ca = a.Center() + deltaA * t;
		Vector3f cb = b.Center() + deltaB * t;
		hit.point = (ca + cb) * 0.5f;
		Vector3f n = ca - cb;
		if (n.MagnitudeSqrd() > Epsilon)
		{
			// Approximate normal from dominant axis of separation at TOI
			n.Normalize();
			hit.normal = n;
		}
		return hit;
	}

	SweptHit3D SweptIntersects(const Box3f& a, const Vector3f& deltaA,
		                        const Box3f& b, const Vector3f& deltaB)
	{
		// Relative motion of centers; orientations fixed.
		// Broadphase: bounding spheres, then binary search with static OBB test.
		SweptHit3D hit;

		float ra = Vector3f(a.Extent[0], a.Extent[1], a.Extent[2]).Magnitude();
		float rb = Vector3f(b.Extent[0], b.Extent[1], b.Extent[2]).Magnitude();
		Sphere3f sa(a.Center, ra);
		Sphere3f sb(b.Center, rb);

		SweptHit3D broad = SweptIntersects(sa, deltaA, sb, deltaB);
		if (!broad.hit)
		{
			// Still may touch if spheres miss due to... spheres are conservative outer, so if spheres miss, OBBs miss.
			return hit;
		}

		if (Intersects(a, b))
		{
			hit.hit = true;
			hit.t = 0.0f;
			hit.point = (a.Center + b.Center) * 0.5f;
			return hit;
		}

		float lo = 0.0f;
		float hi = broad.t;
		// Expand search to 1 in case broad TOI is late contact of spheres after OBB contact
		// Actually sphere TOI is lower bound for OBB TOI (outer spheres).
		// OBB can only hit at t >= sphere miss... wait outer spheres: if OBBs hit, spheres hit.
		// Sphere TOI <= OBB TOI. So first OBB hit is in [broad.t, 1] only if already
		// overlapping spheres early... Outer sphere TOI is when spheres first touch,
		// which is necessary before OBB touch only if spheres are outer — OBB is inside
		// sphere, so OBB contact implies spheres already overlap. So OBB TOI >= 0 and
		// spheres overlap for t in [tSphereEnter, tSphereExit]. Binary search [0,1]
		// with static OBB is safest.

		lo = 0.0f;
		hi = 1.0f;
		bool found = false;
		// Find any hit via sampling then refine
		const int samples = 16;
		for (int i = 0; i <= samples; ++i)
		{
			float t = static_cast<float>(i) / static_cast<float>(samples);
			Box3f at = a;
			Box3f bt = b;
			at.Center = a.Center + deltaA * t;
			bt.Center = b.Center + deltaB * t;
			if (Intersects(at, bt))
			{
				found = true;
				hi = t;
				break;
			}
			lo = t;
		}
		if (!found)
			return hit;

		for (int i = 0; i < 24; ++i)
		{
			float mid = 0.5f * (lo + hi);
			Box3f at = a;
			Box3f bt = b;
			at.Center = a.Center + deltaA * mid;
			bt.Center = b.Center + deltaB * mid;
			if (Intersects(at, bt))
				hi = mid;
			else
				lo = mid;
		}

		hit.hit = true;
		hit.t = hi;
		hit.point = a.Center + deltaA * hi;
		Vector3f n = (a.Center + deltaA * hi) - (b.Center + deltaB * hi);
		if (n.MagnitudeSqrd() > Epsilon)
		{
			n.Normalize();
			hit.normal = n;
		}
		return hit;
	}

	SweptHit3D SweptIntersects(const Aabb3f& box, const Vector3f& delta, const Frustum3f& frustum)
	{
		// Conservative: bounding sphere of AABB
		Sphere3f s = box.ToBoundingSphere();
		return SweptIntersects(s, delta, frustum);
	}

	SweptHit3D SweptIntersects(const Box3f& box, const Vector3f& delta, const Frustum3f& frustum)
	{
		float r = Vector3f(box.Extent[0], box.Extent[1], box.Extent[2]).Magnitude();
		return SweptIntersects(Sphere3f(box.Center, r), delta, frustum);
	}

	// ─────────────────────────────────────────────────────────────────────
	// 2D
	// ─────────────────────────────────────────────────────────────────────

	SweptHit2D SweptIntersects(const Vector2f& p0, const Vector2f& delta, const Sphere2f& circle)
	{
		SweptHit2D hit;
		float len = delta.Magnitude();
		if (len < Epsilon)
		{
			if (Intersects(p0, circle))
			{
				hit.hit = true;
				hit.t = 0.0f;
				hit.point = p0;
			}
			return hit;
		}
		Vector2f dir = delta * (1.0f / len);
        RayHit2D rh;

		if (!Intersect(Ray2f(p0, dir), circle, rh))
		{
            hit.hit = false;
            return hit;
		}

		float t = rh.t / len;
		if (t > 1.0f)
			return hit;
		hit.hit = true;
		hit.t = std::max(t, 0.0f);
		hit.point = p0 + delta * hit.t;
		hit.normal = rh.normal;
		return hit;
	}

	SweptHit2D SweptIntersects(const Vector2f& p0, const Vector2f& delta, const Aabb2f& box)
	{
		SweptHit2D hit;
		float len = delta.Magnitude();
		if (len < Epsilon)
		{
			if (Intersects(p0, box))
			{
				hit.hit = true;
				hit.t = 0.0f;
				hit.point = p0;
			}
			return hit;
		}
		Vector2f dir = delta * (1.0f / len);
		RayHit2D rh{};
		if (!Intersect(Ray2f(p0, dir), box, rh) || !rh.hit)
			return hit;
		float t = rh.t / len;
		if (t > 1.0f)
			return hit;
		hit.hit = true;
		hit.t = std::max(t, 0.0f);
		hit.point = p0 + delta * hit.t;
		return hit;
	}

	SweptHit2D SweptIntersects(const Vector2f& p0, const Vector2f& delta, const Box2f& box)
	{
		SweptHit2D hit;
		float len = delta.Magnitude();
		if (len < Epsilon)
		{
			if (Intersects(p0, box))
			{
				hit.hit = true;
				hit.t = 0.0f;
				hit.point = p0;
			}
			return hit;
		}
		Vector2f dir = delta * (1.0f / len);
		RayHit2D rh{};
		if (!Intersect(Ray2f(p0, dir), box, rh) || !rh.hit)
			return hit;
		float t = rh.t / len;
		if (t > 1.0f)
			return hit;
		hit.hit = true;
		hit.t = std::max(t, 0.0f);
		hit.point = p0 + delta * hit.t;
		return hit;
	}

	SweptHit2D SweptIntersects(const Sphere2f& a, const Vector2f& deltaA,
		                        const Sphere2f& b, const Vector2f& deltaB)
	{
		SweptHit2D hit;
		Vector2f c0 = a.Center - b.Center;
		Vector2f v  = deltaA - deltaB;
		float t = 0.0f;
		if (!SolveSweptSpheres2(c0, v, a.Radius + b.Radius, t))
			return hit;
		hit.hit = true;
		hit.t = t;
		Vector2f ca = a.Center + deltaA * t;
		Vector2f cb = b.Center + deltaB * t;
		hit.point = (ca + cb) * 0.5f;
		hit.normal = ca - cb;
		if (hit.normal.MagnitudeSqrd() > Epsilon)
			hit.normal.Normalize();
		return hit;
	}

	SweptHit2D SweptIntersects(const Sphere2f& circle, const Vector2f& delta, const Aabb2f& box)
	{
		Aabb2f expanded(
			box.Min - Vector2f(circle.Radius, circle.Radius),
			box.Max + Vector2f(circle.Radius, circle.Radius));
		return SweptIntersects(circle.Center, delta, expanded);
	}

	SweptHit2D SweptIntersects(const Sphere2f& circle, const Vector2f& delta, const Box2f& box)
	{
		SweptHit2D hit;
		if (Intersects(circle, box))
		{
			hit.hit = true;
			hit.t = 0.0f;
			hit.point = circle.Center;
			return hit;
		}

		float lo = 0.0f, hi = 1.0f;
		bool found = false;
		for (int i = 1; i <= 16; ++i)
		{
			float t = static_cast<float>(i) / 16.0f;
			Sphere2f s(circle.Center + delta * t, circle.Radius);
			if (Intersects(s, box))
			{
				found = true;
				hi = t;
				break;
			}
			lo = t;
		}
		if (!found)
			return hit;

		for (int i = 0; i < 20; ++i)
		{
			float mid = 0.5f * (lo + hi);
			Sphere2f s(circle.Center + delta * mid, circle.Radius);
			if (Intersects(s, box))
				hi = mid;
			else
				lo = mid;
		}
		hit.hit = true;
		hit.t = hi;
		hit.point = circle.Center + delta * hi;
		return hit;
	}

	SweptHit2D SweptIntersects(const Aabb2f& a, const Vector2f& deltaA,
		                        const Aabb2f& b, const Vector2f& deltaB)
	{
		SweptHit2D hit;
		float t = 0.0f;
		if (!SweptAabbAabb2(a, b, deltaA - deltaB, t))
			return hit;
		hit.hit = true;
		hit.t = t;
		hit.point = a.Center() + deltaA * t;
		return hit;
	}

	SweptHit2D SweptIntersects(const Box2f& a, const Vector2f& deltaA,
		                        const Box2f& b, const Vector2f& deltaB)
	{
		SweptHit2D hit;
		if (Intersects(a, b))
		{
			hit.hit = true;
			hit.t = 0.0f;
			hit.point = (a.Center + b.Center) * 0.5f;
			return hit;
		}

		float lo = 0.0f, hi = 1.0f;
		bool found = false;
		for (int i = 1; i <= 16; ++i)
		{
			float t = static_cast<float>(i) / 16.0f;
			Box2f at = a;
			Box2f bt = b;
			at.Center = a.Center + deltaA * t;
			bt.Center = b.Center + deltaB * t;
			if (Intersects(at, bt))
			{
				found = true;
				hi = t;
				break;
			}
			lo = t;
		}
		if (!found)
			return hit;

		for (int i = 0; i < 20; ++i)
		{
			float mid = 0.5f * (lo + hi);
			Box2f at = a;
			Box2f bt = b;
			at.Center = a.Center + deltaA * mid;
			bt.Center = b.Center + deltaB * mid;
			if (Intersects(at, bt))
				hi = mid;
			else
				lo = mid;
		}
		hit.hit = true;
		hit.t = hi;
		hit.point = a.Center + deltaA * hi;
		return hit;
	}
} // Dark::Collision
