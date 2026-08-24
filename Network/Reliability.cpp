#include "Network/Reliability.h"
#include "Core/Log.h"

namespace Dark
{

    namespace
    {
        uint16_t nextId(uint16_t id)
        {
            ++id;
            if (id == 0)
                id = 1;
            return id;
        }

        uint16_t prevId(uint16_t id)
        {
            if (id <= 1)
                return 65535;
            return static_cast<uint16_t>(id - 1);
        }

        int16_t idDelta(uint16_t a, uint16_t b)
        {
            return static_cast<int16_t>(a - b);
        }

        // Prefix u16 id; the 24-byte header has no first-reliable-id field.
        uint32_t frameBytes(uint32_t payloadLen)
        {
            return 2u + 2u + 1u + payloadLen; // id + len + opcode + payload
        }
    } // namespace

    bool writeDatagramHeader(PacketWriter& w, const DatagramHeader& h)
    {
        return w.writeU32(h.magic) && w.writeU8(h.version) && w.writeU8(h.headerFlags) && w.writeU8(h.headerSize) && w.writeU8(h.reserved) && w.writeU16(h.seq) && w.writeU16(h.ack) &&
               w.writeU32(h.ackBits) && w.writeU32(h.connToken) && w.writeU16(h.reliableAck) && w.writeU8(h.reliableCount) && w.writeU8(h.pad);
    }

    bool readDatagramHeader(PacketReader& r, DatagramHeader& h)
    {
        return r.readU32(h.magic) && r.readU8(h.version) && r.readU8(h.headerFlags) && r.readU8(h.headerSize) && r.readU8(h.reserved) && r.readU16(h.seq) && r.readU16(h.ack) && r.readU32(h.ackBits) &&
               r.readU32(h.connToken) && r.readU16(h.reliableAck) && r.readU8(h.reliableCount) && r.readU8(h.pad);
    }

    void ReliabilityChannel::forceNextSeq(uint16_t seq)
    {
        if (seq == 0)
            seq = 1;
        m_outgoingSeq = static_cast<uint16_t>(seq - 1u);
    }

    void ReliabilityChannel::forceNextReliableId(uint16_t id)
    {
        m_reliableSendNext = (id == 0) ? 1 : id;
    }

    void ReliabilityChannel::forceReliableExpected(uint16_t id)
    {
        m_reliableExpected = (id == 0) ? 1 : id;
    }

    uint16_t ReliabilityChannel::allocSeq()
    {
        m_outgoingSeq = nextId(m_outgoingSeq);
        return m_outgoingSeq;
    }

    uint16_t ReliabilityChannel::advertisedReliableAck() const
    {
        if (!m_gotReliable)
            return 0;
        return prevId(m_reliableExpected);
    }

    void ReliabilityChannel::applyRemoteReliableAck(uint16_t reliableAck)
    {
        if (reliableAck == 0)
            return;
        while (!m_pending.empty())
        {
            if (idDelta(m_pending.front().id, reliableAck) > 0)
                break;
            const uint32_t n = frameBytes(static_cast<uint32_t>(m_pending.front().payload.size()));
            if (m_pendingBytes >= n)
                m_pendingBytes -= n;
            else
                m_pendingBytes = 0;
            m_pending.pop_front();
        }
    }

    void ReliabilityChannel::noteIncomingSeq(uint16_t seq)
    {
        if (seq == 0)
            return;
        if (m_incomingSeq == 0)
        {
            m_incomingSeq = seq;
            m_ackBits     = 0;
            return;
        }

        const int16_t delta = idDelta(seq, m_incomingSeq);
        if (delta == 0)
            return;

        if (delta > 0)
        {
            uint32_t shift  = 0;
            uint16_t cursor = seq;
            while (cursor != m_incomingSeq && shift < 33)
            {
                cursor = prevId(cursor);
                ++shift;
            }

            uint32_t newBits = 0;
            if (shift < 33 && cursor == m_incomingSeq)
            {
                if (shift < 32)
                    newBits = m_ackBits << shift;
                if (shift >= 1 && shift <= 32)
                    newBits |= (1u << (shift - 1));
            }
            m_ackBits     = newBits;
            m_incomingSeq = seq;
        }
        else
        {
            uint32_t bit    = 0;
            uint16_t cursor = m_incomingSeq;
            while (bit < 32)
            {
                cursor = prevId(cursor);
                if (cursor == seq)
                {
                    m_ackBits |= (1u << bit);
                    break;
                }
                ++bit;
            }
        }
    }

