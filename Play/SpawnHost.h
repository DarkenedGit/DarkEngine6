#pragma once

#include "Content/ContentDb.h"

namespace Dark
{

    // What an app has. makeSpawnHost copies it onto the behavior host.
    struct SpawnServices
    {
        World*                        world    = nullptr;
        AssetManager*                 assets   = nullptr;
        AssetPinTable*                pins     = nullptr;
        Renderer*                     renderer = nullptr;
        Audio::AudioSystem*           audio    = nullptr;
        const Terrain::TerrainGround* ground   = nullptr;
        AiSystem*                     ai       = nullptr;
        bool (*ensureMesh)(void* user, Entity entity) = nullptr;
        bool (*ensurePrimitive)(void* user, Entity entity, const char* primitive) = nullptr;
        bool (*ensureMarker)(void* user, Entity entity) = nullptr;
        bool (*bindParticleMaterial)(void* user, Entity entity, bool ribbon) = nullptr;
        WeaponHitFn onWeaponHit    = nullptr;
        void*       weaponHitUser  = nullptr;
        bool        modelAnimatedOnly = false;
        void*       user           = nullptr;
        CrowModels* crows          = nullptr;
        const Terrain::TerrainGrid* crowTerrain = nullptr;
        bool                        crowHomeSet  = false;
        float                       crowHomeX    = 0.0f;
        float                       crowHomeZ    = 0.0f;
        uint32_t                    crowSeed     = 1;
        int                         crowSlot     = 0;
    };

    ArchetypeSpawnHost makeSpawnHost(const SpawnServices& services);

} // namespace Dark
