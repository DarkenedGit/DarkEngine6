// NetReplicationApply.cpp — apply/send snapshot & pawn paths (NetworkSystem::)
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

    void NetworkSystem::applyReliable(World& world, Peer& peer, uint8_t op, const std::vector<uint8_t>& payload)
    {
        (void)peer;
        if (op == static_cast<uint8_t>(NetOpcode::Spawn) || op == static_cast<uint8_t>(NetOpcode::Despawn))
        {
            if (m_role != NetRole::Client)
            {
                warnDrop("host-only opcode");
                return;
            }
            if (op == static_cast<uint8_t>(NetOpcode::Spawn))
                applySpawn(world, payload.data(), static_cast<uint32_t>(payload.size()));
            else
                applyDespawn(world, payload.data(), static_cast<uint32_t>(payload.size()));
            return;
        }
    }

    void NetworkSystem::applyUnreliable(World& world, Peer& peer, uint8_t op, const uint8_t* payload, uint32_t size)
    {
        if (op == static_cast<uint8_t>(NetOpcode::Heartbeat))
        {
            PacketReader r;
            uint32_t     tick = 0;
            if (payload && r.begin(payload, size) && r.readU32(tick))
            {
                if (m_role == NetRole::Client)
                    m_echoTick = tick;
            }
            return;
        }
        if (op == static_cast<uint8_t>(NetOpcode::Disconnect))
        {
            DE_LOG_INFO(LogCategory::Networking, "NetworkSystem: peer {} disconnected", static_cast<unsigned>(peer.id));
            dropPeer(peer, m_role == NetRole::Host, false, kNetDisconnectUser);
            return;
        }
        if (op == static_cast<uint8_t>(NetOpcode::Snapshot))
        {
            if (m_role != NetRole::Client)
            {
                warnDrop("host-only opcode");
                return;
            }
            applySnapshot(world, payload, size);
            return;
        }
        if (op == static_cast<uint8_t>(NetOpcode::PawnState))
        {
            if (m_role != NetRole::Host)
            {
                warnDrop("pawn from non-client");
                return;
            }
            applyPawnState(world, peer, payload, size);
            return;
        }
        if (op == static_cast<uint8_t>(NetOpcode::Spawn) || op == static_cast<uint8_t>(NetOpcode::Despawn))
        {
            warnDrop("host-only opcode");
            return;
        }
    }

    void NetworkSystem::applySpawn(World& world, const uint8_t* payload, uint32_t size)
    {
        PacketReader r;
        SpawnPayload p{};
        if (!payload || !r.begin(payload, size) || !readSpawn(r, p) || p.netId == NULL_NET_ID)
            return;
        if (m_netToEntity.contains(p.netId))
            return;
        if (m_entityToNet.size() >= kNetMaxReplicated)
        {
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: inbound Spawn dropped, cap {}", kNetMaxReplicated);
            return;
        }
        if (!finite3(p.px, p.py, p.pz) || !finite4(p.qw, p.qx, p.qy, p.qz) || !finite3(p.sx, p.sy, p.sz))
        {
            warnNan("spawn");
            return;
        }
        normalizeQuat(p.qw, p.qx, p.qy, p.qz);

        Entity e = world.createEntity();
        TransformComponent xf;
        xf.position = Math::Vector3f(p.px, p.py, p.pz);
        xf.rotation = Math::Quaternion(p.qw, p.qx, p.qy, p.qz);
        xf.scale    = Math::Vector3f(p.sx, p.sy, p.sz);
        world.emplace<TagComponent>(e, TagComponent{ tagForPrefab(static_cast<NetPrefab>(p.prefab)) });
        world.emplace<TransformComponent>(e, xf);

        NetworkedComponent nc;
        nc.netId              = p.netId;
        nc.owner              = static_cast<ClientId>(p.owner);
        nc.prefab             = static_cast<NetPrefab>(p.prefab);
        nc.colorRgba8         = p.colorRgba8;
        nc.replicateTransform = (p.flags & 1u) != 0u;
        world.emplace<NetworkedComponent>(e, nc);

        m_netToEntity[p.netId] = e.id();
        m_entityToNet[e.id()]  = p.netId;
        rememberPawnPose(p.netId, p.px, p.py, p.pz);

        if (m_spawnFn && !m_spawnFn(world, e, nc.prefab, xf, p.colorRgba8, m_spawnUser))
        {
            DE_LOG_WARN(LogCategory::Networking, "NetworkSystem: spawn callback rejected NetId {}", p.netId);
            m_netToEntity.erase(p.netId);
            m_entityToNet.erase(e.id());
            m_pawnLast.erase(p.netId);
            if (world.alive(e))
                world.destroyEntity(e);
        }
    }

    void NetworkSystem::applyDespawn(World& world, const uint8_t* payload, uint32_t size)
    {
        PacketReader r;
        uint32_t     netId = 0;
        if (!payload || !r.begin(payload, size) || !readDespawn(r, netId) || netId == NULL_NET_ID)
            return;
        const Entity e = entityFor(netId);
        if (!e.valid())
            return;
        teardownEntity(world, e, netId, false);
    }

    void NetworkSystem::applySnapshot(World& world, const uint8_t* payload, uint32_t size)
    {
        (void)world;
        PacketReader r;
        uint32_t     serverTick = 0;
        uint8_t      count      = 0;
        if (!payload || !r.begin(payload, size) || !readSnapshotHeader(r, serverTick, count))
            return;
        if (count > kNetMaxReplicated)
        {
            warnDrop("snapshot count");
            return;
        }
        if (m_hasRecvTick && static_cast<int32_t>(serverTick - m_latestRecvTick) < 0)
            return;

        m_latestRecvTick = serverTick;
        m_hasRecvTick    = true;
        m_echoTick       = serverTick;

        for (uint8_t i = 0; i < count; ++i)
        {
            uint32_t netId = 0;
            float    px = 0.f, py = 0.f, pz = 0.f;
            float    qw = 1.f, qx = 0.f, qy = 0.f, qz = 0.f;
            if (!readSnapshotPose(r, netId, px, py, pz, qw, qx, qy, qz))
                return;
            if (netId == NULL_NET_ID || !m_netToEntity.contains(netId))
                continue;
            if (!finite3(px, py, pz) || !finite4(qw, qx, qy, qz))
            {
                warnNan("snapshot");
                continue;
            }
            normalizeQuat(qw, qx, qy, qz);
            pushInterpSlot(netId, serverTick, px, py, pz, qw, qx, qy, qz);
        }
    }

    void NetworkSystem::applyPawnState(World& world, Peer& peer, const uint8_t* payload, uint32_t size)
    {
        PacketReader r;
        PawnStatePayload p{};
        if (!payload || !r.begin(payload, size) || !readPawnState(r, p) || p.netId == NULL_NET_ID)
            return;
        if (!finite3(p.px, p.py, p.pz) || !finite4(p.qw, p.qx, p.qy, p.qz))
        {
            warnNan("pawn");
            return;
        }
        normalizeQuat(p.qw, p.qx, p.qy, p.qz);

        const Entity e = entityFor(p.netId);
        if (!e.valid())
            return;
        NetworkedComponent* nc = world.get<NetworkedComponent>(e);
        if (!nc || nc->owner != peer.id)
        {
            warnDrop("pawn owner");
            return;
        }

        PawnAccepted& last = m_pawnLast[p.netId];
        if (last.has)
        {
            const float dt = m_now - last.timeSec;
            const Math::Vector3f delta(p.px - last.px, p.py - last.py, p.pz - last.pz);
            const float dist = delta.Magnitude();
            if (dt <= 1.0e-6f)
            {
                if (dist > 1.0e-3f)
                {
                    warnDrop("pawn speed");
                    return;
                }
            }
            else if ((dist / dt) > kNetPawnMaxSpeed)
            {
                warnDrop("pawn speed");
                return;
            }
        }

        TransformComponent* xf = world.get<TransformComponent>(e);
        if (!xf)
            return;
        xf->position = Math::Vector3f(p.px, p.py, p.pz);
        xf->rotation = Math::Quaternion(p.qw, p.qx, p.qy, p.qz);
        xf->rotation.Normalize();
        rememberPawnPose(p.netId, p.px, p.py, p.pz);
    }

    bool NetworkSystem::sendSnapshot(Peer& peer, World& world)
    {
        struct Rec
        {
            uint32_t netId;
            float    px, py, pz;
            float    qw, qx, qy, qz;
        };
        Rec     recs[kNetMaxReplicated];
        uint8_t count = 0;
        world.each<NetworkedComponent>([&](Entity e, NetworkedComponent& nc) {
            if (count >= kNetMaxReplicated || nc.netId == NULL_NET_ID || !nc.replicateTransform)
                return;
            const TransformComponent* xf = world.get<TransformComponent>(e);
            if (!xf)
                return;
            float qw = xf->rotation.w, qx = xf->rotation.x, qy = xf->rotation.y, qz = xf->rotation.z;
            if (!finite3(xf->position.x, xf->position.y, xf->position.z) || !finite4(qw, qx, qy, qz))
                return;
            normalizeQuat(qw, qx, qy, qz);
            recs[count++] = Rec{ nc.netId, xf->position.x, xf->position.y, xf->position.z, qw, qx, qy, qz };
        });

        uint8_t      buf[kNetMaxPayload];
        PacketWriter w;
        if (!w.begin(buf, sizeof(buf)) || !writeSnapshotHeader(w, m_serverTick, count))
            return false;
        for (uint8_t i = 0; i < count; ++i)
        {
            if (!writeSnapshotPose(w, recs[i].netId, recs[i].px, recs[i].py, recs[i].pz, recs[i].qw, recs[i].qx, recs[i].qy, recs[i].qz))
                return false;
        }
        return peer.channel.setUnreliable(static_cast<uint8_t>(NetOpcode::Snapshot), buf, w.size());
    }

    bool NetworkSystem::sendPawnState(Peer& peer, World& world)
    {
        const Entity e = localPawn();
        if (!e.valid())
            return false;
        const NetworkedComponent* nc = world.get<NetworkedComponent>(e);
        const TransformComponent* xf = world.get<TransformComponent>(e);
        if (!nc || !xf || nc->netId == NULL_NET_ID)
            return false;

        PawnStatePayload p{};
        p.netId = nc->netId;
        p.px    = xf->position.x;
        p.py    = xf->position.y;
        p.pz    = xf->position.z;
        p.qw    = xf->rotation.w;
        p.qx    = xf->rotation.x;
        p.qy    = xf->rotation.y;
        p.qz    = xf->rotation.z;
        if (!finite3(p.px, p.py, p.pz) || !finite4(p.qw, p.qx, p.qy, p.qz))
            return false;
        normalizeQuat(p.qw, p.qx, p.qy, p.qz);

        uint8_t      buf[kPawnStateBytes];
        PacketWriter w;
        if (!w.begin(buf, sizeof(buf)) || !writePawnState(w, p))
            return false;
        return peer.channel.setUnreliable(static_cast<uint8_t>(NetOpcode::PawnState), buf, w.size());
    }

} // namespace Dark
