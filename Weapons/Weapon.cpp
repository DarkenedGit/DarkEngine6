#include "Weapons/Weapon.h"

#include "Collision/Collision.h"
#include "Combat/HitPart.h"
#include "Combat/HitVolumes.h"
#include "Math/Capsule3f.h"
#include "Math/MathDefines.h"
#include "Math/Sphere3f.h"

#include <cmath>

namespace Dark
{
    using Math::AABox3f;
    using Math::Capsule3f;
    using Math::Ray3f;
    using Math::Sphere3f;
    using Math::Vector3f;

    int weaponTargetCount(const WeaponWorldQuery& world)
    {
        if (!world.targetCount)
            return 0;
        return world.targetCount(world.targetUser);
    }

    bool weaponTargetAlive(const WeaponWorldQuery& world, int index)
    {
        if (!world.targetAlive)
            return false;
        return world.targetAlive(world.targetUser, index);
    }

    bool weaponTargetCenter(const WeaponWorldQuery& world, int index, Vector3f& out)
    {
        if (!world.targetCenter)
            return false;
        out = world.targetCenter(world.targetUser, index);
        return true;
    }

    AABox3f weaponTargetBox(const WeaponWorldQuery& world, int index)
    {
        Vector3f c{ 0.0f, 0.0f, 0.0f };
        if (!weaponTargetCenter(world, index, c))
            return AABox3f{};
        Vector3f half = world.targetHalfExtents;
        if (world.targetHalfExtentsAt)
            half = world.targetHalfExtentsAt(world.targetUser, index);
        return AABox3f::FromCenterExtents(c, half);
    }

    void fillHitTarget(WeaponHit& out, const WeaponWorldQuery& world, int index)
    {
        out.targetIndex  = index;
        out.targetEntity = {};
        if (index >= 0 && world.targetEntityAt)
            out.targetEntity = world.targetEntityAt(world.targetUser, index);
    }

    Collision::RayHit3D weaponRaycastTerrain(const WeaponWorldQuery& world, const Ray3f& ray, float maxDistance)
    {
        if (!world.raycastTerrain)
            return {};
        return world.raycastTerrain(world.terrainUser, ray, maxDistance);
    }

    float weaponHeightAt(const WeaponWorldQuery& world, float x, float z)
    {
        if (!world.heightAt)
            return 0.0f;
        return world.heightAt(world.terrainUser, x, z);
    }

    namespace
    {
        bool segmentActive(const Combat::HitSegment& segment)
        {
            return segment.active && segment.radius > 0.0f && Combat::hitPartIndex(segment.part) >= 0;
        }

        Vector3f shellPoint(const Capsule3f& capsule, const Vector3f& from)
        {
            const Vector3f onSeg  = capsule.ClosestPointOnSegment(from);
            Vector3f       radial = from - onSeg;
            const float    distSq = radial.MagnitudeSqrd();
            const float    radius = capsule.Radius > 0.0f ? capsule.Radius : 0.0f;
            if (distSq <= 1.0e-12f)
            {
                const Vector3f axis = capsule.Axis();
                radial              = fabsf(axis.x) > 0.9f ? Vector3f{ 0.0f, 0.0f, 1.0f } : Vector3f{ 1.0f, 0.0f, 0.0f };
                return onSeg + radial * radius;
            }
            return onSeg + radial * (radius / sqrtf(distSq));
        }

        Vector3f flatLookOrForward(const Vector3f& look)
        {
            Vector3f flat{ look.x, 0.0f, look.z };
            if (flat.MagnitudeSqrd() <= 1.0e-8f)
                return Vector3f{ 0.0f, 0.0f, 1.0f };
            flat.Normalize();
            return flat;
        }

        struct RayCapsulePick
        {
            float          t = 0.0f;
            Combat::HitPart part = Combat::HitPart::None;
            Vector3f       point{};
            Vector3f       normal{};
        };