    bool ReliabilityChannel::queueReliable(uint8_t opcode, const void* payload, uint32_t len)
    {
        if (len > 0 && !payload)
            return false;
        if (len > static_cast<uint32_t>(kNetMaxReliableLen) - 1u)
        {
            DE_LOG_ERROR(LogCategory::Networking, "ReliabilityChannel: reliable payload {} exceeds max", len);
            return false;
        }

        const uint32_t bytes = frameBytes(len);
        if (m_pendingBytes + bytes > kNetPendingCapBytes)
        {
            m_pendingOverflow = true;
            DE_LOG_ERROR(LogCategory::Networking, "ReliabilityChannel: pending reliable cap {} exceeded", kNetPendingCapBytes);
            return false;
        }

        PendingReliable p;
        p.id           = m_reliableSendNext;
        p.lastSendTime = -1.f;
        p.opcode       = opcode;
        if (len)
            p.payload.assign(static_cast<const uint8_t*>(payload), static_cast<const uint8_t*>(payload) + len);

        m_reliableSendNext = nextId(m_reliableSendNext);
        m_pending.push_back(std::move(p));
        m_pendingBytes += bytes;
        return true;
    }

    bool ReliabilityChannel::setUnreliable(uint8_t opcode, const void* payload, uint32_t len)
    {
        if (len > 0 && !payload)
            return false;
        if (1u + len > kNetMaxPayload - kNetHeaderSize)
        {
            DE_LOG_ERROR(LogCategory::Networking, "ReliabilityChannel: unreliable payload {} will not fit", len);
            return false;
        }

        m_hasUnreliableOut    = true;
        m_unreliableOutOpcode = opcode;
        m_unreliableOut.clear();
        if (len)
            m_unreliableOut.assign(static_cast<const uint8_t*>(payload), static_cast<const uint8_t*>(payload) + len);
        return true;
    }

    bool ReliabilityChannel::flush(ITransport& transport, const Address& dest, float nowSeconds)
    {
        const bool     hasUnr   = m_hasUnreliableOut;
        const uint32_t unrBytes = hasUnr ? (1u + static_cast<uint32_t>(m_unreliableOut.size())) : 0u;

        size_t start = 0;
        if (!m_pending.empty())
        {
            const bool headDue = m_pending[0].lastSendTime < 0.f || (nowSeconds - m_pending[0].lastSendTime) >= kNetResendIntervalSec;
            if (!headDue)
            {
                while (start < m_pending.size() && m_pending[start].lastSendTime >= 0.f)
                    ++start;
            }
        }

        const bool haveReliables = start < m_pending.size();
        if (!hasUnr && !haveReliables && !m_ackDirty)
        {
            m_hasUnreliableOut    = false;
            m_unreliableOutOpcode = 0;
            m_unreliableOut.clear();
            return true;
        }

        bool     sentUnreliable = false;
        size_t   idx            = start;
        uint32_t flushBytes     = 0;
        bool     ok             = true;
        bool     first          = true;

        while (ok)
        {
            const bool     includeUnr = hasUnr && !sentUnreliable;
            const uint32_t reserve    = includeUnr ? unrBytes : 0u;

            if (!first && idx >= m_pending.size() && !includeUnr)
                break;

            uint8_t      buf[kNetMaxPayload];
            PacketWriter w;
            if (!w.begin(buf, kNetMaxPayload))
                return false;
            for (uint8_t i = 0; i < kNetHeaderSize; ++i)
            {
                if (!w.writeU8(0))
                    return false;
            }

            uint8_t count = 0;
            while (idx < m_pending.size() && count < 255)
            {
                PendingReliable& p    = m_pending[idx];
                const uint32_t   need = frameBytes(static_cast<uint32_t>(p.payload.size()));
                if (w.size() + need + reserve > kNetMaxPayload)
                    break;
                const uint16_t len = static_cast<uint16_t>(1u + p.payload.size());
                if (!w.writeU16(p.id) || !w.writeU16(len) || !w.writeU8(p.opcode))
                    return false;
                if (!p.payload.empty() && !w.writeBytes(p.payload.data(), static_cast<uint32_t>(p.payload.size())))
                    return false;
                if (p.lastSendTime >= 0.f)
                    ++m_resends;
                p.lastSendTime = nowSeconds;
                ++count;
                ++idx;
            }

            if (includeUnr)
            {
                if (!w.writeU8(m_unreliableOutOpcode))
                    return false;
                if (!m_unreliableOut.empty() && !w.writeBytes(m_unreliableOut.data(), static_cast<uint32_t>(m_unreliableOut.size())))
                    return false;
            }

            if (!first && count == 0 && !includeUnr)
                break;

            DatagramHeader h{};
            h.magic         = kNetMagic;
            h.version       = kNetProtocolVersion;
            h.headerFlags   = 0;
            h.headerSize    = kNetHeaderSize;
            h.reserved      = 0;
            h.seq           = allocSeq();
            h.ack           = m_incomingSeq;
            h.ackBits       = m_ackBits;
            h.connToken     = m_token;
            h.reliableAck   = advertisedReliableAck();
            h.reliableCount = count;
            h.pad           = 0;

            PacketWriter hw;
            if (!hw.begin(buf, kNetHeaderSize) || !writeDatagramHeader(hw, h))
                return false;

            const uint32_t dgSize = w.size();
            if (flushBytes + dgSize > kNetFlushByteCap)
                break;
            if (!transport.sendTo(dest, buf, dgSize))
            {
                ok = false;
                break;
            }

            flushBytes += dgSize;
            m_ackDirty     = false;
            sentUnreliable = sentUnreliable || includeUnr;
            first          = false;

            if (idx >= m_pending.size())
                break;
        }

        m_hasUnreliableOut    = false;
        m_unreliableOutOpcode = 0;
        m_unreliableOut.clear();
        return ok;
    }

