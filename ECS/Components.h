#pragma once

#include <string>
#include <array>
#include <cstdint>

#include "Assets/AssetHandle.h"
#include "ECS/Entity.h"
#include "Math/Quaternion.h"

namespace Dark
{
    // ─── Transform ───────────────────────────────────────────────────────────────

    struct TransformComponent
    {
        static constexpr const char* kTypeName = "Transform";

        Math::Vector3f   position{ 0, 0, 0 };
        Math::Quaternion rotation{ 1, 0, 0, 0 };
        Math::Vector3f   scale{ 1, 1, 1 };
    };

    // ─── Tag / Name ───────────────────────────────────────────────────────────────

    struct TagComponent
    {
        static constexpr const char* kTypeName = "Tag";

        std::string name = "Entity";
    };

    // ─── Mesh renderer reference ─────────────────────────────────────────────────

    struct MeshComponent
    {
        static constexpr const char* kTypeName = "Mesh";

        // AssetManager ids (0 = none / use procedural or unbound resources).
        AssetID meshAssetID = NULL_ASSET;
        AssetID matAssetID  = NULL_ASSET;
        bool    castShadow  = true;
        float   emissive    = 0.0f; // 0..1 → G-buffer RT0.a
    };

    // Cached glTF / GLB from AssetManager::loadModel.
    struct ModelComponent
    {
        static constexpr const char* kTypeName = "Model";

        AssetID modelAssetID = NULL_ASSET;
        bool    castShadow   = true;
    };

    // ─── Camera ──────────────────────────────────────────────────────────────────

    struct CameraComponent
    {
        static constexpr const char* kTypeName = "Camera";

        float fovDeg  = 60.0f;
        float nearZ   = 0.01f;
        float farZ    = 1000.0f;
        bool  primary = false;
    };

    // ─── Directional light ───────────────────────────────────────────────────────

    struct DirectionalLightComponent
    {
        static constexpr const char* kTypeName = "DirectionalLight";

        Math::Vector3f color     = { 1, 1, 1 };
        float          intensity = 1.0f;
    };

    // ─── Local light (point / spot) ──────────────────────────────────────────────

    enum class LocalLightType : uint8_t
    {
        Point = 0,
        Spot  = 1,
    };

    struct LocalLightComponent
    {
        static constexpr const char* kTypeName = "LocalLight";

        LocalLightType type         = LocalLightType::Point;
        Math::Vector3f color        = { 1.0f, 1.0f, 1.0f };
        float          intensity    = 600.0f; // candela
        float          range        = 8.0f;
        float          innerConeDeg = 12.0f;
        float          outerConeDeg = 25.0f;
        float          sourceRadius = 0.05f;
        bool           enabled      = true;
        bool           castShadow   = false; // reserved, ignored
        Entity         emissiveMesh{};
    };

} // namespace Dark
