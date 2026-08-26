#pragma once

#include "Scene/SceneTypes.h"

#include <filesystem>
#include <string>

namespace Dark
{

    // JSON scene I/O (versioned). No exceptions — returns false + fills errorOut on failure.
    bool saveSceneToJson(const std::filesystem::path& path, const SceneFileData& scene, std::string* errorOut = nullptr);

    bool loadSceneFromJson(const std::filesystem::path& path, SceneFileData& outScene, std::string* errorOut = nullptr);

    // Resolve a default path: <content-root>/scenes/<name> via contentRootCandidates().
    std::filesystem::path defaultScenePath(const std::filesystem::path& preferredName = "level.json");

} // namespace Dark
