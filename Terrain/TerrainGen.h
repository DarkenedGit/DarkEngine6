#pragma once

#include "Terrain/HeightMap.h"
#include "Terrain/SplatMap.h"
#include "Terrain/TerrainTileFile.h"

#include <cstdint>

struct ID3D12CommandQueue;
struct ID3D12Device;

namespace Dark
{
    class TerrainErosionPipeline;
}

namespace Dark::Terrain
{

    constexpr int kMaxWorkingSize = 4097;

    // Duplicate of TU-private Hash21 in HeightMap.cpp. NOT rand().
    // Returns [0,1): (h & 0x00FFFFFF) / 16777216.0f — same as createFbm.
    float hash21(int x, int z, uint32_t seed);

    struct ErosionParams
    {
        uint32_t seed                = 1337u;
        int      fbmOctaves          = 3;
        float    fbmFrequency        = 3.0f;
        float    fbmAmplitude        = 0.25f;
        float    warpAmp             = 0.0f;
        int      thermalIterations   = 40;
        float    talusTan            = 0.7f;
        float    thermalRate         = 0.5f;
        int      hydraulicDroplets   = 0;  // CPU fallback only; 0 → auto (samples / 8)
        int      hydraulicMaxSteps   = 64; // CPU per-droplet travel cap, NOT world effect distance
        int      hydraulicIterations = 48; // GPU Mei pipe Jacobi steps; effect distance = this (samples)
        float    hydraulicInertia    = 0.05f;
        float    evaporate           = 0.02f;
        float    capacity            = 1.0f;
        float    erode               = 0.3f;
        float    deposit             = 0.3f;
        float    gravity             = 4.0f;
        float    seaLevelRaw         = 0.0f;
    };

    struct WorldGenDesc
    {
        uint32_t       tilesX      = 4;
        uint32_t       tilesZ      = 4;
        uint32_t       tileCells   = kTileCells;
        float          cellSize    = 1.0f;
        float          heightScale = 80.0f;
        Math::Vector3f origin{ -1024.0f, 0.0f, -1024.0f };
        ErosionParams  erosion{};
    };

    // Optional GPU bake. All three must be set and pipeline.isValid() to take the Mei path.
    struct WorldGenGpu
    {
        TerrainErosionPipeline* pipeline = nullptr;
        ID3D12Device*           device   = nullptr;
        ID3D12CommandQueue*     queue    = nullptr;
    };

    // Returns false on cap / cancel / alloc fail. outFull is Editor working size (tiles*tileCells + 1). No throw.
    // Prefers GPU Mei+thermal when gpu is valid; else CPU thermal Jacobi + sequential droplets.
    bool generateWorld(const WorldGenDesc& desc, HeightMap& outFull, SplatMap& outSplat, bool (*progress)(float t, const char* phase, void* user) = nullptr, void* user = nullptr,
                       const WorldGenGpu* gpu = nullptr);

    // GPU: padding = thermalIterations + hydraulicIterations, write interior.
    // CPU: thermal padded (bit-identical); droplets in-rect approximate.
    bool regenerateRect(HeightMap& map, const WorldGenDesc& desc, int x0, int z0, int x1, int z1, const WorldGenGpu* gpu = nullptr);

    bool applyThermalJacobi(HeightMap& map, const ErosionParams& params);
    bool applyHydraulicDroplets(HeightMap& map, const ErosionParams& params);

} // namespace Dark::Terrain
