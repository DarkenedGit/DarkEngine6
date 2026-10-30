#pragma once

#include "ECS/Entity.h"
#include "Terrain/GrassField.h"
#include "Terrain/LeafField.h"

#include <vector>

namespace Dark
{

    class AssetManager;
    class Camera3D;
    class Model;
    class World;

    namespace Audio
    {
        class AudioSystem;
    }

    namespace Terrain
    {
        struct FoliageRecord;
        class LeafRustleVoices;
        class TerrainGrid;
    }

    // One falling-leaf step shared by Sandbox and Editor. Ponds are optional.
    struct FallingLeafTick
    {
        Terrain::LeafField*                         field      = nullptr;
        Terrain::LeafRustleVoices*                  rustle     = nullptr;
        Terrain::TerrainGrid*                       terrain    = nullptr;
        const std::vector<Terrain::FoliageRecord>*  foliage    = nullptr;
        const Model*                                treeModel  = nullptr;
        Audio::AudioSystem*                         audio      = nullptr;
        AssetManager*                               assets     = nullptr;
        const Camera3D*                             camera     = nullptr;
        double*                                     simSeconds = nullptr;
        float                                       shedScale  = 1.0f;
        bool                                        lake       = false;
        float                                       lakeLevel  = 0.0f;
        const Terrain::LeafField::Pond*             ponds      = nullptr;
        int                                         pondCount  = 0;
    };

    void stepFallingLeaves(const FallingLeafTick& tick, float dt);

    // True when the body has a transform. That transform becomes the grass anchor and the shove sample.
    bool fillGrassPlayerSample(World& world, Entity body, float& x, float& z, Terrain::GrassPlayerSample& sample);

    // Camera ear. Hear skill scales distance only when asked and the body is alive.
    void setViewAudioListener(Audio::AudioSystem& audio, const Camera3D& camera, World* world, Entity body, bool applyHearSkill);

} // namespace Dark
