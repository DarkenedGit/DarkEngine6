#pragma once

#include "Collision/HitResult.h"
#include "Math/MathHelper.h"
#include "Math/Quaternion.h"
#include "Math/Ray3f.h"
#include "Math/Vector3f.h"
#include "Physics/PhysicsIds.h"
#include "Terrain/TerrainGrid.h"

#include <cmath>

namespace Dark
{
    namespace Physics
    {
        class PhysicsWorld;
    }

    struct ShoulderView
    {
        Math::Vector3f eye{ Math::Vect3f::ZERO };
        Math::Vector3f aim{ Math::Vect3f::ZERO };
    };

    inline Collision::RayHit3D shoulderTerrainRay(void* user, const Math::Ray3f& ray, float maxDistance)
    {
        auto* grid = static_cast<Terrain::TerrainGrid*>(user);
        if (!grid)
            return {};
        return grid->raycast(ray, maxDistance);
    }

    // Terrain heightfield plus solid Box3D shapes. The player body is `ignore` so the
    // boom, which starts inside that capsule, keeps going until a tree, wall, rock, or other character.
    struct ShoulderRayQuery
    {
        Terrain::TerrainGrid*  terrain = nullptr;
        Physics::PhysicsWorld* physics = nullptr;
        Physics::PhysicsBodyId ignore  = Physics::kNullPhysicsBody;
    };
    Collision::RayHit3D shoulderBoomRay(void* user, const Math::Ray3f& ray, float maxDistance);

    // Third-person boom. PlaySession passes shake and wetClamp for both apps.
    // A null raycast leaves the boom at full length.
    inline ShoulderView solveShoulderCamera(const Math::Vector3f& feet, float yaw, float pitch, bool crouched, float shake, float shakeTime, bool wetClamp, float waterLevel,
                                            Collision::RayHit3D (*raycast)(void* user, const Math::Ray3f& ray, float maxDistance), void* rayUser)
    {
        const Math::Vector3f target{ feet.x, feet.y + (crouched ? 0.78f : 1.15f), feet.z };
        Math::Vector3f look{ std::sinf(yaw) * std::cosf(pitch), std::sinf(pitch), std::cosf(yaw) * std::cosf(pitch) };
        look.Normalize();
        Math::Vector3f right = look.Cross(Math::Vector3f{ 0.0f, 1.0f, 0.0f });
        if (right.MagnitudeSqrd() < 1.0e-6f)
            right = Math::Vector3f::X_AXIS;
        right.Normalize();

        constexpr float kBoom    = 2.4f;
        constexpr float kMinBoom = 0.28f;
        Math::Vector3f boom = look * -kBoom + right * 0.42f + Math::Vector3f{ 0.0f, 0.28f, 0.0f };
        float boomLen = boom.Magnitude();
        if (boomLen < 1.0e-3f)
            boomLen = kBoom;
        const Math::Vector3f boomDir = boom * (1.0f / boomLen);

        if (raycast)
        {
            const Math::Ray3f ray{ target, boomDir };
            const Collision::RayHit3D hit = raycast(rayUser, ray, boomLen);
            if (hit.hit && hit.t < boomLen)
                boomLen = Math::Max(kMinBoom, hit.t - 0.12f);
        }
        boomLen = Math::Max(kMinBoom, boomLen);

        Math::Vector3f eye = target + boomDir * boomLen;
        if (shake > 0.0f)
            eye += right * (std::sinf(shakeTime * 95.0f) * shake) + Math::Vector3f{ 0.0f, std::sinf(shakeTime * 120.0f + 1.0f) * shake, 0.0f };
        if (wetClamp)
            eye.y = Math::Max(eye.y, waterLevel + 0.45f);

        ShoulderView view;
        view.eye = eye;
        view.aim = target + look * 16.0f;
        return view;
    }

    // Flat stroke. The eye sits behind the hips and above the back, looking forward over the head.
    inline ShoulderView solveSwimCamera(const Math::Vector3f& hips, float yaw,
                                        Collision::RayHit3D (*raycast)(void* user, const Math::Ray3f& ray, float maxDistance), void* rayUser)
    {
        const Math::Vector3f forward{ std::sinf(yaw), 0.0f, std::cosf(yaw) };
        const Math::Vector3f up{ 0.0f, 1.0f, 0.0f };
        Math::Vector3f       eye = hips - forward * 0.90f + up * 0.55f;
        const Math::Vector3f aim = hips + forward * 1.20f + up * 0.25f;

        constexpr float kMinBoom = 0.28f;
        if (raycast)
        {
            Math::Vector3f delta = eye - hips;
            const float    len   = delta.Magnitude();
            if (len > 1.0e-3f)
            {
                const Math::Vector3f dir = delta * (1.0f / len);
                const Collision::RayHit3D hit = raycast(rayUser, Math::Ray3f{ hips, dir }, len);
                if (hit.hit && hit.t < len)
                    eye = hips + dir * Math::Max(kMinBoom, hit.t - 0.12f);
            }
        }

        ShoulderView view;
        view.eye = eye;
        view.aim = aim;
        return view;
    }

    // Positive pitch about local X tips the head toward look. pose is 0 upright, 1 flat.
    inline Math::Quaternion swimBodyRotation(const Math::Quaternion& upright, float pose)
    {
        constexpr float kSwimPitch = 1.3962634f;
        if (!(pose > 1.0e-4f))
            return upright;
        float p = pose;
        if (p > 1.0f)
            p = 1.0f;
        if (p < 0.0f)
            p = 0.0f;
        return upright * Math::Quaternion::FromAxisAngle(Math::Vector3f::X_AXIS, p * kSwimPitch);
    }

} // namespace Dark
