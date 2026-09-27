#pragma once

#include "Assets/AssetHandle.h"
#include "Math/Vector3f.h"
#include "Physics/PhysicsIds.h"

#include <cstdint>

namespace Dark
{
    enum class PhysicsBodyMode : uint8_t
    {
        None = 0,
        Static,
        Kinematic,
        Dynamic,
        Mover,
    };

    struct PhysicsBodyComponent
    {
        static constexpr const char* kTypeName = "PhysicsBody";

        PhysicsBodyMode        mode       = PhysicsBodyMode::Static;
        Physics::PhysicsBodyId body       = Physics::kNullPhysicsBody;
        AssetID                shapeAsset = NULL_ASSET;
        uint32_t               surfaceId  = 0;
        bool                   sensor     = false;
        bool                   valid      = false;
        Math::Vector3f         bakedScale{1.0f, 1.0f, 1.0f};
    };
} // namespace Dark