    bool ReliabilityChannel::receive(const uint8_t* datagram, uint32_t size)
    {
        if (!datagram || size < kNetHeaderSize || size > kNetMaxPayload)
            return false;

        PacketReader r;
        if (!r.begin(datagram, size))
            return false;

        DatagramHeader h{};
        if (!readDatagramHeader(r, h))
            return false;
        if (h.magic != kNetMagic || h.version != kNetProtocolVersion)
            return false;
        if (h.headerSize < kNetHeaderSize || h.headerSize > size)
            return false;
        if (h.seq == 0)
            return false;

        const uint32_t extra = static_cast<uint32_t>(h.headerSize) - kNetHeaderSize;
        if (extra)
        {
            uint8_t skip[256];
            if (extra > sizeof(skip) || !r.readBytes(skip, extra))
                return false;
        }

        struct ParsedReliable
        {
            uint16_t             id     = 0;
            uint8_t              opcode = 0;
            std::vector<uint8_t> payload;
        };

        std::vector<ParsedReliable> msgs;
        msgs.reserve(h.reliableCount);
        for (uint8_t i = 0; i < h.reliableCount; ++i)
        {
            uint16_t id  = 0;
            uint16_t len = 0;
            uint8_t  op  = 0;
            if (!r.readU16(id) || !r.readU16(len) || !r.readU8(op))
                return false;
            if (len == 0 || len > kNetMaxReliableLen)
                return false;
            ParsedReliable m;
            m.id                      = id;
            m.opcode                  = op;
            const uint32_t payloadLen = static_cast<uint32_t>(len) - 1u;
            if (payloadLen)
            {
                m.payload.resize(payloadLen);
                if (!r.readBytes(m.payload.data(), payloadLen))
                    return false;
            }
            msgs.push_back(std::move(m));
        }

        const bool           hasUnr = r.remaining() > 0;
        uint8_t              unrOp  = 0;
        std::vector<uint8_t> unrPayload;
        if (hasUnr)
        {
            if (!r.readU8(unrOp))
                return false;
            const uint32_t n = r.remaining();
            if (n)
            {
                unrPayload.resize(n);
                if (!r.readBytes(unrPayload.data(), n))
                    return false;
            }
        }

        applyRemoteReliableAck(h.reliableAck);
        noteIncomingSeq(h.seq);
        m_ackDirty = true;

        bool stopReliables = false;
        for (ParsedReliable& m : msgs)
        {
            if (stopReliables)
                break;
            if (m.id == 0)
                continue;
            const int16_t d = idDelta(m.id, m_reliableExpected);
            if (d == 0)
            {
                m_gotReliable      = true;
                m_reliableExpected = nextId(m_reliableExpected);
                m_delivered.push_back(DeliveredReliable{ m.opcode, std::move(m.payload) });
            }
            else if (d > 0)
            {
                stopReliables = true;
            }
        }

        m_hasUnreliableIn    = hasUnr;
        m_unreliableInOpcode = hasUnr ? unrOp : 0;
        if (hasUnr)
            m_unreliableIn = std::move(unrPayload);
        else
            m_unreliableIn.clear();
        return true;
    }

    bool ReliabilityChannel::popReliable(uint8_t& opcode, std::vector<uint8_t>& payload)
    {
        if (m_delivered.empty())
            return false;
        opcode  = m_delivered.front().opcode;
        payload = std::move(m_delivered.front().payload);
        m_delivered.pop_front();
        return true;
    }

} // namespace Dark
