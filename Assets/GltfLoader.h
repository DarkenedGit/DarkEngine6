#pragma once

#include "Animation/AnimationClip.h"
#include "Animation/Skeleton.h"
#include "Assets/Material.h"
#include "Math/Matrix4f.h"
#include "Assets/MeshData.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Dark
{

    struct GltfImageBlob
    {
        std::filesystem::path file;
        std::vector<uint8_t>  bytes;
        int                   imageIndex = -1;
    };

    struct GltfCpuPrimitive
    {
        MeshData          mesh;
        Math::Matrix4f    localToRoot = Math::Matrix4f();
        float             baseColor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        float             metallic     = 0.0f;
        float             roughness    = 1.0f;
        bool              translucent  = false;
        bool              doubleSided  = false;
        bool              skinned      = false;
        MaterialAlphaMode alphaMode    = MaterialAlphaMode::Opaque;
        GltfImageBlob     albedo;
        GltfImageBlob     normal;
        GltfImageBlob     metallicRoughness;
        GltfImageBlob     occlusion;
        GltfImageBlob     emissive;
        float             normalScale  = 1.0f;
        float             ao           = 1.0f; // occlusionTexture.strength
        float             alphaCutoff    = 0.5f;
        float             emissiveColor[3]{ 1.0f, 1.0f, 1.0f };
        float             emissiveScalar = 0.0f; // 1 if any emissiveFactor > 0 or emissive tex present
        int               albedoImageIndex = -1;
        int               mrImageIndex     = -1;
        int               occImageIndex    = -1;
        int               normalImageIndex = -1;
        int               emisImageIndex   = -1;
        int               materialIndex    = -1;
        std::string       materialName;
        std::string       meshName;
    };

    struct GltfCpuModel
    {
        std::vector<GltfCpuPrimitive> primitives;
        std::string                   generator;
        Skeleton                      skeleton;
        std::vector<AnimationClip>    clips;
    };

    // CPU-only parse (no GPU). Used by AssetManager::loadModel and unit tests.
    bool parseGltfFile(const std::filesystem::path& path, GltfCpuModel& out);

} // namespace Dark
