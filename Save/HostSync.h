#pragma once

#include "Core/EntityPins.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Network/NetworkSystem.h"
#include "Physics/PhysicsBodyComponent.h"
#include "Physics/PhysicsWorld.h"

namespace Dark::Save
{
    inline void pullDynamicVelocities(World& world, Physics::PhysicsWorld& physics)
    {
        if (!physics.valid())
            return;
        world.each<PhysicsBodyComponent>([&](Entity, PhysicsBodyComponent& body) {
            if (body.mode != PhysicsBodyMode::Dynamic || !body.valid)
                return;
            physics.getBodyVelocity(body.body, body.linearVelocity, body.angularVelocity);
        });
    }

    inline void pushDynamicProgress(World& world, Physics::PhysicsWorld& physics)
    {
        if (!physics.valid())
            return;
        world.each<PhysicsBodyComponent>([&](Entity e, PhysicsBodyComponent& body) {
            if (body.mode != PhysicsBodyMode::Dynamic || !body.valid)
                return;
            if (const TransformComponent* xf = world.get<TransformComponent>(e))
                physics.setBodyPose(body.body, xf->position, xf->rotation);
            physics.setBodyVelocity(body.body, body.linearVelocity, body.angularVelocity);
        });
    }

    // unregisterEntity already despawns and destroys a networked entity.
    inline void destroyProgressEntity(World& world, Entity e, NetworkSystem& network, AssetPinTable& pins)
    {
        if (!e.valid())
            return;
        if (world.has<NetworkedComponent>(e))
        {
            network.unregisterEntity(world, e);
            return;
        }
        onEntityRemoved(world, e, &pins);
        world.destroyEntity(e);
    }

    inline bool skipRemoteOwner(World& world, Entity e)
    {
        const NetworkedComponent* nc = world.get<NetworkedComponent>(e);
        return nc && nc->owner != ClientId::Host;
    }

    inline int hostNetRole(const NetworkSystem& network)
    {
        return static_cast<int>(network.role());
    }

    inline uint32_t hostPeerCount(const NetworkSystem& network)
    {
        return network.peerCount();
    }
} // namespace Dark::Save
