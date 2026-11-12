#pragma once

#include "ECS/Entity.h"

namespace Dark
{

    class World;
    class Camera3D;
    class ParticleEmitter;
    class BloodSplatPool;
    struct ParticleEmitterDesc;
    struct PlayHost;

    namespace Terrain
    {
        class HeightMap;
    }

    // App-owned pointers for the shared muzzle, blood, and respawn hooks.
    // The same struct is bound before the play tick and again from fillPlayHost.
    struct PlayFeedback
    {
        World*          world       = nullptr;
        Camera3D*       camera      = nullptr;
        Entity*         muzzle      = nullptr;
        float*          muzzleTimer = nullptr;
        ParticleEmitter* blood      = nullptr;
        BloodSplatPool* splats      = nullptr;
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
    void tickPlayMuzzle(const PlayFeedback& feedback, float dt);

} // namespace Dark
