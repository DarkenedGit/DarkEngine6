#include "Debug/DebugProtocol.h"

#include <cstring>

namespace Dark
{

    void copyDebugName(char dst[kDebugNameBytes], const char* src)
    {
        std::memset(dst, 0, kDebugNameBytes);
        if (!src)
            return;
        uint32_t n = 0;
        while (src[n] && n + 1 < kDebugNameBytes)
        {
            dst[n] = src[n];
            ++n;
        }
    }

    bool writeDebugFrame(uint8_t* buf, uint32_t cap, DebugOpcode opcode, const uint8_t* payload, uint32_t payloadBytes, uint32_t& outSize)
    {
        outSize = 0;
        if (!buf)
            return false;
        if (payloadBytes > kDebugMaxPayload)
            return false;
        if (payloadBytes > 0 && !payload)
            return false;

        PacketWriter w;
        if (!w.begin(buf, cap))
            return false;
        if (!w.writeU32(kDebugMagic) || !w.writeU8(kDebugProtocolVersion) || !w.writeU8(static_cast<uint8_t>(opcode)) || !w.writeU8(0) || !w.writeU8(0) || !w.writeU32(payloadBytes))
            return false;
        if (payloadBytes > 0 && !w.writeBytes(payload, payloadBytes))
            return false;
        outSize = w.size();
        return true;
    }

    bool writeHelloPayload(PacketWriter& w, const DebugHello& p)
    {
        return w.writeBytes(p.name, kDebugNameBytes) && w.writeU32(p.pid) && w.writeU32(p.channels);
    }

    bool readHelloPayload(PacketReader& r, DebugHello& p)
    {
        if (!r.readBytes(p.name, kDebugNameBytes) || !r.readU32(p.pid) || !r.readU32(p.channels))
            return false;
        p.name[kDebugNameBytes - 1] = 0;
        return true;
    }

    bool writeHelloAckPayload(PacketWriter& w, const DebugHelloAck& p)
    {
        return w.writeU8(p.protocolVersion) && w.writeU8(0) && w.writeU8(0) && w.writeU8(0) && w.writeU16(p.listenPort) && w.writeU8(0) && w.writeU8(0) && w.writeBytes(p.title, kDebugNameBytes);
    }

    bool readHelloAckPayload(PacketReader& r, DebugHelloAck& p)
    {
        uint8_t pad0 = 0, pad1 = 0, pad2 = 0, pad3 = 0, pad4 = 0;
        if (!r.readU8(p.protocolVersion) || !r.readU8(pad0) || !r.readU8(pad1) || !r.readU8(pad2) || !r.readU16(p.listenPort) || !r.readU8(pad3) || !r.readU8(pad4) || !r.readBytes(p.title, kDebugNameBytes))
            return false;
        p.title[kDebugNameBytes - 1] = 0;
        return true;
    }

    bool writeSubscribePayload(PacketWriter& w, uint32_t channels)
    {
        return w.writeU32(channels);
    }

    bool readSubscribePayload(PacketReader& r, uint32_t& channels)
    {
        return r.readU32(channels);
    }

    bool writeLogFilterPayload(PacketWriter& w, LogLevel minLevel, uint32_t categoryMask)
    {
        return w.writeU8(static_cast<uint8_t>(minLevel)) && w.writeU8(0) && w.writeU8(0) && w.writeU8(0) && w.writeU32(categoryMask);
    }

    bool readLogFilterPayload(PacketReader& r, LogLevel& minLevel, uint32_t& categoryMask)
    {
        uint8_t lv = 0, p0 = 0, p1 = 0, p2 = 0;
        if (!r.readU8(lv) || !r.readU8(p0) || !r.readU8(p1) || !r.readU8(p2) || !r.readU32(categoryMask))
            return false;
        minLevel = static_cast<LogLevel>(lv);
        return true;
    }

    bool writeLogLinePayload(PacketWriter& w, const DebugLogEntry& e)
    {
        uint16_t textLen = 0;
        while (textLen < kDebugMaxLogText && e.text[textLen])
            ++textLen;
        return w.writeU64(e.timestampMs) && w.writeU8(static_cast<uint8_t>(e.level)) && w.writeU8(static_cast<uint8_t>(e.category)) && w.writeU16(e.dropped) && w.writeU16(textLen) && w.writeU8(0)
               && w.writeU8(0) && w.writeBytes(e.text, textLen);
    }

    bool readLogLinePayload(PacketReader& r, DebugLogEntry& e)
    {
        uint8_t  level = 0, cat = 0, p0 = 0, p1 = 0;
        uint16_t textLen = 0;
        if (!r.readU64(e.timestampMs) || !r.readU8(level) || !r.readU8(cat) || !r.readU16(e.dropped) || !r.readU16(textLen) || !r.readU8(p0) || !r.readU8(p1))
            return false;
        if (textLen > kDebugMaxLogText)
            return false;
        e.level    = static_cast<LogLevel>(level);
        e.category = static_cast<LogCategory>(cat);
        std::memset(e.text, 0, sizeof(e.text));
        if (textLen > 0 && !r.readBytes(e.text, textLen))
            return false;
        e.text[textLen] = 0;
        return true;
    }

