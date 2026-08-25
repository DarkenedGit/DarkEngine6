#include <gtest/gtest.h>

#include "Debug/DebugProtocol.h"
#include "Network/FakeTcp.h"

using namespace Dark;

TEST(DebugProtocol, FrameRoundTrip)
{
    uint8_t payload[] = {1, 2, 3, 4};
    uint8_t buf[64]{};
    uint32_t size = 0;
    ASSERT_TRUE(writeDebugFrame(buf, sizeof(buf), DebugOpcode::Heartbeat, payload, sizeof(payload), size));
    EXPECT_EQ(size, kDebugHeaderBytes + sizeof(payload));

    DebugFramer framer;
    ASSERT_TRUE(framer.pushBytes(buf, size));
    DebugHeader hdr{};
    std::vector<uint8_t> out;
    ASSERT_TRUE(framer.popMessage(hdr, out));
    EXPECT_EQ(hdr.magic, kDebugMagic);
    EXPECT_EQ(hdr.version, kDebugProtocolVersion);
    EXPECT_EQ(hdr.opcode, DebugOpcode::Heartbeat);
    ASSERT_EQ(out.size(), sizeof(payload));
    EXPECT_EQ(out[0], 1);
    EXPECT_EQ(out[3], 4);
    EXPECT_FALSE(framer.popMessage(hdr, out));
}

TEST(DebugProtocol, SplitAcrossPushes)
{
    uint8_t payload[8]{9, 8, 7, 6, 5, 4, 3, 2};
    uint8_t buf[64]{};
    uint32_t size = 0;
    ASSERT_TRUE(writeDebugFrame(buf, sizeof(buf), DebugOpcode::Hello, payload, sizeof(payload), size));

    DebugFramer framer;
    ASSERT_TRUE(framer.pushBytes(buf, 5));
    DebugHeader hdr{};
    std::vector<uint8_t> out;
    EXPECT_FALSE(framer.popMessage(hdr, out));
    ASSERT_TRUE(framer.pushBytes(buf + 5, size - 5));
    ASSERT_TRUE(framer.popMessage(hdr, out));
    EXPECT_EQ(hdr.opcode, DebugOpcode::Hello);
    ASSERT_EQ(out.size(), sizeof(payload));
    EXPECT_EQ(out[0], 9);
}

TEST(DebugProtocol, BadMagicFailsFramer)
{
    uint8_t buf[32]{};
    uint32_t size = 0;
    ASSERT_TRUE(writeDebugFrame(buf, sizeof(buf), DebugOpcode::Heartbeat, nullptr, 0, size));
    buf[0] ^= 0xFF;

    DebugFramer framer;
    ASSERT_TRUE(framer.pushBytes(buf, size));
    DebugHeader hdr{};
    std::vector<uint8_t> out;
    EXPECT_FALSE(framer.popMessage(hdr, out));
    EXPECT_FALSE(framer.ok());
}

TEST(DebugProtocol, OversizePayloadFails)
{
    uint8_t buf[32]{};
    PacketWriter w;
    ASSERT_TRUE(w.begin(buf, sizeof(buf)));
    ASSERT_TRUE(w.writeU32(kDebugMagic));
    ASSERT_TRUE(w.writeU8(kDebugProtocolVersion));
    ASSERT_TRUE(w.writeU8(static_cast<uint8_t>(DebugOpcode::Hello)));
    ASSERT_TRUE(w.writeU8(0));
    ASSERT_TRUE(w.writeU8(0));
    ASSERT_TRUE(w.writeU32(kDebugMaxPayload + 1));

    DebugFramer framer;
    ASSERT_TRUE(framer.pushBytes(buf, w.size()));
    DebugHeader hdr{};
    std::vector<uint8_t> out;
    EXPECT_FALSE(framer.popMessage(hdr, out));
    EXPECT_FALSE(framer.ok());
}

TEST(DebugProtocol, HelloPayload)
{
    DebugHello in{};
    copyDebugName(in.name, "VisualDebugger");
    in.pid      = 42;
    in.channels = DebugChannelAll;

    uint8_t payload[64]{};
    PacketWriter w;
    ASSERT_TRUE(w.begin(payload, sizeof(payload)));
    ASSERT_TRUE(writeHelloPayload(w, in));

    PacketReader r;
    ASSERT_TRUE(r.begin(payload, w.size()));
    DebugHello out{};
    ASSERT_TRUE(readHelloPayload(r, out));
    EXPECT_STREQ(out.name, "VisualDebugger");
    EXPECT_EQ(out.pid, 42u);
    EXPECT_EQ(out.channels, DebugChannelAll);
}

