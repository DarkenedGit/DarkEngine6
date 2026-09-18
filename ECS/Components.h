#pragma once

#include <string>
#include <array>
#include <cstdint>

#include "Assets/AssetHandle.h"
#include "ECS/Entity.h"
#include "Math/MathDefines.h"
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

    // Host GPU mesh for v1 cubes/crosses/spheres. meshAssetID stays 0 (no ensureMesh).
    enum class PrimitiveMesh : uint8_t
    {
        None = 0,
        Cube,
        Sphere,
        Cross,
    };

    // ─── Mesh renderer reference ─────────────────────────────────────────────────

    struct MeshComponent
    {
        static constexpr const char* kTypeName = "Mesh";

        // AssetManager ids (0 = none / use procedural or unbound resources).
        AssetID       meshAssetID = NULL_ASSET; // reserved; v1 always 0
        AssetID       matAssetID  = NULL_ASSET;
        PrimitiveMesh primitive   = PrimitiveMesh::None;
        bool          castShadow  = true;
        float         emissive    = 0.0f; // 0..1 → G-buffer RT0.a
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

        Math::Vector3f color     = { 1.0f, 0.96f, 0.88f };
        float          intensity = Math::Pi; // Fd/π look-preserving; gather is color*intensity
        bool           enabled   = true;
    };

    struct AmbientLightComponent
    {
        static constexpr const char* kTypeName = "AmbientLight";

        Math::Vector3f color     = { 0.22f, 0.22f, 0.22f };
        float          intensity = 1.0f;
        bool           enabled   = true;
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
        float          intensity    = 1885.0f; // candela; 600*π after Fd/π. JSON 600 stays.
        float          range        = 8.0f;
        float          innerConeDeg = 12.0f;
        float          outerConeDeg = 25.0f;
        float          sourceRadius = 0.05f;
        bool           enabled      = true;
        bool           castShadow   = false; // reserved, ignored
        Entity         emissiveMesh{};
    };

} // namespace Dark
