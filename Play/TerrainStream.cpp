#include "Play/TerrainStream.h"

#include "ECS/Components.h"
#include "Render/Renderer.h"
#include "Terrain/TerrainGrid.h"
#include "Terrain/TerrainMaterial.h"
#include "Water/Water.h"

namespace Dark
{

    void updatePinnedTerrainStream(Terrain::TerrainGrid& terrain, const Math::Vector3f& cameraPos, Renderer& renderer, const TerrainMaterial& material, bool dropPin, const TransformComponent* anchor)
    {
        if (dropPin)
            terrain.clearPin();
        else if (anchor)
            terrain.pinWorldXZ(anchor->position.x, anchor->position.z, 3);
        terrain.updateStreaming(cameraPos, &renderer, &material);
    }

    bool flushDirtyWater(WaterWorld& water, Renderer& renderer)
    {
        if (!water.needsRebuild())
            return true;
        water.rebuildDirtyCpuMeshes();
        return water.uploadDirty(renderer);
    }

} // namespace Dark
