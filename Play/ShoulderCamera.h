#pragma once

#include "Collision/HitResult.h"
#include "Math/MathHelper.h"
#include "Math/Ray3f.h"
#include "Math/Vector3f.h"
#include "Terrain/TerrainGrid.h"

#include <cmath>

namespace Dark
{

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

    // Third-person boom. shake and wetClamp are sandbox-owned; editor play passes zeros and wetClamp false.
    // A null raycast skips the terrain shorten.
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

} // namespace Dark
