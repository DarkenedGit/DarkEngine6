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

} // namespace Dark
