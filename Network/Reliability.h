#pragma once

#include "Network/Packet.h"
#include "Network/Transport.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace Dark
{

    constexpr uint32_t kNetMagic             = 0x314E4544u;
    constexpr uint8_t  kNetProtocolVersion   = 1;
    constexpr uint8_t  kNetHeaderSize        = 24;
    constexpr uint16_t kNetMaxReliableLen    = 512; // opcode + payload
    constexpr uint32_t kNetPendingCapBytes   = 16u * 1024u;
    constexpr float    kNetResendIntervalSec = 0.1f;
    constexpr uint32_t kNetFlushByteCap      = 64u * 1024u;

    struct DatagramHeader
    {
        uint32_t magic         = kNetMagic;
        uint8_t  version       = kNetProtocolVersion;
        uint8_t  headerFlags   = 0;
        uint8_t  headerSize    = kNetHeaderSize;
        uint8_t  reserved      = 0;
        uint16_t seq           = 0;
        uint16_t ack           = 0;
        uint32_t ackBits       = 0;
        uint32_t connToken     = 0;
        uint16_t reliableAck   = 0;
        uint8_t  reliableCount = 0;
        uint8_t  pad           = 0;
    };

    bool writeDatagramHeader(PacketWriter& w, const DatagramHeader& h);
    bool readDatagramHeader(PacketReader& r, DatagramHeader& h);

    // Per-connection go-back-N. Seq and reliable ids start at 1 and skip 0 on wrap.
    // ack / reliableAck of 0 means none received yet.
    class ReliabilityChannel
    {
    public:
        ReliabilityChannel() = default;

        ReliabilityChannel(const ReliabilityChannel&)            = delete;
        ReliabilityChannel& operator=(const ReliabilityChannel&) = delete;

        void setToken(uint32_t token) { m_token = token; }
        uint32_t token() const { return m_token; }

        bool queueReliable(uint8_t opcode, const void* payload, uint32_t len);
        bool setUnreliable(uint8_t opcode, const void* payload, uint32_t len);

        bool flush(ITransport& transport, const Address& dest, float nowSeconds);
        bool receive(const uint8_t* datagram, uint32_t size);

        bool popReliable(uint8_t& opcode, std::vector<uint8_t>& payload);

        bool hasUnreliable() const { return m_hasUnreliableIn; }
        uint8_t unreliableOpcode() const { return m_unreliableInOpcode; }
        const std::vector<uint8_t>& unreliablePayload() const { return m_unreliableIn; }

        bool pendingOverflow() const { return m_pendingOverflow; }
        uint32_t pendingCount() const { return static_cast<uint32_t>(m_pending.size()); }
        uint32_t pendingBytes() const { return m_pendingBytes; }

        // Test hooks: next values written / accepted (never 0).
        void forceNextSeq(uint16_t seq);
        void forceNextReliableId(uint16_t id);
        void forceReliableExpected(uint16_t id);

        uint16_t outgoingSeq() const { return m_outgoingSeq; }
        uint16_t reliableSendNext() const { return m_reliableSendNext; }
        uint16_t reliableExpected() const { return m_reliableExpected; }

    private:
        struct PendingReliable
        {
            uint16_t             id           = 0;
            float                lastSendTime = -1.f;
            uint8_t              opcode       = 0;
            std::vector<uint8_t> payload;
        };

        struct DeliveredReliable
        {
            uint8_t              opcode = 0;
            std::vector<uint8_t> payload;
        };

        uint16_t allocSeq();
        uint16_t advertisedReliableAck() const;
        void     applyRemoteReliableAck(uint16_t reliableAck);
        void     noteIncomingSeq(uint16_t seq);

        uint32_t m_token            = 0;
        uint16_t m_outgoingSeq      = 0; // last sent; 0 = none
        uint16_t m_incomingSeq      = 0; // 0 = none received
        uint32_t m_ackBits          = 0;
        uint16_t m_reliableSendNext = 1;
        uint16_t m_reliableExpected = 1;
        bool     m_gotReliable      = false;
        bool     m_pendingOverflow  = false;
        bool     m_ackDirty         = false;
        uint32_t m_pendingBytes     = 0;
        uint32_t m_resends          = 0;

        std::deque<PendingReliable>   m_pending;
        std::deque<DeliveredReliable> m_delivered;

        bool                 m_hasUnreliableOut    = false;
        uint8_t              m_unreliableOutOpcode = 0;
        std::vector<uint8_t> m_unreliableOut;

        bool                 m_hasUnreliableIn    = false;
        uint8_t              m_unreliableInOpcode = 0;
        std::vector<uint8_t> m_unreliableIn;
    };

} // namespace Dark
