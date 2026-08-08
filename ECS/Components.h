#pragma once

#include <string>
#include <array>
#include <cstdint>

#include "Assets/AssetHandle.h"
#include "Math/Quaternion.h"

namespace Dark
{
    // ─── Transform ───────────────────────────────────────────────────────────────

    struct TransformComponent 
    {
        Math::Vector3f   position{ 0, 0, 0};
        Math::Quaternion rotation{ 1, 0, 0, 0 };
        Math::Vector3f   scale{ 1, 1, 1 };
    };

    // ─── Tag / Name ───────────────────────────────────────────────────────────────

    struct TagComponent
    {
        std::string name = "Entity";
    };

    // ─── Mesh renderer reference ─────────────────────────────────────────────────

    struct MeshComponent
    {
        // AssetManager ids (0 = none / use procedural or unbound resources).
        AssetID  meshAssetID = NULL_ASSET;
        AssetID  matAssetID  = NULL_ASSET;
        bool     castShadow  = true;
    };

    // ─── Camera ──────────────────────────────────────────────────────────────────

    struct CameraComponent 
    {
        float fovDeg    = 60.0f;
        float nearZ     = 0.01f;
        float farZ      = 1000.0f;
        bool  primary   = false;
    };

    // ─── Directional light ───────────────────────────────────────────────────────

    struct DirectionalLightComponent 
    {
        Math::Vector3f color     = { 1, 1, 1 };
        float          intensity = 1.0f;
    };

} // namespace Dark
