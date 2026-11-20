#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    // A content root the scene id is relative to. An empty package is core content and adds no prefix.
    struct SceneIdRoot
    {
        std::filesystem::path root;
        std::string           package;
    };

    // Content-relative scene id: lower-case, '/' separators, e.g. "scenes/level.json".
    // A file under a non-core package root is "<package>:scenes/x.json". The longest matching
    // root wins. With no matching root the id starts at the last "scenes" folder in the path,
    // else it is "scenes/<file name>". An id already in this form comes back unchanged, so an
    // absolute path from an older save normalizes to the same id. Empty path gives "".
    std::string sceneContentId(const std::filesystem::path& scenePath, const std::vector<SceneIdRoot>& roots);
    // Roots are the enabled content packages plus contentRootCandidates() as core.
    std::string sceneContentId(const std::filesystem::path& scenePath);

    // The level Sandbox boots by default. Saves from before scene ids (identity "") load only here.
    inline constexpr std::string_view kDefaultSceneId = "scenes/level.json";

    // Save gate: the file's scene identity (an id, an old absolute path, or "") against ours.
    bool sceneIdAccepts(const std::string& fileIdentity, const std::string& ourId, const std::vector<SceneIdRoot>& roots);
    bool sceneIdAccepts(const std::string& fileIdentity, const std::string& ourId);

    // Same scene when both normalize to the same id. Two empty paths are not the same scene.
    bool sameSceneId(const std::filesystem::path& a, const std::filesystem::path& b);

} // namespace Dark
