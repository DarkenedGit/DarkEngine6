#include <gtest/gtest.h>

#include "Network/FakeTransport.h"
#include "Network/Packet.h"
#include "Network/Reliability.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace Dark;

namespace
{
    Address makeAddr(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port)
    {
        Address addr;
        addr.ipv4 = (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | uint32_t(d);
        addr.port = port;
        return addr;
    }

    void recvAll(FakeTransport& t, ReliabilityChannel& ch)
    {
        Address  src{};
        uint8_t  buf[kNetMaxPayload]{};
        uint32_t n = 0;
        while (t.recvFrom(src, buf, sizeof(buf), n))
        {
            if (n > 0)
                ch.receive(buf, n);
        }
    }

    bool parseHeader(const uint8_t* data, uint32_t size, DatagramHeader& h, PacketReader& r)
    {
        if (!r.begin(data, size))
            return false;
        return readDatagramHeader(r, h);
    }

    bool skipReliables(PacketReader& r, uint8_t count)
    {
        for (uint8_t i = 0; i < count; ++i)
        {
            uint16_t id  = 0;
            uint16_t len = 0;
            uint8_t  op  = 0;
            if (!r.readU16(id) || !r.readU16(len) || !r.readU8(op))
                return false;
            if (len == 0 || len > kNetMaxReliableLen)
                return false;
            const uint32_t payloadLen = static_cast<uint32_t>(len) - 1u;
            if (payloadLen)
            {
                uint8_t skip[kNetMaxReliableLen];
                if (!r.readBytes(skip, payloadLen))
                    return false;
            }
        }
        return true;
    }

    class FailTransport : public ITransport
    {
    public:
        bool sendTo(const Address&, const void*, uint32_t) override { return false; }
        bool recvFrom(Address&, void*, uint32_t, uint32_t& outSize) override
        {
            outSize = 0;
            return false;
        }
        Address localAddress() const override { return {}; }
        void close() override {}
    };
} // namespace

TEST(Reliability, HeaderRoundTrip)
{
    DatagramHeader in{};
    in.magic         = kNetMagic;
    in.version       = kNetProtocolVersion;
    in.headerFlags   = 0;
    in.headerSize    = kNetHeaderSize;
    in.reserved      = 0;
    in.seq           = 1;
    in.ack           = 0;
    in.ackBits       = 0xA5A5A5A5u;
    in.connToken     = 0x11223344u;
    in.reliableAck   = 0;
    in.reliableCount = 2;
    in.pad           = 0;

    uint8_t      buf[kNetHeaderSize]{};
    PacketWriter w;
    ASSERT_TRUE(w.begin(buf, sizeof(buf)));
    ASSERT_TRUE(writeDatagramHeader(w, in));
    EXPECT_EQ(w.size(), 24u);

    EXPECT_EQ(buf[0], 0x44);
    EXPECT_EQ(buf[1], 0x45);
    EXPECT_EQ(buf[2], 0x4E);
    EXPECT_EQ(buf[3], 0x31);
    EXPECT_EQ(buf[4], kNetProtocolVersion);
    EXPECT_EQ(buf[5], 0);
    EXPECT_EQ(buf[6], kNetHeaderSize);
    EXPECT_EQ(buf[7], 0);
    EXPECT_EQ(buf[8], 1);
    EXPECT_EQ(buf[9], 0);
    EXPECT_EQ(buf[10], 0);
    EXPECT_EQ(buf[11], 0);

    PacketReader r;
    ASSERT_TRUE(r.begin(buf, w.size()));
    DatagramHeader out{};
    ASSERT_TRUE(readDatagramHeader(r, out));
    EXPECT_EQ(out.magic, in.magic);
    EXPECT_EQ(out.version, in.version);
    EXPECT_EQ(out.headerFlags, in.headerFlags);
    EXPECT_EQ(out.headerSize, in.headerSize);
    EXPECT_EQ(out.reserved, in.reserved);
    EXPECT_EQ(out.seq, in.seq);
    EXPECT_EQ(out.ack, in.ack);
    EXPECT_EQ(out.ackBits, in.ackBits);
    EXPECT_EQ(out.connToken, in.connToken);
    EXPECT_EQ(out.reliableAck, in.reliableAck);
    EXPECT_EQ(out.reliableCount, in.reliableCount);
    EXPECT_EQ(out.pad, in.pad);
    EXPECT_EQ(r.remaining(), 0u);
}

TEST(Reliability, DropFirstReliableResentInOrder)
{
    FakeHub            hub;
    const Address      addrA = makeAddr(10, 0, 0, 1, 26160);
    const Address      addrB = makeAddr(10, 0, 0, 2, 26161);
    FakeTransport      ta(hub, addrA);
    FakeTransport      tb(hub, addrB);
    ReliabilityChannel a;
    ReliabilityChannel b;

    const uint8_t p1[] = { 10 };
    const uint8_t p2[] = { 20 };
    const uint8_t p3[] = { 30 };
    ASSERT_TRUE(a.queueReliable(6, p1, 1));

    hub.dropNext(1);
    ASSERT_TRUE(a.flush(ta, addrB, 0.0f));
    recvAll(tb, b);

    uint8_t              op = 0;
    std::vector<uint8_t> pl;
    EXPECT_FALSE(b.popReliable(op, pl));
    EXPECT_EQ(a.pendingCount(), 1u);

    // Peer's reliableAck=0 must not mean "all acked".
    ASSERT_TRUE(b.setUnreliable(5, nullptr, 0));
    ASSERT_TRUE(b.flush(tb, addrA, 0.0f));
    recvAll(ta, a);
    EXPECT_EQ(a.pendingCount(), 1u);

    ASSERT_TRUE(a.queueReliable(6, p2, 1));
    ASSERT_TRUE(a.queueReliable(6, p3, 1));
    ASSERT_TRUE(a.flush(ta, addrB, 0.0f));
    recvAll(tb, b);
    EXPECT_FALSE(b.popReliable(op, pl));

    ASSERT_TRUE(a.flush(ta, addrB, kNetResendIntervalSec));
    recvAll(tb, b);

    ASSERT_TRUE(b.popReliable(op, pl));
    EXPECT_EQ(op, 6);
    ASSERT_EQ(pl.size(), 1u);
    EXPECT_EQ(pl[0], 10);

    ASSERT_TRUE(b.popReliable(op, pl));
    ASSERT_EQ(pl.size(), 1u);
    EXPECT_EQ(pl[0], 20);

    ASSERT_TRUE(b.popReliable(op, pl));
    ASSERT_EQ(pl.size(), 1u);
    EXPECT_EQ(pl[0], 30);
    EXPECT_FALSE(b.popReliable(op, pl));
}

TEST(Reliability, WrapSkipsZero)
{
    FakeHub            hub;
    const Address      addrA = makeAddr(10, 0, 0, 1, 1);
    const Address      addrB = makeAddr(10, 0, 0, 2, 2);
    FakeTransport      ta(hub, addrA);
    FakeTransport      tb(hub, addrB);
    ReliabilityChannel a;
    ReliabilityChannel b;

    a.forceNextSeq(65535);
    a.forceNextReliableId(65535);
    b.forceReliableExpected(65535);

    const uint8_t first[]  = { 7 };
    const uint8_t second[] = { 8 };
    ASSERT_TRUE(a.queueReliable(6, first, 1));
    ASSERT_TRUE(a.flush(ta, addrB, 0.0f));
    ASSERT_TRUE(a.queueReliable(6, second, 1));
    ASSERT_TRUE(a.flush(ta, addrB, 0.0f));

    Address  src{};
    uint8_t  buf[kNetMaxPayload]{};
    uint32_t n = 0;

    ASSERT_TRUE(tb.recvFrom(src, buf, sizeof(buf), n));
    {
        PacketReader   r;
        DatagramHeader h{};
        ASSERT_TRUE(parseHeader(buf, n, h, r));
        EXPECT_EQ(h.seq, 65535);
        EXPECT_NE(h.seq, 0);
        EXPECT_EQ(h.ack, 0); // none received yet
        EXPECT_EQ(h.reliableAck, 0);
        EXPECT_EQ(h.reliableCount, 1);
        uint16_t id = 0;
        ASSERT_TRUE(r.readU16(id));
        EXPECT_EQ(id, 65535);
        EXPECT_NE(id, 0);
    }
    ASSERT_TRUE(b.receive(buf, n));

    // After 65535, reliableAck must be 65535 — wrap must not look like "none".
    ASSERT_TRUE(b.setUnreliable(5, nullptr, 0));
    ASSERT_TRUE(b.flush(tb, addrA, 0.0f));
    ASSERT_TRUE(ta.recvFrom(src, buf, sizeof(buf), n));
    {
        PacketReader   r;
        DatagramHeader h{};
        ASSERT_TRUE(parseHeader(buf, n, h, r));
        EXPECT_NE(h.seq, 0);
        EXPECT_EQ(h.reliableAck, 65535);
        EXPECT_NE(h.reliableAck, 0);
    }
    ASSERT_TRUE(a.receive(buf, n));
    EXPECT_EQ(a.pendingCount(), 1u);
    EXPECT_EQ(a.pendingIdAt(0), 1);
    EXPECT_NE(a.pendingIdAt(0), 65535);

    ASSERT_TRUE(tb.recvFrom(src, buf, sizeof(buf), n));
    {
        PacketReader   r;
        DatagramHeader h{};
        ASSERT_TRUE(parseHeader(buf, n, h, r));
        EXPECT_EQ(h.seq, 1);
        EXPECT_NE(h.seq, 0);
        EXPECT_EQ(h.ack, 0); // still none — wrap must not emit seq 0
        uint16_t id = 0;
        ASSERT_TRUE(r.readU16(id));
        EXPECT_EQ(id, 1);
        EXPECT_NE(id, 0);
    }
    ASSERT_TRUE(b.receive(buf, n));
    EXPECT_EQ(a.pendingCount(), 1u);
    EXPECT_EQ(a.pendingIdAt(0), 1);

    ASSERT_TRUE(b.flush(tb, addrA, 0.0f));
    ASSERT_TRUE(ta.recvFrom(src, buf, sizeof(buf), n));
    ASSERT_TRUE(a.receive(buf, n));
    EXPECT_EQ(a.pendingCount(), 0u);

    uint8_t              op = 0;
    std::vector<uint8_t> pl;
    ASSERT_TRUE(b.popReliable(op, pl));
    ASSERT_EQ(pl.size(), 1u);
    EXPECT_EQ(pl[0], 7);
    ASSERT_TRUE(b.popReliable(op, pl));
    ASSERT_EQ(pl.size(), 1u);
    EXPECT_EQ(pl[0], 8);
    EXPECT_FALSE(b.popReliable(op, pl));
}

TEST(Reliability, EveryOtherDatagramDroppedInOrder)
{
    FakeHub            hub;
    const Address      addrA = makeAddr(10, 0, 0, 1, 1);
    const Address      addrB = makeAddr(10, 0, 0, 2, 2);
    FakeTransport      ta(hub, addrA);
    FakeTransport      tb(hub, addrB);
    ReliabilityChannel a;
    ReliabilityChannel b;

    constexpr uint32_t kCount   = 8;
    constexpr uint32_t kPayload = 400;
    for (uint32_t i = 0; i < kCount; ++i)
    {
        std::vector<uint8_t> msg(kPayload, static_cast<uint8_t>(i + 1));
        ASSERT_TRUE(a.queueReliable(6, msg.data(), kPayload));
    }

    std::vector<uint8_t> delivered;
    auto                 popAll = [&]()
    {
        uint8_t              op = 0;
        std::vector<uint8_t> pl;
        while (b.popReliable(op, pl))
            delivered.push_back(pl.empty() ? 0 : pl[0]);
    };

    float now  = 0.0f;
    auto  pump = [&]() -> bool
    {
        if (!a.flush(ta, addrB, now))
            return false;
        Address  src{};
        uint8_t  buf[kNetMaxPayload]{};
        uint32_t n     = 0;
        uint32_t index = 0;
        while (tb.recvFrom(src, buf, sizeof(buf), n))
        {
            const bool drop = (index % 2) == 1;
            ++index;
            if (!drop && n > 0)
                b.receive(buf, n);
        }
        popAll();
        if (!b.flush(tb, addrA, now))
            return false;
        recvAll(ta, a);
        return true;
    };

    for (int i = 0; i < 24 && delivered.size() < kCount; ++i)
    {
        ASSERT_TRUE(pump());
        now += kNetResendIntervalSec;
    }

    ASSERT_EQ(delivered.size(), kCount);
    for (uint32_t i = 0; i < kCount; ++i)
        EXPECT_EQ(delivered[i], static_cast<uint8_t>(i + 1));
}

TEST(Reliability, UnknownMagicAndVersionDropped)
{
    FakeHub            hub;
    const Address      addrA = makeAddr(10, 0, 0, 1, 1);
    const Address      addrB = makeAddr(10, 0, 0, 2, 2);
    FakeTransport      ta(hub, addrA);
    FakeTransport      tb(hub, addrB);
    ReliabilityChannel a;
    ReliabilityChannel b;

    const uint8_t payload[] = { 1 };
    ASSERT_TRUE(a.queueReliable(6, payload, 1));
    ASSERT_TRUE(a.flush(ta, addrB, 0.0f));

    Address  src{};
    uint8_t  buf[kNetMaxPayload]{};
    uint32_t n = 0;
    ASSERT_TRUE(tb.recvFrom(src, buf, sizeof(buf), n));
    ASSERT_GE(n, 8u);

    uint8_t badMagic[kNetMaxPayload];
    std::memcpy(badMagic, buf, n);
    badMagic[0] ^= 0xFF;
    EXPECT_FALSE(b.receive(badMagic, n));

    uint8_t badVer[kNetMaxPayload];
    std::memcpy(badVer, buf, n);
    badVer[4] = 99;
    EXPECT_FALSE(b.receive(badVer, n));

    uint8_t badSize[kNetMaxPayload];
    const uint8_t sizes[] = { 0, 23, static_cast<uint8_t>(n + 1 > 255 ? 255 : n + 1) };
    for (uint8_t s : sizes)
    {
        std::memcpy(badSize, buf, n);
        badSize[6] = s;
        EXPECT_FALSE(b.receive(badSize, n)) << "headerSize=" << static_cast<int>(s);
    }

    EXPECT_TRUE(b.receive(buf, n));
    uint8_t              op = 0;
    std::vector<uint8_t> pl;
    ASSERT_TRUE(b.popReliable(op, pl));
}

TEST(Reliability, PackingReservesUnreliableTail)
{
    FakeHub            hub;
    const Address      addrA = makeAddr(10, 0, 0, 1, 1);
    const Address      addrB = makeAddr(10, 0, 0, 2, 2);
    FakeTransport      ta(hub, addrA);
    FakeTransport      tb(hub, addrB);
    ReliabilityChannel a;

    constexpr uint32_t kRelLen  = 400;
    constexpr uint32_t kUnrLen  = 800;
    constexpr uint8_t  kUnrOp   = 8;
    std::vector<uint8_t> rel(kRelLen, 0x11);
    std::vector<uint8_t> unr(kUnrLen, 0x22);
    for (int i = 0; i < 4; ++i)
        ASSERT_TRUE(a.queueReliable(6, rel.data(), kRelLen));
    ASSERT_TRUE(a.setUnreliable(kUnrOp, unr.data(), kUnrLen));
    ASSERT_TRUE(a.flush(ta, addrB, 0.0f));

    std::vector<std::vector<uint8_t>> dgs;
    Address  src{};
    uint8_t  buf[kNetMaxPayload]{};
    uint32_t n = 0;
    while (tb.recvFrom(src, buf, sizeof(buf), n))
    {
        ASSERT_GT(n, 0u);
        dgs.emplace_back(buf, buf + n);
    }
    ASSERT_GE(dgs.size(), 2u);

    {
        PacketReader   r;
        DatagramHeader h{};
        ASSERT_TRUE(parseHeader(dgs[0].data(), static_cast<uint32_t>(dgs[0].size()), h, r));
        ASSERT_TRUE(skipReliables(r, h.reliableCount));
        ASSERT_GT(r.remaining(), 0u);
        uint8_t op = 0;
        ASSERT_TRUE(r.readU8(op));
        EXPECT_EQ(op, kUnrOp);
        EXPECT_EQ(r.remaining(), kUnrLen);
        std::vector<uint8_t> tail(kUnrLen);
        ASSERT_TRUE(r.readBytes(tail.data(), kUnrLen));
        EXPECT_EQ(tail, unr);
    }

    for (size_t i = 1; i < dgs.size(); ++i)
    {
        PacketReader   r;
        DatagramHeader h{};
        ASSERT_TRUE(parseHeader(dgs[i].data(), static_cast<uint32_t>(dgs[i].size()), h, r));
        EXPECT_GT(h.reliableCount, 0);
        ASSERT_TRUE(skipReliables(r, h.reliableCount));
        EXPECT_EQ(r.remaining(), 0u);
    }
}

TEST(Reliability, FailedSendDoesNotMarkPendingSent)
{
    FailTransport      fail;
    ReliabilityChannel a;
    const Address      dest = makeAddr(10, 0, 0, 2, 2);
    const uint8_t      payload[] = { 1 };
    ASSERT_TRUE(a.queueReliable(6, payload, 1));
    ASSERT_TRUE(a.setUnreliable(8, payload, 1));
    EXPECT_FALSE(a.flush(fail, dest, 0.0f));
    EXPECT_EQ(a.outgoingSeq(), 0);
    EXPECT_EQ(a.pendingCount(), 1u);
    EXPECT_EQ(a.pendingIdAt(0), 1);

    FakeHub            hub;
    const Address      addrA = makeAddr(10, 0, 0, 1, 1);
    FakeTransport      ta(hub, addrA);
    FakeTransport      tb(hub, dest);
    ReliabilityChannel b;
    ASSERT_TRUE(a.flush(ta, dest, 0.0f));
    recvAll(tb, b);
    uint8_t              op = 0;
    std::vector<uint8_t> pl;
    ASSERT_TRUE(b.popReliable(op, pl));
    EXPECT_TRUE(b.hasUnreliable());
    EXPECT_EQ(b.unreliableOpcode(), 8);
}

TEST(Reliability, PendingCapFailsQueue)
{
    ReliabilityChannel   ch;
    std::vector<uint8_t> blob(kNetMaxReliableLen - 1u, 1);
    uint32_t             queued = 0;
    while (ch.queueReliable(6, blob.data(), static_cast<uint32_t>(blob.size())))
        ++queued;
    EXPECT_GT(queued, 0u);
    EXPECT_TRUE(ch.pendingOverflow());
    EXPECT_FALSE(ch.queueReliable(6, blob.data(), static_cast<uint32_t>(blob.size())));
}
