#include "Play/SpawnHost.h"

namespace Dark
{

    ArchetypeSpawnHost makeSpawnHost(const SpawnServices& services)
    {
        ArchetypeSpawnHost host{};
        host.world               = services.world;
        host.assets              = services.assets;
        host.pins                = services.pins;
        host.renderer            = services.renderer;
        host.audio               = services.audio;
        host.ground              = services.ground;
        host.ai                  = services.ai;
        host.ensureMesh          = services.ensureMesh;
        host.ensurePrimitive     = services.ensurePrimitive;
        host.ensureMarker        = services.ensureMarker;
        host.bindParticleMaterial = services.bindParticleMaterial;
        host.onWeaponHit         = services.onWeaponHit;
        host.weaponHitUser       = services.weaponHitUser;
        host.modelAnimatedOnly   = services.modelAnimatedOnly;
        host.user                = services.user;
        host.crows               = services.crows;
        host.crowTerrain         = services.crowTerrain;
        host.crowHomeSet         = services.crowHomeSet;
        host.crowHomeX           = services.crowHomeX;
        host.crowHomeZ           = services.crowHomeZ;
        host.crowSeed            = services.crowSeed;
        host.crowSlot            = services.crowSlot;
        return host;
    }

} // namespace Dark
