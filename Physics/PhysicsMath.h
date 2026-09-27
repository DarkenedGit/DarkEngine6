#pragma once

#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

namespace Dark::Physics
{
    // Rotate `v` by `q` through Box3D (`b3RotateVector`). Engine types only on this header.
    Math::Vector3f rotate(const Math::Quaternion& q, const Math::Vector3f& v);

    // Copy `q` into a Box3D quaternion and back. Double-cover: q and -q both round-trip as themselves.
    Math::Quaternion roundTripQuat(const Math::Quaternion& q);

    // Bake Transform.scale into primitive sizes. Box3D bodies do not carry non-uniform scale.
    Math::Vector3f bakeBoxHalfExtents(const Math::Vector3f& halfExtents, const Math::Vector3f& scale);
    float bakeSphereRadius(float radius, const Math::Vector3f& scale);
    // Y-up capsule: radius scales by max(|sx|, |sz|).
    float bakeCapsuleRadius(float radius, const Math::Vector3f& scale);
} // namespace Dark::Physics