        bool pickRayCapsule(const Ray3f& ray, const Combat::HitSegment* segments, int count, float maxT, RayCapsulePick& out)
        {
            if (!segments || count <= 0)
                return false;

            float minT = maxT;
            bool  any  = false;
            for (int i = 0; i < count; ++i)
            {
                if (!segmentActive(segments[i]))
                    continue;
                const Capsule3f           capsule(segments[i].a, segments[i].b, segments[i].radius);
                const Collision::RayHit3D hit = Collision::Intersect(ray, capsule);
                if (!hit.hit || hit.t < 0.0f || hit.t > maxT)
                    continue;
                any = true;
                if (hit.t < minT)
                    minT = hit.t;
            }
            if (!any)
                return false;

            bool found     = false;
            int  bestOrder = 0;
            for (int i = 0; i < count; ++i)
            {
                if (!segmentActive(segments[i]))
                    continue;
                const Capsule3f           capsule(segments[i].a, segments[i].b, segments[i].radius);
                const Collision::RayHit3D hit = Collision::Intersect(ray, capsule);
                if (!hit.hit || hit.t < 0.0f || hit.t > maxT || hit.t > minT + kWeaponPartTieMeters)
                    continue;
                const int order = Combat::hitPartIndex(segments[i].part);
                if (found && order >= bestOrder)
                    continue;
                found          = true;
                bestOrder      = order;
                out.t          = hit.t;
                out.part       = segments[i].part;
                out.point      = hit.point;
                out.normal     = hit.normal;
            }
            return found;
        }

        struct SweepCapsulePick
        {
            float           t = 0.0f;
            Combat::HitPart part = Combat::HitPart::None;
            Vector3f        point{};
            Vector3f        normal{};
        };

        bool pickSweepCapsule(const Sphere3f& ball, const Vector3f& delta, float pathLength, const Combat::HitSegment* segments, int count, float maxT, SweepCapsulePick& out)
        {
            if (!segments || count <= 0)
                return false;

            float minT = maxT;
            bool  any  = false;
            for (int i = 0; i < count; ++i)
            {
                if (!segmentActive(segments[i]))
                    continue;
                const Capsule3f             capsule(segments[i].a, segments[i].b, segments[i].radius);
                const Collision::SweptHit3D hit = Collision::SweptIntersects(ball, delta, capsule);
                if (!hit.hit || hit.t < 0.0f || hit.t > maxT)
                    continue;
                any = true;
                if (hit.t < minT)
                    minT = hit.t;
            }
            if (!any)
                return false;

            bool found     = false;
            int  bestOrder = 0;
            for (int i = 0; i < count; ++i)
            {
                if (!segmentActive(segments[i]))
                    continue;
                const Capsule3f             capsule(segments[i].a, segments[i].b, segments[i].radius);
                const Collision::SweptHit3D hit = Collision::SweptIntersects(ball, delta, capsule);
                if (!hit.hit || hit.t < 0.0f || hit.t > maxT)
                    continue;
                const float along = (hit.t - minT) * pathLength;
                if (along > kWeaponPartTieMeters)
                    continue;
                const int order = Combat::hitPartIndex(segments[i].part);
                if (found && order >= bestOrder)
                    continue;
                found      = true;
                bestOrder  = order;
                out.t      = hit.t;
                out.part   = segments[i].part;
                out.point  = hit.point;
                out.normal = hit.normal;
            }
            return found;
        }
    } // namespace

    const Combat::HitSegment* weaponTargetCapsules(const WeaponWorldQuery& world, int index, int& outCount)
    {
        outCount = 0;
        if (!world.targetCapsules)
            return nullptr;
        int count = 0;
        const Combat::HitSegment* segments = world.targetCapsules(world.targetUser, index, &count);
        if (!segments || count <= 0)
            return nullptr;
        outCount = count;
        return segments;
    }

    bool weaponCapsulesActive(const Combat::HitSegment* segments, int count)
    {
        if (!segments || count <= 0)
            return false;
        for (int i = 0; i < count; ++i)
        {
            if (segmentActive(segments[i]))
                return true;
        }
        return false;
    }

