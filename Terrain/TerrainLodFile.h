#pragma once

#include "Terrain/GrassTypes.h"
#include "Terrain/TerrainLod.h"

#include <cstdint>
#include <string>

namespace Dark::Terrain
{

    // Grass band distances in metres. Loaded from content/terrain/lod.json.
    struct GrassLodDistances
    {
        float nominal0     = 16.0f;
        float nominal1     = 40.0f;
        float nominal2     = 80.0f;
        float nominal3     = 128.0f;
        float promote0     = 13.6f;
        float promote1     = 34.0f;
        float promote2     = 68.0f;
        float demote0      = 18.4f;
        float demote1      = 46.0f;
        float demote2      = 92.0f;
        float createRadius = 128.0f;
        float evictRadius  = 147.2f;
        float fadeStart    = 112.0f;
        float fadeEnd      = 128.0f;
    };

    // terrainMultiples[i] is how many chunk widths out LOD i steps to i+1.
    struct TerrainLodSettings
    {
        float             terrainMultiples[kMaxLodLevels]{ 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f, 256.0f };
        int               terrainCount = 5;
        GrassLodDistances grass{};
    };

    // Built-in copy of content/terrain/lod.json, used when that file is missing.
    TerrainLodSettings builtinTerrainLodSettings();

    // On failure `out` is unchanged.
    bool parseTerrainLodSettings(std::string_view jsonText, TerrainLodSettings& out);

    // Tile and blade room for one grass distance set. tiles[lod] is the most
    // 8 m tiles that band can hold at once. residentTiles is the linger disc.
    struct GrassRegionBudget
    {
        uint32_t tiles[kGrassLodCount]{};
        uint32_t slots[kGrassLodCount]{};
        uint32_t bladeCap      = 0;
        uint32_t residentTiles = 0;
    };

    // Finest-hysteresis lattice: player stepped across one tile, centers at 8*i+4.
    // LOD0 is d <= demote0, then each coarser band is (previous demote, next demote],
    // and LOD3 runs out to evictRadius.
    GrassRegionBudget measureGrassRegions(const GrassLodDistances& distances);

    // First call reads content/terrain/lod.json. Later calls return the same settings.
    const TerrainLodSettings& terrainLodSettings();
    const GrassRegionBudget&  grassRegionBudget();
    bool                      terrainLodLoadedFromFile();

    inline const GrassLodDistances& grassLod()
    {
        return terrainLodSettings().grass;
    }

    // distances[i] = chunkWidth * multiples[i]. chunkWidth is cell size times chunk cells.
    inline void fillChunkLodDistances(float chunkWidth, const TerrainLodSettings& settings, float* out, int& count)
    {
        count = settings.terrainCount;
        if (count < 1)
            count = 1;
        if (count > kMaxLodLevels)
            count = kMaxLodLevels;
        if (!out)
            return;
        for (int i = 0; i < count; ++i)
            out[i] = chunkWidth * settings.terrainMultiples[i];
    }

} // namespace Dark::Terrain
