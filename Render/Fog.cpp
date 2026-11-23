#include "Render/Fog.h"
#include "Render/DeferredLightingPipeline.h"
#include "Render/EnvironmentProvider.h"
#include "Terrain/HeightMap.h"

namespace Dark
{

    FogGpu makeFogGpu(const IEnvironmentProvider* env, float waterLevel, bool lighting)
    {
        FogGpu fog;
        fog.heightFogHeight  = waterLevel;
        fog.waterLevel       = waterLevel;
        fog.heightFogFalloff = 0.06f;
        fog.volumetricHeight = 14.0f;
        if (!env || !lighting)
        {
            fog.fogDensity           = 0.0f;
            fog.heightFogDensity     = 0.0f;
            fog.volumetricFogDensity = 0.0f;
            fog.fogColor[0]          = 0.0f;
            fog.fogColor[1]          = 0.0f;
            fog.fogColor[2]          = 0.0f;
            return fog;
        }

        const EnvironmentFog f   = env->fog();
        fog.fogColor[0]          = f.color.x;
        fog.fogColor[1]          = f.color.y;
        fog.fogColor[2]          = f.color.z;
        fog.fogDensity           = f.density;
        fog.heightFogDensity     = f.heightDensity;
        fog.heightFogFalloff     = f.heightFalloff;
        fog.volumetricFogDensity = f.volumetricDensity;
        fog.volumetricHeight     = f.volumetricHeight;
        return fog;
    }

    void fillFogHeightMap(FogGpu& fog, const Terrain::HeightMap* heightMap)
    {
        if (!heightMap || !heightMap->valid())
        {
            // Dummy 1x1 height SRV stays bound; FogSampleTerrainY skips when cellSize is 0.
            fog.heightCellSize = 0.0f;
            return;
        }
        const float cellsX     = static_cast<float>(heightMap->width() > 0 ? heightMap->width() - 1 : 0);
        const float cellsZ     = static_cast<float>(heightMap->height() > 0 ? heightMap->height() - 1 : 0);
        fog.heightOriginX      = heightMap->origin().x;
        fog.heightOriginZ      = heightMap->origin().z;
        fog.heightCellSize     = heightMap->cellSize();
        fog.heightWorldSizeX   = heightMap->cellSize() * cellsX;
        fog.heightWorldSizeZ   = heightMap->cellSize() * cellsZ;
    }

    void applyFogToLighting(LightingConstants& lc, const FogGpu& fog)
    {
        lc.fogDensity           = fog.fogDensity;
        lc.heightFogDensity     = fog.heightFogDensity;
        lc.fogColor[0]          = fog.fogColor[0];
        lc.fogColor[1]          = fog.fogColor[1];
        lc.fogColor[2]          = fog.fogColor[2];
        lc.heightFogFalloff     = fog.heightFogFalloff;
        lc.heightFogHeight      = fog.heightFogHeight;
        lc.volumetricFogDensity = fog.volumetricFogDensity;
        lc.waterLevel           = fog.waterLevel;
        lc.volumetricHeight     = fog.volumetricHeight;
        lc.heightOriginX        = fog.heightOriginX;
        lc.heightOriginZ        = fog.heightOriginZ;
        lc.heightCellSize       = fog.heightCellSize;
        lc.heightWorldSizeX     = fog.heightWorldSizeX;
        lc.heightWorldSizeZ     = fog.heightWorldSizeZ;
    }

} // namespace Dark
