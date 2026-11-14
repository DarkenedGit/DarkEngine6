#pragma once

#include "Terrain/FoliageCover.h"
#include "Terrain/FoliageFile.h"
#include "Terrain/HeightMap.h"
#include "Terrain/SplatMap.h"
#include "Terrain/TrailPath.h"

#include <cstdint>
#include <vector>

namespace Dark::Terrain
{
    // Still water. entireMap is the level lake (every XZ). Otherwise the rectangle is one placed body.
    // Trees, grass, and flowers whose bed is below surfaceY are not planted. Rocks are.
    struct FoliageWaterVolume
    {
        float minX      = 0.0f;
        float maxX      = 0.0f;
        float minZ      = 0.0f;
        float maxZ      = 0.0f;
        float surfaceY  = 0.0f;
        bool  entireMap = false;
    };

    // Pins the large forest/meadow mask inside a world-space disk.
    struct FoliageLargeMaskDisk
    {
        float x      = 0.0f;
        float z      = 0.0f;
        float radius = 0.0f;
        float mask   = 0.0f;
    };

    struct FoliageSpawnIn
    {
        const HeightMap* height = nullptr;
        const SplatMap*  splat  = nullptr;
        FoliageDensity   density{};
        uint32_t         tilesX    = 0;
        uint32_t         tilesZ    = 0;
        uint32_t         tileCells = 0;
        float            cellSize  = 0.0f;
        Math::Vector3f   origin{ 0.0f, 0.0f, 0.0f };
        float            seaLevel = 0.0f;
        std::vector<FoliageWaterVolume> water;
        bool (*onProgress)(float t, const char* phase, void* user) = nullptr;
        void* user = nullptr;
        float treeExtentX = 0.0f;
        float treeExtentZ = 0.0f;
        std::vector<TrailPath> trails;
        // radius <= 0 keeps the hashed large mask. A later disk overrides an earlier one.
        FoliageLargeMaskDisk largeMaskDisks[2]{};
    };

    struct FoliageSpawnOut
    {
        FoliageSpawnOut()  = default;
        ~FoliageSpawnOut() = default;

        FoliageSpawnOut(const FoliageSpawnOut&)            = delete;
        FoliageSpawnOut& operator=(const FoliageSpawnOut&) = delete;
        FoliageSpawnOut(FoliageSpawnOut&&) noexcept            = default;
        FoliageSpawnOut& operator=(FoliageSpawnOut&&) noexcept = default;

        std::vector<FoliageRecord> records;
        FoliageCover               cover;
        uint64_t                   accepted = 0;
        uint32_t                   kept     = 0;
        bool                       capped   = false;
    };

    bool spawnFoliage(const FoliageSpawnIn& in, FoliageSpawnOut& out);
    // Repaints cover from stored paths and the current trees. Does not plant or thin.
    bool restampFoliageCover(const FoliageSpawnIn& in, const std::vector<FoliageRecord>& records, FoliageCover& cover);

} // namespace Dark::Terrain
