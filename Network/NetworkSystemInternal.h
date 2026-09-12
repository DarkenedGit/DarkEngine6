#pragma once

// Shared helpers for NetworkSystem translation units (not a public API).

#include "Network/NetTypes.h"
#include "Network/Protocol.h"
#include "Network/Transport.h"
#include "Math/Quaternion.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace Dark
{
namespace NetSysDetail
{
    constexpr uint32_t kRateSlotCap = 64;

    inline uint16_t nextId(uint16_t id)
    {
        ++id;
        if (id == 0)
            id = 1;
        return id;
    }

    inline NetId nextNetIdValue(NetId id)
    {
        ++id;
        if (id == 0)
            id = 1;
        return id;
    }

    inline bool finite3(float x, float y, float z)
    {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }

    inline bool finite4(float w, float x, float y, float z)
    {
        return std::isfinite(w) && std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }

    inline void normalizeQuat(float& w, float& x, float& y, float& z)
    {
        Math::Quaternion q(w, x, y, z);
        q.Normalize();
        w = q.w;
        x = q.x;
        y = q.y;
        z = q.z;
    }

    inline const char* tagForPrefab(NetPrefab prefab)
    {
        switch (prefab)
        {
        case NetPrefab::Cube:
            return "Cube";
        case NetPrefab::Sphere:
            return "Sphere";
        case NetPrefab::PlayerPawn:
            return "PlayerPawn";
        case NetPrefab::Platform:
            return "Platform";
        case NetPrefab::Coin:
            return "Coin";
        case NetPrefab::Player2D:
            return "Player2D";
        default:
            return "Net";
        }
    }

    inline bool isPawnPrefab(NetPrefab prefab)
    {
        return prefab == NetPrefab::PlayerPawn || prefab == NetPrefab::Player2D;
    }

    struct ParsedDatagram
    {
        DatagramHeader   header{};
        uint8_t          opcode       = 0;
        const uint8_t*   payload      = nullptr;
        uint32_t         payloadSize  = 0;
        bool             hasUnreliable = false;
        bool             headerOk      = false;
        bool             framed        = false;
    };

    inline ParsedDatagram parseDatagram(const uint8_t* data, uint32_t size)
    {
        ParsedDatagram out;
        if (!data || size < kNetHeaderSize || size > kNetMaxPayload)
            return out;

        PacketReader r;
        if (!r.begin(data, size) || !readDatagramHeader(r, out.header))
            return out;
        out.headerOk = true;
        if (out.header.magic != kNetMagic)
            return out;
        if (out.header.headerSize < kNetHeaderSize || out.header.headerSize > size)
            return out;
        if (out.header.seq == 0)
            return out;

        const uint32_t extra = static_cast<uint32_t>(out.header.headerSize) - kNetHeaderSize;
        if (extra)
        {
            uint8_t skip[256];
            if (extra > sizeof(skip) || !r.readBytes(skip, extra))
                return out;
        }

        for (uint8_t i = 0; i < out.header.reliableCount; ++i)
        {
            uint16_t id  = 0;
            uint16_t len = 0;
            uint8_t  op  = 0;
            if (!r.readU16(id) || !r.readU16(len) || !r.readU8(op))
                return out;
            if (len == 0 || len > kNetMaxReliableLen)
                return out;
            const uint32_t payloadLen = static_cast<uint32_t>(len) - 1u;
            if (payloadLen)
            {
                uint8_t skip[kNetMaxReliableLen];
                if (!r.readBytes(skip, payloadLen))
                    return out;
            }
        }

        if (r.remaining() > 0)
        {
            if (!r.readU8(out.opcode))
                return out;
            out.hasUnreliable = true;
            out.payloadSize   = r.remaining();
            out.payload       = data + (size - out.payloadSize);
        }

        out.framed = true;
        return out;
    }

    struct OutCountTransport final : ITransport
    {
        ITransport* inner      = nullptr;
        uint64_t*   packetsOut = nullptr;
        uint64_t*   bytesOut   = nullptr;

        bool sendTo(const Address& dest, const void* data, uint32_t size) override
        {
            if (!inner || !inner->sendTo(dest, data, size))
                return false;
            if (packetsOut)
                ++*packetsOut;
            if (bytesOut)
                *bytesOut += size;
            return true;
        }

        bool recvFrom(Address& src, void* buffer, uint32_t capacity, uint32_t& outSize) override
        {
            return inner ? inner->recvFrom(src, buffer, capacity, outSize) : false;
        }

        Address localAddress() const override { return inner ? inner->localAddress() : Address{}; }
        void    close() override {}
    };
} // namespace NetSysDetail
} // namespace Dark
