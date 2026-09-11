#pragma once

#include "Animation/AnimationClip.h"
#include "Animation/Skeleton.h"
#include "Math/Matrix4f.h"
#include "Render/MeshGen.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Dark
{

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
        std::filesystem::path albedoFile;
        std::vector<uint8_t>  albedoBytes; // embedded png/jpeg
        int               imageIndex = -1;
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
