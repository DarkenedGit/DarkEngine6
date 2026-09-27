#pragma once

#include <cstdint>

namespace Dark::Physics
{
    // Opaque Box3D handles. Layout matches b3Store*/b3Load* in Physics/*.cpp only.
    using PhysicsWorldId = uint32_t;
    using PhysicsBodyId  = uint64_t;
    using PhysicsShapeId = uint64_t;

    inline constexpr PhysicsWorldId kNullPhysicsWorld = 0;
    inline constexpr PhysicsBodyId  kNullPhysicsBody  = 0;
    inline constexpr PhysicsShapeId kNullPhysicsShape = 0;
} // namespace Dark::Physics
