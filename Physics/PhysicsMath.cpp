#include "Physics/PhysicsMath.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"

#include <box3d/box3d.h>

#include <cmath>

namespace Dark::Physics
{
    namespace
    {
        b3Vec3 toB3(const Math::Vector3f& v)
        {
            return b3Vec3{v.x, v.y, v.z};
        }

        Math::Vector3f fromB3(const b3Vec3& v)
        {
            return Math::Vector3f(v.x, v.y, v.z);
        }

        b3Quat toB3(const Math::Quaternion& q)
        {
            return b3Quat{{q.x, q.y, q.z}, q.w};
        }

        Math::Quaternion fromB3(const b3Quat& q)
        {
            return Math::Quaternion(q.s, q.v.x, q.v.y, q.v.z);
        }

        void warnNegativeScale(const char* geom, const Math::Vector3f& scale)
        {
            if (scale.x < 0.0f || scale.y < 0.0f || scale.z < 0.0f)
            {
                DE_LOG_WARN(LogCategory::Collision, "Physics: negative scale on {}; using absolute radius", geom);
            }
        }
    } // namespace

    Math::Vector3f rotate(const Math::Quaternion& q, const Math::Vector3f& v)
    {
        const b3Vec3 r = b3RotateVector(toB3(q), toB3(v));
        return fromB3(r);
    }

    Math::Quaternion roundTripQuat(const Math::Quaternion& q)
    {
        return fromB3(toB3(q));
    }

    Math::Vector3f bakeBoxHalfExtents(const Math::Vector3f& halfExtents, const Math::Vector3f& scale)
    {
        return Math::Vector3f(halfExtents.x * std::fabs(scale.x), halfExtents.y * std::fabs(scale.y), halfExtents.z * std::fabs(scale.z));
    }

    float bakeSphereRadius(float radius, const Math::Vector3f& scale)
    {
        warnNegativeScale("sphere", scale);
        const float s = Math::Max(Math::Max(std::fabs(scale.x), std::fabs(scale.y)), std::fabs(scale.z));
        return std::fabs(radius) * s;
    }

    float bakeCapsuleRadius(float radius, const Math::Vector3f& scale)
    {
        warnNegativeScale("capsule", scale);
        const float s = Math::Max(std::fabs(scale.x), std::fabs(scale.z));
        return std::fabs(radius) * s;
    }
} // namespace Dark::Physics
