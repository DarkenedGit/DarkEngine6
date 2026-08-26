#pragma once

#include <filesystem>
#include <vector>

namespace Dark
{

    // Candidate content/ directories: <exe|cwd>/content plus two- and three-level parent walks
    // (Visual Studio: build/bin/Debug). Unique, weakly canonical when possible; need not exist.
    std::vector<std::filesystem::path> contentRootCandidates();
    std::vector<std::filesystem::path> contentRootCandidates(const std::filesystem::path& exeDir, const std::filesystem::path& cwd);

} // namespace Dark
