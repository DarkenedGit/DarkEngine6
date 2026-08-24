#include <gtest/gtest.h>

#include "Network/FakeTransport.h"
#include "Network/Transport.h"

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
}

TEST(FakeTransport, EchoAToB)
{
    FakeHub hub;
    const Address addrA = makeAddr(10, 0, 0, 1, 26160);
    const Address addrB = makeAddr(10, 0, 0, 2, 26161);
    FakeTransport a(hub, addrA);
    FakeTransport b(hub, addrB);

    EXPECT_EQ(a.localAddress(), addrA);
    EXPECT_EQ(b.localAddress(), addrB);

    const uint8_t msg[] = {9, 8, 7};
    ASSERT_TRUE(a.sendTo(addrB, msg, sizeof(msg)));

    Address  src{};
    uint8_t  buf[16]{};
    uint32_t n = 0;
    ASSERT_TRUE(b.recvFrom(src, buf, sizeof(buf), n));
    EXPECT_EQ(n, sizeof(msg));
    EXPECT_EQ(src, addrA);
    EXPECT_EQ(buf[0], 9);
    EXPECT_EQ(buf[1], 8);
    EXPECT_EQ(buf[2], 7);
    EXPECT_FALSE(b.recvFrom(src, buf, sizeof(buf), n));
    EXPECT_FALSE(a.recvFrom(src, buf, sizeof(buf), n));
}

TEST(FakeTransport, DropNextSkipsThenDelivers)
{
    FakeHub hub;
    const Address addrA = makeAddr(10, 0, 0, 1, 1);
    const Address addrB = makeAddr(10, 0, 0, 2, 2);
    FakeTransport a(hub, addrA);
    FakeTransport b(hub, addrB);

    hub.dropNext(2);
    const uint8_t one[] = {1};
    const uint8_t two[] = {2};
    const uint8_t three[] = {3};
    ASSERT_TRUE(a.sendTo(addrB, one, 1));
    ASSERT_TRUE(a.sendTo(addrB, two, 1));
    ASSERT_TRUE(a.sendTo(addrB, three, 1));

    Address  src{};
    uint8_t  buf[8]{};
    uint32_t n = 0;
    ASSERT_TRUE(b.recvFrom(src, buf, sizeof(buf), n));
    EXPECT_EQ(n, 1u);
    EXPECT_EQ(buf[0], 3);
    EXPECT_FALSE(b.recvFrom(src, buf, sizeof(buf), n));
}

TEST(FakeTransport, DropRateOneDropsAll)
{
    FakeHub hub;
    const Address addrA = makeAddr(10, 0, 0, 1, 1);
    const Address addrB = makeAddr(10, 0, 0, 2, 2);
    FakeTransport a(hub, addrA);
    FakeTransport b(hub, addrB);

    hub.setDropRate(1.0f);
    const uint8_t msg[] = {1};
    ASSERT_TRUE(a.sendTo(addrB, msg, 1));
    Address  src{};
    uint8_t  buf[8]{};
    uint32_t n = 0;
    EXPECT_FALSE(b.recvFrom(src, buf, sizeof(buf), n));
}

TEST(FakeTransport, BroadcastCopiesToAllExceptSrc)
{
    FakeHub hub;
    const Address addrA = makeAddr(10, 0, 0, 1, 26160);
    const Address addrB = makeAddr(10, 0, 0, 2, 26160);
    const Address addrC = makeAddr(10, 0, 0, 3, 26161);
    FakeTransport a(hub, addrA);
    FakeTransport b(hub, addrB);
    FakeTransport c(hub, addrC);

    const Address bcast = makeAddr(255, 255, 255, 255, 26161);
    const uint8_t msg[] = {42};
    ASSERT_TRUE(a.sendTo(bcast, msg, 1));

    Address  src{};
    uint8_t  buf[8]{};
    uint32_t n = 0;
    EXPECT_FALSE(a.recvFrom(src, buf, sizeof(buf), n));

    ASSERT_TRUE(b.recvFrom(src, buf, sizeof(buf), n));
    EXPECT_EQ(n, 1u);
    EXPECT_EQ(buf[0], 42);
    EXPECT_EQ(src, addrA);

    ASSERT_TRUE(c.recvFrom(src, buf, sizeof(buf), n));
    EXPECT_EQ(n, 1u);
    EXPECT_EQ(buf[0], 42);
    EXPECT_EQ(src, addrA);
}

TEST(FakeTransport, RejectsOversizeSend)
{
    FakeHub hub;
    const Address addrA = makeAddr(10, 0, 0, 1, 1);
    const Address addrB = makeAddr(10, 0, 0, 2, 2);
    FakeTransport a(hub, addrA);
    FakeTransport b(hub, addrB);

    uint8_t big[kNetMaxPayload + 1]{};
    EXPECT_FALSE(a.sendTo(addrB, big, sizeof(big)));

    Address  src{};
    uint8_t  buf[8]{};
    uint32_t n = 0;
    EXPECT_FALSE(b.recvFrom(src, buf, sizeof(buf), n));
}

TEST(FakeTransport, CloseUnregisters)
{
    FakeHub hub;
    const Address addrA = makeAddr(10, 0, 0, 1, 1);
    const Address addrB = makeAddr(10, 0, 0, 2, 2);
    FakeTransport a(hub, addrA);
    FakeTransport b(hub, addrB);
    b.close();

    const uint8_t msg[] = {1};
    EXPECT_TRUE(a.sendTo(addrB, msg, 1));
    EXPECT_FALSE(b.sendTo(addrA, msg, 1));

    Address  src{};
    uint8_t  buf[8]{};
    uint32_t n = 0;
    EXPECT_FALSE(b.recvFrom(src, buf, sizeof(buf), n));
}

TEST(FakeTransport, RecvDropsWhenCapacityTooSmall)
{
    FakeHub hub;
    const Address addrA = makeAddr(10, 0, 0, 1, 1);
    const Address addrB = makeAddr(10, 0, 0, 2, 2);
    FakeTransport a(hub, addrA);
    FakeTransport b(hub, addrB);

    const uint8_t msg[] = {1, 2, 3, 4};
    ASSERT_TRUE(a.sendTo(addrB, msg, sizeof(msg)));

    Address  src{};
    uint8_t  buf[2]{};
    uint32_t n = 99;
    ASSERT_TRUE(b.recvFrom(src, buf, sizeof(buf), n));
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(src, addrA);
}
