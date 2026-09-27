#pragma once

#include "Assets/MeshData.h"
#include "Math/Vector3f.h"

#include <cstdint>

namespace Dark::Physics
{
    inline constexpr uint32_t kMaxPhysicsDebugLines = 65536;

    // Returns false when the 64k line cap is hit (`out` unchanged).
    bool appendPhysicsDebugLine(LineMeshData& out, const Math::Vector3f& a, const Math::Vector3f& b);
} // namespace Dark::Physics
