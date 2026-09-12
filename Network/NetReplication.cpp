// NetReplication.cpp — entity↔NetId maps, register/unregister, spawn/despawn queues (NetworkSystem::)
#include "Network/NetworkSystem.h"
#include "Network/NetworkSystemInternal.h"
#include "Network/Protocol.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

#include <cstring>
#include <unordered_map>
#include <vector>

namespace Dark
{
    using namespace NetSysDetail;

    void NetworkSystem::bindWorld(World& world)
    {
        m_world = &world;
    }

    void NetworkSystem::clearReplicaMaps()
    {
        m_netToEntity.clear();
        m_entityToNet.clear();
        m_interp.clear();
        m_pawnLast.clear();
        m_nextNetId      = 1;
        m_latestRecvTick = 0;
        m_hasRecvTick    = false;
    }

    void NetworkSystem::destroyRemoteReplicas()
    {
        if (!m_world)
        {
            clearReplicaMaps();
            return;
        }

        std::vector<EntityID> ids;
        ids.reserve(m_entityToNet.size());
        for (const auto& kv : m_entityToNet)
            ids.push_back(kv.first);

        World& world = *m_world;
        for (EntityID eid : ids)
        {
            const auto it = m_entityToNet.find(eid);
            const NetId nid = (it != m_entityToNet.end()) ? it->second : NULL_NET_ID;
            teardownEntity(world, Entity{ eid }, nid, false);
        }
        clearReplicaMaps();
    }

    void NetworkSystem::parkHostReplicas()
    {
        if (m_world)
        {
            m_world->each<NetworkedComponent>([](Entity, NetworkedComponent& nc) { nc.netId = NULL_NET_ID; });
            std::unordered_map<EntityID, NetId> parked;
            m_world->each<NetworkedComponent>([&](Entity e, NetworkedComponent&) { parked[e.id()] = NULL_NET_ID; });
            m_entityToNet.swap(parked);
        }
        else
        {
            for (auto& kv : m_entityToNet)
                kv.second = NULL_NET_ID;
        }
        m_netToEntity.clear();
        m_interp.clear();
        m_pawnLast.clear();
        m_nextNetId      = 1;
        m_latestRecvTick = 0;
        m_hasRecvTick    = false;
    }

    void NetworkSystem::assignIdleNetIds(World& world)
    {
        std::vector<EntityID> ids;
        ids.reserve(m_entityToNet.size());
        for (const auto& kv : m_entityToNet)
            ids.push_back(kv.first);

        world.each<NetworkedComponent>([&](Entity e, NetworkedComponent&) {
            if (!m_entityToNet.contains(e.id()))
                ids.push_back(e.id());
        });

        for (EntityID eid : ids)
        {
            Entity e{ eid };
            NetworkedComponent* nc = world.get<NetworkedComponent>(e);
            if (!nc || nc->netId != NULL_NET_ID)
                continue;
            const NetId id = m_nextNetId;
            m_nextNetId    = nextNetIdValue(m_nextNetId);
            nc->netId          = id;
            m_entityToNet[eid] = id;
            m_netToEntity[id]  = eid;
            if (const TransformComponent* xf = world.get<TransformComponent>(e))
                rememberPawnPose(id, xf->position.x, xf->position.y, xf->position.z);
            else
                rememberPawnPose(id, 0.f, 0.f, 0.f);
        }
    }

    bool NetworkSystem::queueSpawn(Peer& peer, World& world, Entity e, const NetworkedComponent& nc)
    {
        if (nc.netId == NULL_NET_ID)
            return false;

        SpawnPayload p{};
        p.netId      = nc.netId;
        p.prefab     = static_cast<uint8_t>(nc.prefab);
        p.owner      = static_cast<uint8_t>(nc.owner);
        p.flags      = nc.replicateTransform ? 1u : 0u;
        p.colorRgba8 = nc.colorRgba8;
        if (const TransformComponent* xf = world.get<TransformComponent>(e))
        {
            p.px = xf->position.x;
            p.py = xf->position.y;
            p.pz = xf->position.z;
            p.qw = xf->rotation.w;
            p.qx = xf->rotation.x;
            p.qy = xf->rotation.y;
            p.qz = xf->rotation.z;
            p.sx = xf->scale.x;
            p.sy = xf->scale.y;
            p.sz = xf->scale.z;
        }
        if (!finite3(p.px, p.py, p.pz) || !finite4(p.qw, p.qx, p.qy, p.qz) || !finite3(p.sx, p.sy, p.sz))
        {
            warnNan("spawn");
            return false;
        }
        normalizeQuat(p.qw, p.qx, p.qy, p.qz);

        uint8_t      buf[kSpawnBytes];
        PacketWriter w;
        if (!w.begin(buf, sizeof(buf)) || !writeSpawn(w, p))
            return false;
        return peer.channel.queueReliable(static_cast<uint8_t>(NetOpcode::Spawn), buf, w.size());
    }

    void NetworkSystem::queueSpawnAllPeers(World& world, Entity e, const NetworkedComponent& nc)
    {
        for (auto& p : m_peers)
        {
            if (p)
                queueSpawn(*p, world, e, nc);
        }
    }

