#pragma once

#include "Network/NetTypes.h"
#include "Network/Packet.h"

#include <cstdint>
#include <cstring>

namespace Dark
{

    enum class NetOpcode : uint8_t
    {
        Invalid        = 0,
        ConnectRequest = 1,
        ConnectAccept  = 2,
        ConnectReject  = 3,
        Disconnect     = 4,
        Heartbeat      = 5,
        Spawn          = 6,
        Despawn        = 7,
        Snapshot       = 8,
        PawnState      = 9,
        Beacon         = 10
    };

    constexpr uint32_t kConnectRequestBytes = 44;
    constexpr uint32_t kConnectAcceptBytes  = 20;

    struct ConnectRequestPayload
    {
        char     name[32]{};
        uint64_t playerId  = 0;
        uint8_t  wantsPawn = 0;
        uint8_t  sceneMode = 0;
    };

    struct ConnectAcceptPayload
    {
        uint8_t  clientId   = 0;
        uint32_t connToken  = 0;
        uint8_t  netTickHz  = 0;
        uint8_t  maxClients = 0;
        uint32_t serverTick = 0;
        uint8_t  sceneMode  = 0;
    };

    inline bool writeU64LE(PacketWriter& w, uint64_t v)
    {
        return w.writeU32(static_cast<uint32_t>(v)) && w.writeU32(static_cast<uint32_t>(v >> 32));
    }

    inline bool readU64LE(PacketReader& r, uint64_t& v)
    {
        uint32_t lo = 0;
        uint32_t hi = 0;
        if (!r.readU32(lo) || !r.readU32(hi))
            return false;
        v = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
        return true;
    }

    inline bool writeConnectRequest(PacketWriter& w, const ConnectRequestPayload& p)
    {
        return w.writeBytes(p.name, 32) && writeU64LE(w, p.playerId) && w.writeU8(p.wantsPawn) && w.writeU8(p.sceneMode) && w.writeU8(0) && w.writeU8(0);
    }

    inline bool readConnectRequest(PacketReader& r, ConnectRequestPayload& p)
    {
        uint8_t pad0 = 0;
        uint8_t pad1 = 0;
        if (!r.readBytes(p.name, 32) || !readU64LE(r, p.playerId) || !r.readU8(p.wantsPawn) || !r.readU8(p.sceneMode) || !r.readU8(pad0) || !r.readU8(pad1))
            return false;
        p.name[31] = 0;
        return true;
    }

    inline bool writeConnectAccept(PacketWriter& w, const ConnectAcceptPayload& p)
    {
        return w.writeU8(p.clientId) && w.writeU8(0) && w.writeU8(0) && w.writeU8(0) && w.writeU32(p.connToken) && w.writeU8(p.netTickHz) && w.writeU8(p.maxClients) && w.writeU8(0) && w.writeU8(0) &&
               w.writeU32(p.serverTick) && w.writeU8(p.sceneMode) && w.writeU8(0) && w.writeU8(0) && w.writeU8(0);
    }

    inline bool readConnectAccept(PacketReader& r, ConnectAcceptPayload& p)
    {
        uint8_t pad = 0;
        if (!r.readU8(p.clientId) || !r.readU8(pad) || !r.readU8(pad) || !r.readU8(pad) || !r.readU32(p.connToken) || !r.readU8(p.netTickHz) || !r.readU8(p.maxClients) || !r.readU8(pad) ||
            !r.readU8(pad) || !r.readU32(p.serverTick) || !r.readU8(p.sceneMode) || !r.readU8(pad) || !r.readU8(pad) || !r.readU8(pad))
            return false;
        return true;
    }

    inline void copyNetName(char dst[32], const char* src)
    {
        std::memset(dst, 0, 32);
        if (!src)
            return;
        for (uint32_t i = 0; i < 31 && src[i]; ++i)
        {
            const unsigned char c = static_cast<unsigned char>(src[i]);
            if (c > 127)
                break;
            dst[i] = static_cast<char>(c);
        }
    }

    constexpr uint32_t kBeaconBytes = 36;

    struct BeaconPayload
    {
        uint16_t hostPort  = 0;
        uint8_t  peerCount = 0;
        uint8_t  sceneMode = 0;
        char     name[32]{};
    };

    inline bool writeBeacon(PacketWriter& w, const BeaconPayload& p)
    {
        return w.writeU16(p.hostPort) && w.writeU8(p.peerCount) && w.writeU8(p.sceneMode) && w.writeBytes(p.name, 32);
    }

    inline bool readBeacon(PacketReader& r, BeaconPayload& p)
    {
        if (!r.readU16(p.hostPort) || !r.readU8(p.peerCount) || !r.readU8(p.sceneMode) || !r.readBytes(p.name, 32))
            return false;
        p.name[31] = 0;
        return true;
    }

