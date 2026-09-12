#include "Weapons/Weapon.h"

#include "Collision/Collision.h"
#include "Math/MathDefines.h"
#include "Math/Sphere3f.h"

#include <cmath>

namespace Dark
{
    using Math::AABox3f;
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
        return AABox3f::FromCenterExtents(c, world.targetHalfExtents);
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

    bool weaponRaycastClosest(const WeaponWorldQuery& world, const Ray3f& ray, float maxDistance, WeaponHit& out)
    {
        bool  any   = false;
        float bestT = maxDistance;

        const int n = weaponTargetCount(world);
        for (int i = 0; i < n; ++i)
        {
            if (!weaponTargetAlive(world, i))
                continue;
            const Collision::RayHit3D hit = Collision::Intersect(ray, weaponTargetBox(world, i));
            if (!hit.hit || hit.t < 0.0f || hit.t > bestT)
                continue;
            bestT           = hit.t;
            any             = true;
            out.point       = hit.point;
            out.normal      = hit.normal;
            out.targetIndex = i;
            out.hitTarget   = true;
        }

        const Collision::RayHit3D ground = weaponRaycastTerrain(world, ray, bestT);
        if (ground.hit && ground.t >= 0.0f && ground.t <= bestT)
        {
            bestT           = ground.t;
            any             = true;
            out.point       = ground.point;
            out.normal      = ground.normal;
            out.targetIndex = -1;
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
            const Collision::SweptHit3D hit = Collision::SweptIntersects(ball, delta, weaponTargetBox(world, i));
            if (!hit.hit || hit.t < 0.0f || hit.t > bestT)
                continue;
            bestT            = hit.t;
            any              = true;
            best.point       = hit.point;
            best.normal      = hit.normal;
            best.targetIndex = i;
            best.hitTarget   = true;
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
                    best.targetIndex = -1;
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
                best.targetIndex = -1;
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