    int weaponSampleActiveCapsules(const Combat::HitSegment* segments, int count, const Vector3f& from, WeaponCapsuleSample* out, int outCap)
    {
        if (!segments || count <= 0 || !out || outCap <= 0)
            return 0;
        int written = 0;
        for (int i = 0; i < count && written < outCap; ++i)
        {
            if (!segmentActive(segments[i]))
                continue;
            const Capsule3f capsule(segments[i].a, segments[i].b, segments[i].radius);
            const Vector3f  point = shellPoint(capsule, from);
            const float     dx    = point.x - from.x;
            const float     dz    = point.z - from.z;
            WeaponCapsuleSample& sample = out[written];
            sample.part       = segments[i].part;
            sample.point      = point;
            sample.horizontal = sqrtf(dx * dx + dz * dz);
            sample.vertical   = point.y - from.y;
            ++written;
        }
        return written;
    }

    bool weaponPickCapsuleReach(const Combat::HitSegment* segments, int count, const Vector3f& origin, const Vector3f& lookFlat, float range, float minDot, WeaponPartContact& out)
    {
        WeaponCapsuleSample samples[32];
        const int           n = weaponSampleActiveCapsules(segments, count, origin, samples, 32);
        if (n <= 0)
            return false;

        const Vector3f look = flatLookOrForward(lookFlat);
        float          minDist = 0.0f;
        bool           any     = false;
        for (int i = 0; i < n; ++i)
        {
            const float dist = samples[i].horizontal;
            if (dist > range || dist < 1.0e-4f)
                continue;
            Vector3f to{ samples[i].point.x - origin.x, 0.0f, samples[i].point.z - origin.z };
            to *= (1.0f / dist);
            if (look.Dot(to) < minDot)
                continue;
            if (!any || dist < minDist)
                minDist = dist;
            any = true;
        }
        if (!any)
            return false;

        int best = -1;
        for (int i = 0; i < n; ++i)
        {
            const float dist = samples[i].horizontal;
            if (dist > range || dist < 1.0e-4f || dist > minDist + kWeaponPartTieMeters)
                continue;
            Vector3f to{ samples[i].point.x - origin.x, 0.0f, samples[i].point.z - origin.z };
            to *= (1.0f / dist);
            if (look.Dot(to) < minDot)
                continue;
            if (best < 0 || Combat::hitPartIndex(samples[i].part) < Combat::hitPartIndex(samples[best].part))
                best = i;
        }
        if (best < 0)
            return false;

        out.part     = samples[best].part;
        out.point    = samples[best].point;
        out.distance = samples[best].horizontal;
        Vector3f toward{ samples[best].point.x - origin.x, 0.0f, samples[best].point.z - origin.z };
        if (toward.MagnitudeSqrd() > 1.0e-8f)
        {
            toward.Normalize();
            out.facing = look.Dot(toward);
        }
        else
            out.facing = 1.0f;

        Vector3f normal = origin - samples[best].point;
        if (normal.MagnitudeSqrd() <= 1.0e-8f)
            normal = -look;
        else
            normal.Normalize();
        out.normal = normal;
        return true;
    }

    bool weaponRaycastClosest(const WeaponWorldQuery& world, const Ray3f& ray, float maxDistance, WeaponHit& out)
    {
        bool  any   = false;
        float bestT = maxDistance;

        const int n = weaponTargetCount(world);
        for (int i = 0; i < n; ++i)
        {
            if (!weaponTargetAlive(world, i))
                continue;
            int                       segmentCount = 0;
            const Combat::HitSegment* segments     = weaponTargetCapsules(world, i, segmentCount);
            if (weaponCapsulesActive(segments, segmentCount))
            {
                RayCapsulePick picked{};
                if (!pickRayCapsule(ray, segments, segmentCount, bestT, picked))
                    continue;
                bestT       = picked.t;
                any         = true;
                out.point   = picked.point;
                out.normal  = picked.normal;
                out.part    = picked.part;
                fillHitTarget(out, world, i);
                out.hitTarget = true;
                continue;
            }

            const Collision::RayHit3D hit = Collision::Intersect(ray, weaponTargetBox(world, i));
            if (!hit.hit || hit.t < 0.0f || hit.t > bestT)
                continue;
            bestT          = hit.t;
            any            = true;
            out.point      = hit.point;
            out.normal     = hit.normal;
            out.part       = Combat::HitPart::None;
            fillHitTarget(out, world, i);
            out.hitTarget  = true;
        }

        const Collision::RayHit3D ground = weaponRaycastTerrain(world, ray, bestT);
        if (ground.hit && ground.t >= 0.0f && ground.t <= bestT)
        {
            bestT           = ground.t;
            any             = true;
            out.point       = ground.point;
            out.normal      = ground.normal;
            out.part        = Combat::HitPart::None;
            fillHitTarget(out, world, -1);
            out.hitTarget   = false;
        }

        if (!any)
            return false;
        if (out.normal.MagnitudeSqrd() < 1.0e-8f)
            out.normal = Vector3f{ 0.0f, 1.0f, 0.0f };
        else
            out.normal.Normalize();
        return true;
    }

