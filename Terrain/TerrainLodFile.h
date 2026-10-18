#pragma once

#include "Terrain/TerrainLod.h"

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

    // First call reads content/terrain/lod.json. Later calls return the same settings.
    const TerrainLodSettings& terrainLodSettings();
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
