#pragma once

#include "Math/Vector3f.h"

namespace Dark
{

    struct HittableComponent
    {
        static constexpr const char* kTypeName = "Hittable";
        Math::Vector3f               halfExtents{ 1.0f, 1.0f, 1.0f };
    };

} // namespace Dark
