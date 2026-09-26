#pragma once

#include <filesystem>
#include <vector>

namespace Dark
{

    // Candidate content/ directories: <exe|cwd>/content plus two- and three-level parent walks
    // (Visual Studio: build/bin/Debug). Unique, weakly canonical when possible; need not exist.
    // The exe-adjacent folder is first. Builds copy the repo content/ there, so that copy is
    // replaced on every build.
    std::vector<std::filesystem::path> contentRootCandidates();
    std::vector<std::filesystem::path> contentRootCandidates(const std::filesystem::path& exeDir, const std::filesystem::path& cwd);

    // Content directory for scene and terrain saves. Prefers a content/ folder outside the
    // executable directory so authoring does not land in the build copy. Falls back to the
    // exe-adjacent content folder when that is the only one.
    std::filesystem::path authoringContentRoot();
    std::filesystem::path authoringContentRoot(const std::filesystem::path& exeDir, const std::filesystem::path& cwd);

} // namespace Dark
