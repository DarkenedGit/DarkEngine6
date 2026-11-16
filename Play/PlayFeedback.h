#pragma once

#include "ECS/Entity.h"
#include "Particles/BloodSplatPool.h"
#include "Particles/ParticleEmitter.h"

namespace Dark
{

    class World;
    class Camera3D;
    struct ParticleEmitterDesc;
    struct PlayHost;

    namespace Terrain
    {
        class HeightMap;
    }

    // Owned by PlaySession. World, camera, muzzle, and height are filled by the app
    // before the play tick. The muzzle timer, blood burst, and splat pool live here.
    struct PlayFeedback
    {
        World*    world  = nullptr;
        Camera3D* camera = nullptr;
        Entity*   muzzle = nullptr;
        float     muzzleTimer = 0.0f;
        ParticleEmitter blood;
        BloodSplatPool  splats;
        const Terrain::HeightMap* height = nullptr;
        void (*afterRespawn)(void* user) = nullptr;
        void* afterUser = nullptr;
    };

    // Installs the four shared callbacks and points host.muzzle at *feedback.muzzle.
    // Callbacks receive the PlayFeedback pointer, not PlayHost::user.
    void installPlayFeedback(PlayHost& host, PlayFeedback* feedback);

    const ParticleEmitterDesc& playBloodBurstDesc();

    void ensurePlayMuzzle(World& world, Entity& muzzle, float& timer);
    void destroyPlayMuzzle(World& world, Entity& muzzle, float& timer);
    void tickPlayMuzzle(PlayFeedback& feedback, float dt);

} // namespace Dark
