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
    constexpr uint32_t kFoliageVersion         = 2u;
    constexpr uint32_t kMaxFoliageVariants     = 8u;
    constexpr uint64_t kMaxFoliageSidecarBytes = 32ull * 1024ull * 1024ull;

    enum class FoliageKind : uint8_t
    {
        Tree = 0,
        Flower,
        Rock,
        Grass,
        Count
    };

    // scaleMin/scaleMax of 0 use the kind default. worldScale skips the flower/tuft x10 in the draw.
    struct FoliageSpecies
    {
        std::string path;
        float       weight     = 1.0f;
        float       scaleMin   = 0.0f;
        float       scaleMax   = 0.0f;
        bool        worldScale = false;
    };

    inline std::vector<FoliageSpecies> defaultTreeSpecies()
    {
        return { FoliageSpecies{ "models/BirchTree/scene.gltf", 1.0f, 0.70f, 1.60f, false } };
    }

    inline std::vector<FoliageSpecies> defaultFlowerSpecies()
    {
        return {
            FoliageSpecies{ "models/Dandelion/dandelion_01_2k.gltf", 1.0f, 0.0f, 0.0f, false },
            FoliageSpecies{ "models/NaturePack/glTF/Fern_1.gltf", 1.0f, 0.0f, 0.0f, false },
        };
    }

    inline std::vector<FoliageSpecies> defaultRockSpecies()
    {
        return { FoliageSpecies{ "models/RockMoss/rock_moss_set_01_2k.gltf", 1.0f, 0.0f, 0.0f, false } };
    }

    inline std::vector<FoliageSpecies> defaultTuftSpecies()
    {
        return {
            FoliageSpecies{ "models/Grass/grass_medium_01_2k.gltf", 1.0f, 0.0f, 0.0f, false },
            FoliageSpecies{ "models/NaturePack/glTF/Bush_Common.gltf", 1.0f, 0.0f, 0.0f, false },
        };
    }

    inline uint32_t packFoliageKind(uint8_t kind, uint8_t variant, uint16_t rgb565)
    {
        return static_cast<uint32_t>(kind) | (static_cast<uint32_t>(variant) << 8) | (static_cast<uint32_t>(rgb565) << 16);
    }

    inline uint32_t foliageKindIndex(uint32_t packed)
    {
        return packed & 0xFFu;
    }

    struct FoliageDensity
    {
        float       dirtTreesPerM2     = 0.002f;
        float       dirtFlowersPerM2   = 0.006f;
        float       grassTreesPerM2    = 0.003f;
        float       grassFlowersPerM2  = 0.008f;
        float       rockPerM2          = 0.004f;
        float       grassPerM2         = 0.0f; // grass layer only; 0 keeps old scenes unchanged
        float       treeMinNormalY     = 0.87f;
        float       flowerMinNormalY   = 0.82f;
        float       tuftMinNormalY     = 0.82f;
        float       rockMinNormalY     = 0.40f;
        float       treeMaxLeanDeg     = 7.0f;
        float       rockMaxPitchDeg    = 30.0f;
        float       treeSpacingM       = 3.0f;
        float       rockSpacingM       = 0.0f;
        float       patchStrength      = 0.0f;
        float       patchLargeM        = 80.0f;
        float       patchMediumM       = 22.0f;
        float       patchFineM         = 4.0f;
        uint32_t    seed               = 1337u;
        std::string treeModel   = "models/BirchTree/scene.gltf";
        std::string flowerModel = "models/Dandelion/dandelion_01_2k.gltf";
        std::string rockModel   = "models/RockMoss/rock_moss_set_01_2k.gltf";
        std::string grassModel  = "models/Grass/grass_medium_01_2k.gltf";
        std::vector<FoliageSpecies> trees   = defaultTreeSpecies();
        std::vector<FoliageSpecies> flowers = defaultFlowerSpecies();
        std::vector<FoliageSpecies> rocks   = defaultRockSpecies();
        std::vector<FoliageSpecies> tufts   = defaultTuftSpecies();
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
