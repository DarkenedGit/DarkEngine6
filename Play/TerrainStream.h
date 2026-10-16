#pragma once

#include "Math/Vector3f.h"

namespace Dark
{

    class Renderer;
    class TerrainMaterial;
    class WaterWorld;
    struct TransformComponent;

    namespace Terrain
    {
        class TerrainGrid;
    }

    // Pins the body tile when an anchor is present, otherwise clears the pin, then streams.
    // A dropped pin with a null anchor leaves the previous pin alone, matching a live body that has no transform yet.
    void updatePinnedTerrainStream(Terrain::TerrainGrid& terrain, const Math::Vector3f& cameraPos, Renderer& renderer, const TerrainMaterial& material, bool dropPin, const TransformComponent* anchor);

    // Rebuilds dirty water chunks and uploads them. Does not call updateLod and does not log.
    // True when nothing is dirty or the upload succeeds.
    bool flushDirtyWater(WaterWorld& water, Renderer& renderer);

} // namespace Dark
