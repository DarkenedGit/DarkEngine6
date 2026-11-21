#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// On-disk cache for runtime DXC compiles in Debug and the Editor (plan D10, §3.15.1).
// The key hashes the main source, entry, profile, defines, compiler arguments and DXC version.
// Each entry also records every included file with its content hash; a changed include is a miss.
// Pure code (std::filesystem only) so the format is unit-tested natively.

namespace Dark
{

    struct ShaderCacheDependency
    {
        std::string path; // UTF-8, as the include handler opened it
        uint64_t    hash = 0;
    };

    struct ShaderCacheEntry
    {
        uint64_t                           key = 0;
        std::vector<ShaderCacheDependency> dependencies;
        std::vector<uint8_t>               bytecode;
    };

    uint64_t shaderCacheKey(std::string_view source, std::string_view sourcePath, std::string_view entry, std::string_view profile, std::string_view defines, std::string_view arguments,
                            std::string_view compilerVersion);

    // "<16 hex digits>.bin".
    std::string shaderCacheFileName(uint64_t key);

    std::vector<uint8_t> serializeShaderCacheEntry(const ShaderCacheEntry& entry);
    bool                 parseShaderCacheEntry(std::span<const uint8_t> bytes, ShaderCacheEntry& out);

    // Hash of a file's bytes, or false when it cannot be read.
    bool hashShaderCacheFile(const std::filesystem::path& path, uint64_t& outHash);

    // True when every recorded include still exists with the same content hash.
    bool shaderCacheDependenciesCurrent(const ShaderCacheEntry& entry);

    // Reads dir/<key>.bin. False on a miss, a stale key or a corrupt file.
    bool loadShaderCacheEntry(const std::filesystem::path& dir, uint64_t key, ShaderCacheEntry& out);

    // Writes dir/<key>.bin through a temporary file and a rename. Creates dir. False on I/O failure.
    bool storeShaderCacheEntry(const std::filesystem::path& dir, const ShaderCacheEntry& entry);

} // namespace Dark