    bool weaponSweepClosest(const WeaponWorldQuery& world, const Vector3f& start, const Vector3f& delta, float radius, WeaponHit& out)
    {
        const float dist = delta.Magnitude();
        bool        any  = false;
        float       bestT = 1.0f;
        WeaponHit   best{};

        const Sphere3f ball{ start, radius > 0.0f ? radius : 0.01f };
        const int      n = weaponTargetCount(world);
        for (int i = 0; i < n; ++i)
        {
            if (!weaponTargetAlive(world, i))
                continue;
            int                       segmentCount = 0;
            const Combat::HitSegment* segments     = weaponTargetCapsules(world, i, segmentCount);
            if (weaponCapsulesActive(segments, segmentCount))
            {
                SweepCapsulePick picked{};
                if (!pickSweepCapsule(ball, delta, dist, segments, segmentCount, bestT, picked))
                    continue;
                bestT          = picked.t;
                any            = true;
                best.point     = picked.point;
                best.normal    = picked.normal;
                best.part      = picked.part;
                fillHitTarget(best, world, i);
                best.hitTarget = true;
                continue;
            }

            const Collision::SweptHit3D hit = Collision::SweptIntersects(ball, delta, weaponTargetBox(world, i));
            if (!hit.hit || hit.t < 0.0f || hit.t > bestT)
                continue;
            bestT          = hit.t;
            any            = true;
            best.point     = hit.point;
            best.normal    = hit.normal;
            best.part      = Combat::HitPart::None;
            fillHitTarget(best, world, i);
            best.hitTarget = true;
        }

        if (dist > 1.0e-6f)
        {
            Vector3f dir = delta;
            dir *= (1.0f / dist);
            const Collision::RayHit3D ground = weaponRaycastTerrain(world, Ray3f{ start, dir }, dist);
            if (ground.hit && ground.t >= 0.0f && ground.t <= dist)
            {
                const float t = ground.t / dist;
                if (t <= bestT)
                {
                    bestT            = t;
                    any              = true;
                    best.point       = ground.point;
                    best.normal      = ground.normal;
                    best.part        = Combat::HitPart::None;
                    fillHitTarget(best, world, -1);
                    best.hitTarget   = false;
                }
            }
        }

        const Vector3f end     = start + delta;
        const float    groundY = weaponHeightAt(world, end.x, end.z);
        if (end.y - radius <= groundY)
        {
            const float startClear = start.y - radius - weaponHeightAt(world, start.x, start.z);
            const float endPen     = groundY - (end.y - radius);
            float       t          = 1.0f;
            if (startClear > 0.0f && (startClear + endPen) > 1.0e-6f)
                t = startClear / (startClear + endPen);
            else if (startClear <= 0.0f)
                t = 0.0f;
            if (t < 0.0f)
                t = 0.0f;
            if (t > 1.0f)
                t = 1.0f;
            if (t <= bestT)
            {
                bestT            = t;
                any              = true;
                best.point       = start + delta * t;
                best.point.y     = groundY;
                best.normal      = Vector3f{ 0.0f, 1.0f, 0.0f };
                best.part        = Combat::HitPart::None;
                fillHitTarget(best, world, -1);
                best.hitTarget   = false;
            }
        }

        if (!any)
            return false;
        out = best;
        if (out.normal.MagnitudeSqrd() < 1.0e-8f)
            out.normal = Vector3f{ 0.0f, 1.0f, 0.0f };
        else
            out.normal.Normalize();
        return true;
    }

} // namespace Dark
