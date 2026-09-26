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

        // Broad hills the gully layers sit on. fadeTarget -1 is the basin, +1 the peaks.
        int   shapeOctaves     = 3;
        float shapeFrequency   = 3.0f;
        float shapeAmplitude   = 0.125f;
        float shapeGain        = 0.1f;
        float shapeLacunarity  = 2.0f;

        // Each gully octave is a finer stripe layer. Strength * scale is how far it moves the ground.
        int   gullyOctaves        = 5;
        float gullyStrength       = 0.22f;
        float gullyScale          = 0.15f;
        float gullyWeight         = 0.5f;
        float gullyGain           = 0.5f;
        float gullyLacunarity     = 2.0f;
        float gullyCellScale      = 0.7f;
        float gullyNormalization  = 0.5f;
        float gullyDetail         = 1.5f;
        float gullyDepthBias      = 0.65f;
        // Altitude (fadeTarget) where stripes begin, and where they reach full strength.
        float gullyAltitudeStart  = -0.05f;
        float gullyAltitudeFull   = 0.50f;
    };

    struct WorldGenDesc
    {
        uint32_t       tilesX      = 4;
        uint32_t       tilesZ      = 4;
        uint32_t       tileCells   = kTileCells;
        float          cellSize    = 1.0f;
        float          heightScale = 80.0f;
        Math::Vector3f origin      { -1024.0f, 0.0f, -1024.0f };
        ErosionParams  erosion     {};
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
