#pragma once

#include "Scene/SceneTypes.h"

#include <filesystem>
#include <string>

namespace Dark
{

    // JSON scene I/O (versioned). No exceptions — returns false + fills errorOut on failure.
    bool saveSceneToJson(const std::filesystem::path& path, const SceneFileData& scene, std::string* errorOut = nullptr);

    bool loadSceneFromJson(const std::filesystem::path& path, SceneFileData& outScene, std::string* errorOut = nullptr);

    // Resolve a default path: <authoring-content>/scenes/<name>.
    // Skips the content copy next to the executable when a source content/ directory exists.
    std::filesystem::path defaultScenePath(const std::filesystem::path& preferredName = "level.json");

} // namespace Dark
