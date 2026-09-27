#pragma once

#include "Physics/PhysicsComponent.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Dark::Physics
{
    // human.gltf -> models/human.physics.json when the source sits under a content/ directory.
    // Otherwise just the filename (human.physics.json).
    std::filesystem::path physicsSidecarRelative(const std::filesystem::path& modelSource);

    // Absolute sidecar path. Prefers authoringContentRoot() so writes survive a content copy.
    std::filesystem::path physicsSidecarAuthoringPath(const std::filesystem::path& modelSource);

    // Sibling of the model file (the copy the asset was loaded from).
    std::filesystem::path physicsSidecarBesideModel(const std::filesystem::path& modelSource);

    // nlohmann::json::parse(..., false). On failure `out` is unchanged.
    bool parsePhysicsSettings(std::string_view jsonText, PhysicsComponent& out);
    std::string writePhysicsSettings(const PhysicsComponent& settings);

    bool loadPhysicsSettingsFile(const std::filesystem::path& path, PhysicsComponent& out);
    bool savePhysicsSettingsFile(const std::filesystem::path& path, const PhysicsComponent& settings);

    // Authoring sidecar first, then the file beside the model. False if neither loads.
    bool loadPhysicsSettingsForModel(const std::filesystem::path& modelSource, PhysicsComponent& out);
} // namespace Dark::Physics