    bool writeMemorySnapshotPayload(PacketWriter& w, const DebugMemoryPool* pools, uint32_t count)
    {
        if (count > kDebugMaxPools)
            return false;
        if (count > 0 && !pools)
            return false;
        if (!w.writeU16(static_cast<uint16_t>(count)) || !w.writeU8(0) || !w.writeU8(0))
            return false;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (!w.writeBytes(pools[i].name, kDebugNameBytes) || !w.writeU64(pools[i].used) || !w.writeU64(pools[i].capacity) || !w.writeU32(pools[i].count) || !w.writeU32(0))
                return false;
        }
        return true;
    }

    bool readMemorySnapshotPayload(PacketReader& r, DebugMemoryPool* pools, uint32_t cap, uint32_t& count)
    {
        count = 0;
        uint16_t n  = 0;
        uint8_t  p0 = 0, p1 = 0;
        if (!r.readU16(n) || !r.readU8(p0) || !r.readU8(p1))
            return false;
        if (n > kDebugMaxPools)
            return false;
        if (n > cap || (n > 0 && !pools))
            return false;
        for (uint16_t i = 0; i < n; ++i)
        {
            uint32_t pad = 0;
            if (!r.readBytes(pools[i].name, kDebugNameBytes) || !r.readU64(pools[i].used) || !r.readU64(pools[i].capacity) || !r.readU32(pools[i].count) || !r.readU32(pad))
                return false;
            pools[i].name[kDebugNameBytes - 1] = 0;
        }
        count = n;
        return true;
    }

    bool writePerfSnapshotPayload(PacketWriter& w, const DebugPerfSnapshot& s)
    {
        if (!w.writeF32(s.dtMs) || !w.writeF32(s.fps) || !w.writeU32(s.drawCalls) || !w.writeU32(s.triangles))
            return false;
        for (uint32_t i = 0; i < kDebugPerfSlotCount; ++i)
        {
            if (!w.writeF32(s.phaseMs[i]))
                return false;
        }
        return w.writeU64(s.packetsIn) && w.writeU64(s.packetsOut) && w.writeF32(s.rttMs) && w.writeU32(0);
    }

    bool readPerfSnapshotPayload(PacketReader& r, DebugPerfSnapshot& s)
    {
        uint32_t pad = 0;
        if (!r.readF32(s.dtMs) || !r.readF32(s.fps) || !r.readU32(s.drawCalls) || !r.readU32(s.triangles))
            return false;
        for (uint32_t i = 0; i < kDebugPerfSlotCount; ++i)
        {
            if (!r.readF32(s.phaseMs[i]))
                return false;
        }
        return r.readU64(s.packetsIn) && r.readU64(s.packetsOut) && r.readF32(s.rttMs) && r.readU32(pad);
    }

    bool writeHeartbeatPayload(PacketWriter& w, uint32_t counter)
    {
        return w.writeU32(counter);
    }

    bool readHeartbeatPayload(PacketReader& r, uint32_t& counter)
    {
        return r.readU32(counter);
    }

    void DebugFramer::reset()
    {
        m_buf.clear();
        m_pos = 0;
        m_ok  = true;
    }

    bool DebugFramer::compact()
    {
        if (m_pos == 0)
            return true;
        if (m_pos >= m_buf.size())
        {
            m_buf.clear();
            m_pos = 0;
            return true;
        }
        m_buf.erase(m_buf.begin(), m_buf.begin() + static_cast<std::ptrdiff_t>(m_pos));
        m_pos = 0;
        return true;
    }

    bool DebugFramer::pushBytes(const uint8_t* data, uint32_t n)
    {
        if (!m_ok)
            return false;
        if (n == 0)
            return true;
        if (!data)
            return false;
        if (m_pos > 4096)
            compact();
        const size_t after = (m_buf.size() - m_pos) + n;
        if (after > static_cast<size_t>(kDebugHeaderBytes) + kDebugMaxPayload + 4096)
        {
            m_ok = false;
            return false;
        }
        m_buf.insert(m_buf.end(), data, data + n);
        return true;
    }

    bool DebugFramer::popMessage(DebugHeader& hdr, std::vector<uint8_t>& payload)
    {
        hdr     = {};
        payload.clear();
        if (!m_ok)
            return false;
        if (m_buf.size() < m_pos + kDebugHeaderBytes)
            return false;

        PacketReader r;
        if (!r.begin(m_buf.data() + m_pos, static_cast<uint32_t>(m_buf.size() - m_pos)))
            return false;

        uint8_t op = 0, flags = 0, reserved = 0;
        if (!r.readU32(hdr.magic) || !r.readU8(hdr.version) || !r.readU8(op) || !r.readU8(flags) || !r.readU8(reserved) || !r.readU32(hdr.payloadBytes))
            return false;

        if (hdr.magic != kDebugMagic || hdr.version != kDebugProtocolVersion || hdr.payloadBytes > kDebugMaxPayload)
        {
            m_ok = false;
            return false;
        }
        hdr.opcode = static_cast<DebugOpcode>(op);
        hdr.flags  = flags;

        const uint32_t need = kDebugHeaderBytes + hdr.payloadBytes;
        if (m_buf.size() < m_pos + need)
            return false;

        if (hdr.payloadBytes > 0)
        {
            payload.resize(hdr.payloadBytes);
            std::memcpy(payload.data(), m_buf.data() + m_pos + kDebugHeaderBytes, hdr.payloadBytes);
        }
        m_pos += need;
        if (m_pos > 8192)
            compact();
        return true;
    }

} // namespace Dark
