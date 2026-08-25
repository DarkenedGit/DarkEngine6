#pragma once

#include "Debug/DebugTypes.h"
#include "Network/Packet.h"

#include <cstdint>
#include <vector>

namespace Dark
{

    struct DebugHeader
    {
        uint32_t     magic        = 0;
        uint8_t      version      = 0;
        DebugOpcode  opcode       = DebugOpcode::Invalid;
        uint8_t      flags        = 0;
        uint32_t     payloadBytes = 0;
    };

    bool writeDebugFrame(uint8_t* buf, uint32_t cap, DebugOpcode opcode, const uint8_t* payload, uint32_t payloadBytes, uint32_t& outSize);

    bool writeHelloPayload(PacketWriter& w, const DebugHello& p);
    bool readHelloPayload(PacketReader& r, DebugHello& p);
    bool writeHelloAckPayload(PacketWriter& w, const DebugHelloAck& p);
    bool readHelloAckPayload(PacketReader& r, DebugHelloAck& p);
    bool writeSubscribePayload(PacketWriter& w, uint32_t channels);
    bool readSubscribePayload(PacketReader& r, uint32_t& channels);
    bool writeLogFilterPayload(PacketWriter& w, LogLevel minLevel, uint32_t categoryMask);
    bool readLogFilterPayload(PacketReader& r, LogLevel& minLevel, uint32_t& categoryMask);
    bool writeLogLinePayload(PacketWriter& w, const DebugLogEntry& e);
    bool readLogLinePayload(PacketReader& r, DebugLogEntry& e);
    bool writeMemorySnapshotPayload(PacketWriter& w, const DebugMemoryPool* pools, uint32_t count);
    bool readMemorySnapshotPayload(PacketReader& r, DebugMemoryPool* pools, uint32_t cap, uint32_t& count);
    bool writePerfSnapshotPayload(PacketWriter& w, const DebugPerfSnapshot& s);
    bool readPerfSnapshotPayload(PacketReader& r, DebugPerfSnapshot& s);
    bool writeHeartbeatPayload(PacketWriter& w, uint32_t counter);
    bool readHeartbeatPayload(PacketReader& r, uint32_t& counter);

    void copyDebugName(char dst[kDebugNameBytes], const char* src);

    class DebugFramer
    {
    public:
        bool pushBytes(const uint8_t* data, uint32_t n);
        bool popMessage(DebugHeader& hdr, std::vector<uint8_t>& payload);
        bool ok() const { return m_ok; }
        void reset();

    private:
        bool compact();

        std::vector<uint8_t> m_buf;
        uint32_t             m_pos = 0;
        bool                 m_ok  = true;
    };

} // namespace Dark