    constexpr uint32_t kSpawnBytes     = 52;
    constexpr uint32_t kDespawnBytes   = 4;
    constexpr uint32_t kPawnStateBytes = 32;
    constexpr uint32_t kSnapshotHeaderBytes = 8;
    constexpr uint32_t kSnapshotPoseBytes   = 32;

    struct SpawnPayload
    {
        uint32_t netId      = 0;
        uint8_t  prefab     = 0;
        uint8_t  owner      = 0;
        uint8_t  flags      = 0;
        float    px         = 0.f;
        float    py         = 0.f;
        float    pz         = 0.f;
        float    qw         = 1.f;
        float    qx         = 0.f;
        float    qy         = 0.f;
        float    qz         = 0.f;
        float    sx         = 1.f;
        float    sy         = 1.f;
        float    sz         = 1.f;
        uint32_t colorRgba8 = 0xFFFFFFFFu;
    };

    struct PawnStatePayload
    {
        uint32_t netId = 0;
        float    px    = 0.f;
        float    py    = 0.f;
        float    pz    = 0.f;
        float    qw    = 1.f;
        float    qx    = 0.f;
        float    qy    = 0.f;
        float    qz    = 0.f;
    };

    inline bool writeSpawn(PacketWriter& w, const SpawnPayload& p)
    {
        return w.writeU32(p.netId) && w.writeU8(p.prefab) && w.writeU8(p.owner) && w.writeU8(p.flags) && w.writeU8(0) && w.writeF32(p.px) && w.writeF32(p.py) && w.writeF32(p.pz) && w.writeF32(p.qw) &&
               w.writeF32(p.qx) && w.writeF32(p.qy) && w.writeF32(p.qz) && w.writeF32(p.sx) && w.writeF32(p.sy) && w.writeF32(p.sz) && w.writeU32(p.colorRgba8);
    }

    inline bool readSpawn(PacketReader& r, SpawnPayload& p)
    {
        uint8_t pad = 0;
        return r.readU32(p.netId) && r.readU8(p.prefab) && r.readU8(p.owner) && r.readU8(p.flags) && r.readU8(pad) && r.readF32(p.px) && r.readF32(p.py) && r.readF32(p.pz) && r.readF32(p.qw) &&
               r.readF32(p.qx) && r.readF32(p.qy) && r.readF32(p.qz) && r.readF32(p.sx) && r.readF32(p.sy) && r.readF32(p.sz) && r.readU32(p.colorRgba8);
    }

    inline bool writeDespawn(PacketWriter& w, uint32_t netId)
    {
        return w.writeU32(netId);
    }

    inline bool readDespawn(PacketReader& r, uint32_t& netId)
    {
        return r.readU32(netId);
    }

    inline bool writePawnState(PacketWriter& w, const PawnStatePayload& p)
    {
        return w.writeU32(p.netId) && w.writeF32(p.px) && w.writeF32(p.py) && w.writeF32(p.pz) && w.writeF32(p.qw) && w.writeF32(p.qx) && w.writeF32(p.qy) && w.writeF32(p.qz);
    }

    inline bool readPawnState(PacketReader& r, PawnStatePayload& p)
    {
        return r.readU32(p.netId) && r.readF32(p.px) && r.readF32(p.py) && r.readF32(p.pz) && r.readF32(p.qw) && r.readF32(p.qx) && r.readF32(p.qy) && r.readF32(p.qz);
    }

    inline bool writeSnapshotHeader(PacketWriter& w, uint32_t serverTick, uint8_t count)
    {
        return w.writeU32(serverTick) && w.writeU8(count) && w.writeU8(0) && w.writeU8(0) && w.writeU8(0);
    }

    inline bool readSnapshotHeader(PacketReader& r, uint32_t& serverTick, uint8_t& count)
    {
        uint8_t pad = 0;
        return r.readU32(serverTick) && r.readU8(count) && r.readU8(pad) && r.readU8(pad) && r.readU8(pad);
    }

    inline bool writeSnapshotPose(PacketWriter& w, uint32_t netId, float px, float py, float pz, float qw, float qx, float qy, float qz)
    {
        return w.writeU32(netId) && w.writeF32(px) && w.writeF32(py) && w.writeF32(pz) && w.writeF32(qw) && w.writeF32(qx) && w.writeF32(qy) && w.writeF32(qz);
    }

    inline bool readSnapshotPose(PacketReader& r, uint32_t& netId, float& px, float& py, float& pz, float& qw, float& qx, float& qy, float& qz)
    {
        return r.readU32(netId) && r.readF32(px) && r.readF32(py) && r.readF32(pz) && r.readF32(qw) && r.readF32(qx) && r.readF32(qy) && r.readF32(qz);
    }

} // namespace Dark

