#include "AI/Sight.h"

#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Math/Ray3f.h"

#include <cmath>

namespace Dark::AI
{
    bool sees(const SightQuery& query)
    {
        if (!query.heightMap || !query.heightMap->valid())
        {
            DE_LOG_ERROR(LogCategory::AI, "HunterSight: height map is invalid");
            return false;
        }
        if (query.range <= 0.0f || query.coneDeg <= 0.0f)
            return false;

        const Math::Vector3f delta = query.target - query.eye;
        const float dist = delta.Magnitude();

        if (dist <= 1.0e-3f)
            return dist <= query.range;

        if (dist > query.range)
            return false;

        Math::Vector3f fwd = query.forward;
        fwd.y = 0.0f;

        if (fwd.MagnitudeSqrd() < 1.0e-8f)
            return false;

        fwd.Normalize();

        Math::Vector3f to = delta;
        to.y = 0.0f;

        if (to.MagnitudeSqrd() < 1.0e-8f)
            return dist <= query.range;

        to.Normalize();

        const float halfRad = query.coneDeg * 0.5f * (3.14159265f / 180.0f);
        const float minCos  = std::cos(halfRad);

        if (fwd.Dot(to) < minCos)
            return false;

        Math::Vector3f dir = delta;
        dir.Normalize();
        const Math::Ray3f ray{ query.eye, dir };
        const Collision::RayHit3D hit = query.heightMap->raycast(ray, dist);

        if (!hit.hit)
            return true;

        return hit.t >= dist - 1.0e-2f;
    }
} // namespace Dark::AI
