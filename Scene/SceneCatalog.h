#pragma once

#include "Scene/SceneTypes.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    // Lightweight scene listing for the main-menu picker. Does not load objects.
    struct SceneFileInfo
    {
        std::string           fileName;    // "level2d.json"
        std::string           displayName; // JSON "name", else stem
        std::filesystem::path path;
        SceneMode             mode    = SceneMode::Scene3D;
        int                   version = 1;
    };

    // First existing <content-root>/scenes among contentRootCandidates(); empty if none.
    std::filesystem::path findScenesDirectory();

    // Read name/mode/version only. Returns false + errorOut on missing file or invalid JSON.
    bool peekSceneFile(const std::filesystem::path& path, SceneFileInfo& out, std::string* errorOut = nullptr);

    // JSON files in scenesDir whose mode matches. Sorted by displayName then fileName.
    std::vector<SceneFileInfo> listSceneFiles(SceneMode mode, const std::filesystem::path& scenesDir);

    // listSceneFiles(mode, findScenesDirectory()). Empty if no scenes folder.
    std::vector<SceneFileInfo> listSceneFiles(SceneMode mode);

    // First entry whose fileName matches preferredName (e.g. "level2d.json"), else nullptr.
    const SceneFileInfo* findSceneByFileName(const std::vector<SceneFileInfo>& scenes, std::string_view preferredName);

} // namespace Dark
