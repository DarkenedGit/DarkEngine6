#pragma once

#include <filesystem>

namespace Dark
{

// Directory containing the running executable (not the process CWD).
// Empty path if the OS query fails.
std::filesystem::path executableDirectory();

// Absolute form of path relative to CWD (or absolute as-is). Empty if invalid.
std::filesystem::path absolutePath(const std::filesystem::path& path);

} // namespace Dark
