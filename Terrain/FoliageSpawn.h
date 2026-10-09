#pragma once

#include "Terrain/FoliageFile.h"
#include "Terrain/HeightMap.h"
#include "Terrain/SplatMap.h"

#include <cstdint>
#include <vector>

namespace Dark::Terrain
{
    // Still water. entireMap is the level lake (every XZ). Otherwise the rectangle is one placed body.
    // Trees and grass whose terrain height is below surfaceY are not planted. Flowers and rocks are.
    struct FoliageWaterVolume
    {
        float minX      = 0.0f;
        float maxX      = 0.0f;
        float minZ      = 0.0f;
        float maxZ      = 0.0f;
        float surfaceY  = 0.0f;
        bool  entireMap = false;
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
    };

    struct FoliageSpawnOut
    {
        std::vector<FoliageRecord> records;
        uint64_t                   accepted = 0;
        uint32_t                   kept     = 0;
        bool                       capped   = false;
    };

    bool spawnFoliage(const FoliageSpawnIn& in, FoliageSpawnOut& out);

} // namespace Dark::Terrain
