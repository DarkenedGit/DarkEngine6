#pragma once

#include "HitResult.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Math/Sphere2f.h"
#include "Math/Sphere3f.h"
#include "Math/AABox2f.h"
#include "Math/AABox3f.h"
#include "Math/Box2f.h"
#include "Math/Box3f.h"
#include "Render/Frustum3f.h"

namespace Dark
{
    namespace Collision
    {
    // =====================================================================
    // 2D swept tests
    // =====================================================================
    SweptHit2D SweptIntersects(const Math::Vector2f& p0, const Math::Vector2f& delta, const Math::Sphere2f& circle);
    SweptHit2D SweptIntersects(const Math::Vector2f& p0, const Math::Vector2f& delta, const Math::Aabb2f& box);
    SweptHit2D SweptIntersects(const Math::Vector2f& p0, const Math::Vector2f& delta, const Math::Box2f& box);

    SweptHit2D        SweptIntersects(const Math::Sphere2f& a, const Math::Vector2f& deltaA, const Math::Sphere2f& b, const Math::Vector2f& deltaB);
    inline SweptHit2D SweptIntersects(const Math::Sphere2f& a, const Math::Vector2f& deltaA, const Math::Sphere2f& b)
    {
        return SweptIntersects(a, deltaA, b, Math::Vector2f::ZERO);
    }

    SweptHit2D SweptIntersects(const Math::Sphere2f& circle, const Math::Vector2f& delta, const Math::Aabb2f& box);
    SweptHit2D SweptIntersects(const Math::Sphere2f& circle, const Math::Vector2f& delta, const Math::Box2f& box);

    SweptHit2D        SweptIntersects(const Math::Aabb2f& a, const Math::Vector2f& deltaA, const Math::Aabb2f& b, const Math::Vector2f& deltaB);
    inline SweptHit2D SweptIntersects(const Math::Aabb2f& a, const Math::Vector2f& deltaA, const Math::Aabb2f& b)
    {
        return SweptIntersects(a, deltaA, b, Math::Vector2f::ZERO);
    }

    SweptHit2D SweptIntersects(const Math::Box2f& a, const Math::Vector2f& deltaA, const Math::Box2f& b, const Math::Vector2f& deltaB);

    // =====================================================================
    // Continuous (linear) collision over time interval t ∈ [0, 1].
    //
    // Motion model:
    //   shape A at time t: center/position = P0 + deltaA * t
    //   shape B at time t: center/position = Q0 + deltaB * t
    //
    // Orientation is assumed fixed during the sweep (no angular velocity).
    // Returns first time-of-impact (TOI). If already overlapping at t=0, t=0.
    // =====================================================================

    // ── Point (moving) vs static * ────────────────────────────────────────
    // Point path: p0 + delta * t
    SweptHit3D SweptIntersects(const Math::Vector3f& p0, const Math::Vector3f& delta, const Math::Sphere3f& sphere);
    SweptHit3D SweptIntersects(const Math::Vector3f& p0, const Math::Vector3f& delta, const Math::Aabb3f& box);
    SweptHit3D SweptIntersects(const Math::Vector3f& p0, const Math::Vector3f& delta, const Math::Box3f& box);
    SweptHit3D SweptIntersects(const Math::Vector3f& p0, const Math::Vector3f& delta, const Math::Frustum3f& frustum);

    // ── Sphere vs Sphere / AABB / OBB / Frustum ───────────────────────────
    // Each sphere may move (deltaB = 0 for a static target).
    SweptHit3D SweptIntersects(const Math::Sphere3f& a, const Math::Vector3f& deltaA, const Math::Sphere3f& b, const Math::Vector3f& deltaB);
    SweptHit3D SweptIntersects(const Math::Sphere3f& sphere, const Math::Vector3f& delta, const Math::Aabb3f& box);
    SweptHit3D SweptIntersects(const Math::Sphere3f& sphere, const Math::Vector3f& delta, const Math::Box3f& box);
    SweptHit3D SweptIntersects(const Math::Sphere3f& sphere, const Math::Vector3f& delta, const Math::Frustum3f& frustum);

    // Convenience: static target (deltaB = 0)
    inline SweptHit3D SweptIntersects(const Math::Sphere3f& a, const Math::Vector3f& deltaA, const Math::Sphere3f& b)
    {
        return SweptIntersects(a, deltaA, b, Math::Vector3f::ZERO);
    }

    // ── AABB vs AABB ──────────────────────────────────────────────────────
    SweptHit3D        SweptIntersects(const Math::Aabb3f& a, const Math::Vector3f& deltaA, const Math::Aabb3f& b, const Math::Vector3f& deltaB);
    inline SweptHit3D SweptIntersects(const Math::Aabb3f& a, const Math::Vector3f& deltaA, const Math::Aabb3f& b)
    {
        return SweptIntersects(a, deltaA, b, Math::Vector3f::ZERO);
    }

    // ── OBB vs OBB (both translate; orientations fixed) ───────────────────
    // Uses iterative / conservative relative-sphere + static SAT refinement.
    SweptHit3D        SweptIntersects(const Math::Box3f& a, const Math::Vector3f& deltaA, const Math::Box3f& b, const Math::Vector3f& deltaB);
    inline SweptHit3D SweptIntersects(const Math::Box3f& a, const Math::Vector3f& deltaA, const Math::Box3f& b)
    {
        return SweptIntersects(a, deltaA, b, Math::Vector3f::ZERO);
    }

    // AABB / OBB vs static frustum
    SweptHit3D SweptIntersects(const Math::Aabb3f& box, const Math::Vector3f& delta, const Math::Frustum3f& frustum);
    SweptHit3D SweptIntersects(const Math::Box3f& box, const Math::Vector3f& delta, const Math::Frustum3f& frustum);

    } // namespace Collision
} // namespace Dark
