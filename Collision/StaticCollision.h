#pragma once

#include "HitResult.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Math/Ray2f.h"
#include "Math/Ray3f.h"
#include "Math/Sphere2f.h"
#include "Math/Sphere3f.h"
#include "Math/Aabb2f.h"
#include "Math/Aabb3f.h"
#include "Math/Box2f.h"
#include "Math/Box3f.h"
#include "Render/Frustum3f.h"

namespace Dark
{
	namespace Collision
	{

		// =====================================================================
		// 2D static tests
		// =====================================================================
		bool Intersects(const Math::Vector2f& point, const Math::Sphere2f& circle);
		bool Intersects(const Math::Vector2f& point, const Math::Aabb2f& box);
		bool Intersects(const Math::Vector2f& point, const Math::Box2f& box);

		bool Intersects(const Math::Sphere2f& a, const Math::Sphere2f& b);
		bool Intersects(const Math::Sphere2f& circle, const Math::Aabb2f& box);
		bool Intersects(const Math::Sphere2f& circle, const Math::Box2f& box);

		bool Intersects(const Math::Aabb2f& a, const Math::Aabb2f& b);
		bool Intersects(const Math::Aabb2f& aabb, const Math::Box2f& box);
		bool Intersects(const Math::Box2f& a, const Math::Box2f& b);

		RayHit2D Intersect(const Math::Ray2f& ray, const Math::Sphere2f& circle);
		RayHit2D Intersect(const Math::Ray2f& ray, const Math::Aabb2f& box);
		RayHit2D Intersect(const Math::Ray2f& ray, const Math::Box2f& box);

		inline bool Intersects(const Math::Sphere2f& c, const Math::Vector2f& p) { return Intersects(p, c); }
		inline bool Intersects(const Math::Aabb2f& b, const Math::Vector2f& p) { return Intersects(p, b); }
		inline bool Intersects(const Math::Box2f& b, const Math::Vector2f& p) { return Intersects(p, b); }
		inline bool Intersects(const Math::Aabb2f& b, const Math::Sphere2f& c) { return Intersects(c, b); }
		inline bool Intersects(const Math::Box2f& b, const Math::Sphere2f& c) { return Intersects(c, b); }
		inline bool Intersects(const Math::Box2f& b, const Math::Aabb2f& a) { return Intersects(a, b); }


		// =====================================================================
		// Static intersection tests (no motion).
		// Overloads cover point / ray / sphere / AABB / OBB / frustum.
		// Ray queries return RayHit with parametric t along the ray.
		// =====================================================================

		// ── Point vs * ────────────────────────────────────────────────────────
		bool Intersects(const Math::Vector3f& point, const Math::Sphere3f& sphere);
		bool Intersects(const Math::Vector3f& point, const Math::Aabb3f& box);
		bool Intersects(const Math::Vector3f& point, const Math::Box3f& box);
		bool Intersects(const Math::Vector3f& point, const Math::Frustum3f& frustum);

		// ── Sphere vs * ───────────────────────────────────────────────────────
		bool Intersects(const Math::Sphere3f& a, const Math::Sphere3f& b);
		bool Intersects(const Math::Sphere3f& sphere, const Math::Aabb3f& box);
		bool Intersects(const Math::Sphere3f& sphere, const Math::Box3f& box);
		bool Intersects(const Math::Sphere3f& sphere, const Math::Frustum3f& frustum);

		// ── AABB vs * ─────────────────────────────────────────────────────────
		bool Intersects(const Math::Aabb3f& a, const Math::Aabb3f& b);
		bool Intersects(const Math::Aabb3f& aabb, const Math::Box3f& box);
		bool Intersects(const Math::Aabb3f& aabb, const Math::Frustum3f& frustum);

		// ── OBB vs * ──────────────────────────────────────────────────────────
		bool Intersects(const Math::Box3f& a, const Math::Box3f& b);
		bool Intersects(const Math::Box3f& box, const Math::Frustum3f& frustum);

		// ── Frustum vs Frustum ────────────────────────────────────────────────
		// Convex half-space intersection (alternating projection).
		bool Intersects(const Math::Frustum3f& a, const Math::Frustum3f& b);

		// ── Ray queries (static geometry; t = distance if dir is unit length) ─
		RayHit3D Intersect(const Math::Ray3f& ray, const Math::Sphere3f& sphere);
		RayHit3D Intersect(const Math::Ray3f& ray, const Math::Aabb3f& box);
		RayHit3D Intersect(const Math::Ray3f& ray, const Math::Box3f& box);
		// Frustum: first plane-clip entry along ray (may start inside → t=0).
		RayHit3D Intersect(const Math::Ray3f& ray, const Math::Frustum3f& frustum);

		// Symmetric helpers so order doesn't matter in generic code
		inline bool Intersects(const Math::Sphere3f& s, const Math::Vector3f& p) { return Intersects(p, s); }
		inline bool Intersects(const Math::Aabb3f& b, const Math::Vector3f& p)   { return Intersects(p, b); }
		inline bool Intersects(const Math::Box3f& b, const Math::Vector3f& p)    { return Intersects(p, b); }
		inline bool Intersects(const Math::Frustum3f& f, const Math::Vector3f& p){ return Intersects(p, f); }
		inline bool Intersects(const Math::Aabb3f& b, const Math::Sphere3f& s)   { return Intersects(s, b); }
		inline bool Intersects(const Math::Box3f& b, const Math::Sphere3f& s)    { return Intersects(s, b); }
		inline bool Intersects(const Math::Frustum3f& f, const Math::Sphere3f& s){ return Intersects(s, f); }
		inline bool Intersects(const Math::Box3f& b, const Math::Aabb3f& a)      { return Intersects(a, b); }
		inline bool Intersects(const Math::Frustum3f& f, const Math::Aabb3f& a)  { return Intersects(a, f); }
		inline bool Intersects(const Math::Frustum3f& f, const Math::Box3f& b)   { return Intersects(b, f); }

	}
}
