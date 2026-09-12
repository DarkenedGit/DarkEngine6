#pragma once

#include "ECS/Components.h"
#include "ECS/Entity.h"
#include "Network/NetTypes.h"

namespace Dark
{

    class World;

    struct NetworkedComponent
    {
        static constexpr const char* kTypeName = "Networked";

        NetId     netId              = NULL_NET_ID; // 0 until host() if registered while Idle
        ClientId  owner              = ClientId::Host;
        NetPrefab prefab             = NetPrefab::Unknown;
        uint32_t  colorRgba8         = 0xFFFFFFFFu;
        bool      replicateTransform = true;
    };

    // ─── Draw / spawn contract (apps, not NetworkSystem) ─────────────────────────
    // NetworkSystem never submits draws. Hosts must make replicas visible:
    //
    // * Sandbox / Sandbox2D: draw path iterates ECS (`each<NetworkedComponent>` or
    //   the 2D parallel arrays filled by NetSpawnFn). NetSpawnFn must attach any
    //   components the draw path expects (e.g. MeshComponent).
    // * Editor: World is the only live graph. NetSpawnFn must attach MeshComponent
    //   (draw) and EditorObjectComponent (selection / outliner / type / tint).
    //   Do not maintain a parallel m_objects vector.
    //
    // Spawning into World without satisfying the above produces an invisible replica.

    using NetSpawnFn   = bool (*)(World& world, Entity e, NetPrefab prefab, const TransformComponent& xf, uint32_t colorRgba8, void* user);
    using NetDespawnFn = void (*)(World& world, Entity e, NetId id, void* user);

} // namespace Dark