    void NetworkSystem::queueDespawnAllPeers(NetId id)
    {
        if (id == NULL_NET_ID)
            return;
        uint8_t      buf[kDespawnBytes];
        PacketWriter w;
        if (!w.begin(buf, sizeof(buf)) || !writeDespawn(w, id))
            return;
        for (auto& p : m_peers)
        {
            if (p)
                p->channel.queueReliable(static_cast<uint8_t>(NetOpcode::Despawn), buf, w.size());
        }
    }

    void NetworkSystem::queueJoinSpawns(Peer& peer, World& world)
    {
        std::vector<EntityID> ids;
        ids.reserve(m_entityToNet.size());
        for (const auto& kv : m_entityToNet)
            ids.push_back(kv.first);
        for (EntityID eid : ids)
        {
            Entity e{ eid };
            const NetworkedComponent* nc = world.get<NetworkedComponent>(e);
            if (nc && nc->netId != NULL_NET_ID)
                queueSpawn(peer, world, e, *nc);
        }
    }

    void NetworkSystem::rememberPawnPose(NetId id, float px, float py, float pz)
    {
        PawnAccepted& acc = m_pawnLast[id];
        acc.px            = px;
        acc.py            = py;
        acc.pz            = pz;
        acc.timeSec       = m_now;
        acc.has           = true;
    }

    void NetworkSystem::teardownEntity(World& world, Entity e, NetId id, bool queueDespawn)
    {
        if (queueDespawn && m_role == NetRole::Host && id != NULL_NET_ID)
            queueDespawnAllPeers(id);

        if (id != NULL_NET_ID)
        {
            m_netToEntity.erase(id);
            m_interp.erase(id);
            m_pawnLast.erase(id);
        }
        m_entityToNet.erase(e.id());

        if (world.has<NetworkedComponent>(e))
            world.remove<NetworkedComponent>(e);

        if (m_despawnFn)
            m_despawnFn(world, e, id, m_despawnUser);

        if (world.alive(e))
            world.destroyEntity(e);
    }

    bool NetworkSystem::registerEntity(World& world, Entity e, NetPrefab prefab, ClientId owner, uint32_t colorRgba8)
    {
        bindWorld(world);
        if (m_role != NetRole::Idle && m_role != NetRole::Host)
        {
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: registerEntity rejected in role {}", static_cast<unsigned>(m_role));
            return false;
        }
        if (!e.valid() || !world.alive(e))
        {
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: registerEntity on invalid entity");
            return false;
        }
        if (world.has<NetworkedComponent>(e))
            return true;
        if (m_entityToNet.size() >= kNetMaxReplicated)
        {
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: registerEntity cap {} reached", kNetMaxReplicated);
            return false;
        }

        NetworkedComponent nc;
        nc.netId              = NULL_NET_ID;
        nc.owner              = owner;
        nc.prefab             = prefab;
        nc.colorRgba8         = colorRgba8;
        nc.replicateTransform = true;

        if (m_role == NetRole::Host)
        {
            nc.netId   = m_nextNetId;
            m_nextNetId = nextNetIdValue(m_nextNetId);
        }

        world.emplace<NetworkedComponent>(e, nc);
        m_entityToNet[e.id()] = nc.netId;
        if (nc.netId != NULL_NET_ID)
        {
            m_netToEntity[nc.netId] = e.id();
            if (const TransformComponent* xf = world.get<TransformComponent>(e))
                rememberPawnPose(nc.netId, xf->position.x, xf->position.y, xf->position.z);
            else
                rememberPawnPose(nc.netId, 0.f, 0.f, 0.f);
            queueSpawnAllPeers(world, e, nc);
        }
        return true;
    }

    void NetworkSystem::unregisterEntity(World& world, Entity e)
    {
        bindWorld(world);
        if (!world.has<NetworkedComponent>(e))
            return;
        const NetworkedComponent* nc = world.get<NetworkedComponent>(e);
        const NetId id = nc ? nc->netId : netIdFor(e);
        teardownEntity(world, e, id, true);
    }

    Entity NetworkSystem::entityFor(NetId id) const
    {
        if (id == NULL_NET_ID)
            return {};
        const auto it = m_netToEntity.find(id);
        if (it == m_netToEntity.end())
            return {};
        return Entity{ it->second };
    }

    NetId NetworkSystem::netIdFor(Entity e) const
    {
        const auto it = m_entityToNet.find(e.id());
        if (it == m_entityToNet.end())
            return NULL_NET_ID;
        return it->second;
    }

    Entity NetworkSystem::localPawn() const
    {
        if (!m_world || m_localId == ClientId::Invalid)
            return {};
        for (const auto& kv : m_entityToNet)
        {
            Entity e{ kv.first };
            const NetworkedComponent* nc = m_world->get<NetworkedComponent>(e);
            if (nc && nc->owner == m_localId && isPawnPrefab(nc->prefab))
                return e;
        }
        return {};
    }

    void NetworkSystem::applyDelivered(World& world, Peer& peer)
    {
        uint8_t              relOp = 0;
        std::vector<uint8_t> relPayload;
        while (peer.channel.popReliable(relOp, relPayload))
            applyReliable(world, peer, relOp, relPayload);

        if (!peer.channel.hasUnreliable())
            return;

        const uint8_t op = peer.channel.unreliableOpcode();
        const auto&   pl = peer.channel.unreliablePayload();
        applyUnreliable(world, peer, op, pl.data(), static_cast<uint32_t>(pl.size()));
    }

} // namespace Dark
