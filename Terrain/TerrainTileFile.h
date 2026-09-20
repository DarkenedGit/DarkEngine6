#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace Dark::Terrain
{

constexpr int      kTileCells     = 512;
constexpr int      kTileSamples   = kTileCells + 1; // 513
constexpr uint32_t kMaxWorldTiles = 8;

inline std::string tileHeightFileName(int tileX, int tileZ)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "t_%02d_%02d.height.bin", tileX, tileZ);
    return buf;
}

inline std::string tileSplatFileName(int tileX, int tileZ)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "t_%02d_%02d.splat.png", tileX, tileZ);
    return buf;
}

inline std::filesystem::path tileHeightPath(const std::filesystem::path& tileDir, int tileX, int tileZ)
{
    return tileDir / tileHeightFileName(tileX, tileZ);
}

inline std::filesystem::path tileSplatPath(const std::filesystem::path& tileDir, int tileX, int tileZ)
{
    return tileDir / tileSplatFileName(tileX, tileZ);
}

} // namespace Dark::Terrain
