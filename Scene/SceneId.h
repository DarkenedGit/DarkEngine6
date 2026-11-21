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
    // root wins. With no matching root (an older save from another install) the folders above
    // "scenes/" decide: ".../content/scenes/x" is core, ".../mods/<folder>/scenes/x" is that package
    // (the current package rooted at <folder> when there is one), anything else is
    // kUnresolvedScenePrefix + the path. A bare file name is "scenes/<name>". Ids are idempotent. Empty gives "".
    std::string sceneContentId(const std::filesystem::path& scenePath, const std::vector<SceneIdRoot>& roots);
    // Roots are the enabled content packages plus contentRootCandidates() as core.
    std::string sceneContentId(const std::filesystem::path& scenePath);

    // Prefix of an identity that could not be tied to a content root or a mods/<package> folder
    // (e.g. a custom absolute path from another install). It is kept verbatim and matches only itself.
    inline constexpr std::string_view kUnresolvedScenePrefix = "unresolved:";

    // The level Sandbox boots by default. Saves from before scene ids (identity "") load only here.
    inline constexpr std::string_view kDefaultSceneId = "scenes/level.json";

    // Save gate: the file's scene identity (an id, an old absolute path, or "") against ours.
    bool sceneIdAccepts(const std::string& fileIdentity, const std::string& ourId, const std::vector<SceneIdRoot>& roots);
    bool sceneIdAccepts(const std::string& fileIdentity, const std::string& ourId);

    // Same scene when both normalize to the same id. Two empty paths are not the same scene.
    bool sameSceneId(const std::filesystem::path& a, const std::filesystem::path& b);

} // namespace Dark
