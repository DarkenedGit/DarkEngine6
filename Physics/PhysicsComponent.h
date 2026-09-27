#pragma once

#include "Physics/PhysicsBodyComponent.h"

#include <cstdint>
#include <string>

namespace Dark
{
    enum class PhysicsShapeKind : uint8_t
    {
        Box = 0,
        Sphere,
        Capsule,
        Model,
    };

    // Authoring settings for one rigid body. The live Box3D handle lives on PhysicsBodyComponent.
    struct PhysicsComponent
    {
        static constexpr const char* kTypeName = "Physics";

        bool             enabled        = true;
        PhysicsBodyMode  mode           = PhysicsBodyMode::Static;
        PhysicsShapeKind shape          = PhysicsShapeKind::Box;
        float            density        = 1.0f;  // mass = density * shape volume
        float            friction       = 0.6f;
        float            restitution    = 0.0f;
        float            linearDamping  = 0.0f;
        float            angularDamping = 0.05f;
        float            gravityScale   = 1.0f;
        bool             sensor         = false;
        bool             fixedRotation  = false;
        std::string      surface; // catalog id; empty uses the numbers above as-is
    };
} // namespace Dark
