#pragma once

#include "ECS/Entity.h"
#include "Weapons/ThrowableFlight.h"

#include <string>

namespace Dark
{

    class AiSystem;
    class AssetManager;
    class AssetPinTable;
    class Renderer;
    class World;
    struct WeaponFireRequest;

    namespace Audio
    {
        class AudioSystem;
    }
    namespace Physics
    {
        class PhysicsWorld;
    }

    // Solids carry terrain callbacks, cubes, and spheres. The tick fills bodies.
    // Sandbox passes path cubes and no spheres. Editor passes play cubes and play spheres.
    struct ThrowableFlightHost
    {
        World*                  world    = nullptr;
        AssetPinTable*          pins     = nullptr;
        AssetManager*           assets   = nullptr;
        Renderer*               renderer = nullptr;
        Physics::PhysicsWorld*  physics  = nullptr;
        Audio::AudioSystem*     audio    = nullptr;
        AiSystem*               ai       = nullptr;
        ThrowableWorld          solids{};
    };

    void tickThrowableFlightHost(ThrowableFlights& flights, float dt, const ThrowableFlightHost& host);

    // Looks up the belt item and starts the sweep. False leaves the belt and the flights unchanged.
    bool throwFromLoadout(ThrowableFlights& flights, WeaponLoadout& weapons, ThrowableBelt& belt, const WeaponFireRequest& aim, Entity thrower, bool charged, std::string* thrownId);

} // namespace Dark
