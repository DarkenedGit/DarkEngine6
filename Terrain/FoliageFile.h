#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Dark::Terrain
{
    constexpr uint32_t kMaxFoliageInstances    = 12582912u;
    constexpr uint32_t kMaxFoliagePerFile      = 1048575u; // (32 MiB - 32) / 32
    constexpr uint32_t kMaxFoliageDraw         = 65536u;
    constexpr uint32_t kFoliageMagic           = 0x4C464544u; // bytes 'D','E','F','L' little-endian
    constexpr uint32_t kFoliageVersion         = 1u;
    constexpr uint64_t kMaxFoliageSidecarBytes = 32ull * 1024ull * 1024ull;

    enum class FoliageKind : uint8_t
    {
        Tree = 0,
        Flower,
        Rock,
        Count
    };

    struct FoliageDensity
    {
        float       dirtTreesPerM2     = 0.002f;
        float       dirtFlowersPerM2   = 0.006f;
        float       grassTreesPerM2    = 0.003f;
        float       grassFlowersPerM2  = 0.008f;
        float       rockPerM2          = 0.004f;
        uint32_t    seed               = 1337u;
        std::string treeModel;
        std::string flowerModel;
        std::string rockModel;
    };

    struct FoliageRecord
    {
        float   x       = 0.0f;
        float   y       = 0.0f;
        float   z       = 0.0f;
        float   yaw     = 0.0f;
        float   scale   = 1.0f;
        float   pitch   = 0.0f;
        float   tiltYaw = 0.0f;
        uint8_t kind    = 0;
        uint8_t pad[3]{};
    };

    static_assert(sizeof(FoliageRecord) == 32, "foliage record");
    static_assert(offsetof(FoliageRecord, kind) == 28, "foliage kind");

    bool saveFoliageTile(const std::filesystem::path& path, int tileX, int tileZ, const FoliageRecord* records, uint32_t count);
    bool loadFoliageTile(const std::filesystem::path& path, int expectTileX, int expectTileZ, std::vector<FoliageRecord>& out);

} // namespace Dark::Terrain