TEST(DebugProtocol, LogLinePayload)
{
    DebugLogEntry in{};
    in.timestampMs = 123456789ull;
    in.level       = LogLevel::Warn;
    in.category    = LogCategory::Render;
    in.dropped     = 3;
    copyDebugName(in.text, "hello pool");

    uint8_t payload[600]{};
    PacketWriter w;
    ASSERT_TRUE(w.begin(payload, sizeof(payload)));
    ASSERT_TRUE(writeLogLinePayload(w, in));

    PacketReader r;
    ASSERT_TRUE(r.begin(payload, w.size()));
    DebugLogEntry out{};
    ASSERT_TRUE(readLogLinePayload(r, out));
    EXPECT_EQ(out.timestampMs, 123456789ull);
    EXPECT_EQ(out.level, LogLevel::Warn);
    EXPECT_EQ(out.category, LogCategory::Render);
    EXPECT_EQ(out.dropped, 3);
    EXPECT_STREQ(out.text, "hello pool");
}

TEST(DebugProtocol, MemoryAndPerfPayloads)
{
    DebugMemoryPool pools[2]{};
    copyDebugName(pools[0].name, "Transform");
    pools[0].used     = 128;
    pools[0].capacity = 256;
    pools[0].count    = 4;
    copyDebugName(pools[1].name, "Process/Private");
    pools[1].used     = 1024;
    pools[1].capacity = 1024;
    pools[1].count    = 1;

    uint8_t buf[512]{};
    PacketWriter w;
    ASSERT_TRUE(w.begin(buf, sizeof(buf)));
    ASSERT_TRUE(writeMemorySnapshotPayload(w, pools, 2));
    PacketReader r;
    ASSERT_TRUE(r.begin(buf, w.size()));
    DebugMemoryPool outPools[4]{};
    uint32_t        count = 0;
    ASSERT_TRUE(readMemorySnapshotPayload(r, outPools, 4, count));
    ASSERT_EQ(count, 2u);
    EXPECT_STREQ(outPools[0].name, "Transform");
    EXPECT_EQ(outPools[0].count, 4u);
    EXPECT_STREQ(outPools[1].name, "Process/Private");

    DebugPerfSnapshot perf{};
    perf.dtMs      = 16.6f;
    perf.fps       = 60.0f;
    perf.drawCalls = 12;
    perf.phaseMs[0] = 16.0f;
    perf.packetsIn  = 9;
    uint8_t pbuf[256]{};
    PacketWriter pw;
    ASSERT_TRUE(pw.begin(pbuf, sizeof(pbuf)));
    ASSERT_TRUE(writePerfSnapshotPayload(pw, perf));
    PacketReader pr;
    ASSERT_TRUE(pr.begin(pbuf, pw.size()));
    DebugPerfSnapshot pout{};
    ASSERT_TRUE(readPerfSnapshotPayload(pr, pout));
    EXPECT_NEAR(pout.dtMs, 16.6f, 0.01f);
    EXPECT_EQ(pout.drawCalls, 12u);
    EXPECT_EQ(pout.packetsIn, 9u);
}

TEST(FakeTcp, EchoFramedMessage)
{
    FakeTcpHub      hub;
    FakeTcpListener listener(hub);
    ASSERT_TRUE(listener.listen(26162));

    FakeTcpStream client;
    ASSERT_TRUE(hub.connectClient(client, 26162));

    std::unique_ptr<IByteStream> accepted;
    ASSERT_TRUE(listener.tryAccept(accepted));
    ASSERT_TRUE(accepted);

    uint8_t  frame[64]{};
    uint32_t frameSize = 0;
    uint8_t  payload[] = {42, 43};
    ASSERT_TRUE(writeDebugFrame(frame, sizeof(frame), DebugOpcode::Heartbeat, payload, sizeof(payload), frameSize));

    uint32_t written = 0;
    ASSERT_TRUE(client.write(frame, frameSize, written));
    EXPECT_EQ(written, frameSize);

    uint8_t  recvBuf[64]{};
    uint32_t got = 0;
    ASSERT_TRUE(accepted->read(recvBuf, sizeof(recvBuf), got));
    EXPECT_EQ(got, frameSize);

    DebugFramer framer;
    ASSERT_TRUE(framer.pushBytes(recvBuf, got));
    DebugHeader hdr{};
    std::vector<uint8_t> out;
    ASSERT_TRUE(framer.popMessage(hdr, out));
    EXPECT_EQ(hdr.opcode, DebugOpcode::Heartbeat);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0], 42);
}
