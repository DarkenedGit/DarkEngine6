#pragma once

#include "ECS/Components.h"
#include "ECS/Entity.h"
#include "Network/NetTypes.h"

namespace Dark
{

    class World;

    struct NetworkedComponent
    {
        NetId     netId              = NULL_NET_ID; // 0 until host() if registered while Idle
        ClientId  owner              = ClientId::Host;
        NetPrefab prefab             = NetPrefab::Unknown;
        uint32_t  colorRgba8         = 0xFFFFFFFFu;
        bool      replicateTransform = true;
    };

    using NetSpawnFn   = bool (*)(World& world, Entity e, NetPrefab prefab, const TransformComponent& xf, uint32_t colorRgba8, void* user);
    using NetDespawnFn = void (*)(World& world, Entity e, NetId id, void* user);

} // namespace Dark
