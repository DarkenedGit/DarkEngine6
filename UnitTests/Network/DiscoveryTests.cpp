#include <gtest/gtest.h>

#include "ECS/World.h"
#include "Network/FakeTransport.h"
#include "Network/NetworkSystem.h"
#include "Network/Protocol.h"

#include <cstdint>
#include <cstring>

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

    const Address kHostAddr    = makeAddr(10, 0, 0, 1, 26160);
    const Address kClientAddr  = makeAddr(10, 0, 0, 2, 0);
    const Address kBrowseAddr  = makeAddr(10, 0, 0, 3, 26161);
    const Address kBrowse2Addr = makeAddr(10, 0, 0, 4, 26161);

    void pump(NetworkSystem& a, NetworkSystem& b, World& w, float dt = 0.f, int n = 8)
    {
        for (int i = 0; i < n; ++i)
        {
            a.poll(w, dt);
            b.poll(w, dt);
            a.flush(w, dt);
            b.flush(w, dt);
        }
    }
}

TEST(Discovery, TwoFakeTransportsSeeBeacon)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    FakeTransport tBrowse(hub, kBrowseAddr);
    NetworkSystem host;
    NetworkSystem browser;
    host.setTransport(&tHost);
    browser.setTransport(&tBrowse);
    host.setPlayerName("HostA");
    host.setSceneMode(1);

    World w;
    ASSERT_TRUE(browser.browse());
    ASSERT_TRUE(host.host());
    host.flush(w, 0.f);

    browser.poll(w, 0.f);
    ASSERT_EQ(browser.sessionCount(), 1u);

    NetSessionInfo info{};
    ASSERT_TRUE(browser.sessionAt(0, info));
    EXPECT_EQ(info.address.ipv4, kHostAddr.ipv4);
    EXPECT_EQ(info.address.port, kNetDefaultPort);
    EXPECT_STREQ(info.name, "HostA");
    EXPECT_EQ(info.sceneMode, 1);
    EXPECT_EQ(info.peerCount, 0);
    EXPECT_GE(info.ageSec, 0.f);
}

TEST(Discovery, SecondBrowserAlsoSeesBeacon)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    FakeTransport tA(hub, kBrowseAddr);
    FakeTransport tB(hub, kBrowse2Addr);
    NetworkSystem host;
    NetworkSystem a;
    NetworkSystem b;
    host.setTransport(&tHost);
    a.setTransport(&tA);
    b.setTransport(&tB);
    host.setPlayerName("Lan");

    World w;
    ASSERT_TRUE(a.browse());
    ASSERT_TRUE(b.browse());
    ASSERT_TRUE(host.host());
    host.flush(w, 0.f);
    a.poll(w, 0.f);
    b.poll(w, 0.f);

    EXPECT_EQ(a.sessionCount(), 1u);
    EXPECT_EQ(b.sessionCount(), 1u);
}

TEST(Discovery, ListingsAgeOutAfterThreeSeconds)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    FakeTransport tBrowse(hub, kBrowseAddr);
    NetworkSystem host;
    NetworkSystem browser;
    host.setTransport(&tHost);
    browser.setTransport(&tBrowse);

    World w;
    ASSERT_TRUE(browser.browse());
    ASSERT_TRUE(host.host());
    host.flush(w, 0.f);
    browser.poll(w, 0.f);
    ASSERT_EQ(browser.sessionCount(), 1u);

    host.disconnect();
    browser.poll(w, 2.9f);
    EXPECT_EQ(browser.sessionCount(), 1u);
    browser.poll(w, 0.2f);
    EXPECT_EQ(browser.sessionCount(), 0u);
}

TEST(Discovery, BrowseIdleOnlyAndStopBrowseIdempotent)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    NetworkSystem host;
    host.setTransport(&tHost);

    World w;
    ASSERT_TRUE(host.host(kNetDefaultPort));
    EXPECT_EQ(host.role(), NetRole::Host);
    EXPECT_FALSE(host.browse());

    host.stopBrowse();
    host.stopBrowse();
    EXPECT_EQ(host.sessionCount(), 0u);

    NetSessionInfo dummy{};
    EXPECT_FALSE(host.sessionAt(0, dummy));
}

TEST(Discovery, HostAndJoinStopBrowse)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    FakeTransport tBrowse(hub, kBrowseAddr);
    FakeTransport tOther(hub, makeAddr(10, 0, 0, 9, 26160));
    NetworkSystem host;
    NetworkSystem browser;
    NetworkSystem other;
    host.setTransport(&tHost);
    browser.setTransport(&tBrowse);
    other.setTransport(&tOther);
    other.setPlayerName("Other");

    World w;
    ASSERT_TRUE(other.host());
    other.flush(w, 0.f);
    ASSERT_TRUE(browser.browse());
    browser.poll(w, 0.f);
    ASSERT_EQ(browser.sessionCount(), 1u);

    ASSERT_TRUE(browser.join(kHostAddr));
    EXPECT_EQ(browser.sessionCount(), 0u);
    EXPECT_EQ(browser.role(), NetRole::Joining);

    ASSERT_TRUE(host.browse());
    ASSERT_TRUE(host.host());
    EXPECT_EQ(host.role(), NetRole::Host);
    EXPECT_FALSE(host.browse());
    EXPECT_EQ(host.sessionCount(), 0u);
}

TEST(Discovery, DisconnectStopsBeacon)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    FakeTransport tBrowse(hub, kBrowseAddr);
    NetworkSystem host;
    NetworkSystem browser;
    host.setTransport(&tHost);
    browser.setTransport(&tBrowse);

    World w;
    ASSERT_TRUE(browser.browse());
    ASSERT_TRUE(host.host());
    host.flush(w, 0.f);
    browser.poll(w, 0.f);
    ASSERT_EQ(browser.sessionCount(), 1u);

    host.disconnect();
    host.flush(w, 1.0f);
    host.flush(w, 1.0f);
    browser.poll(w, 0.f);
    EXPECT_EQ(browser.sessionCount(), 1u);
    browser.poll(w, kNetSessionAgeOutSec);
    EXPECT_EQ(browser.sessionCount(), 0u);
}

TEST(Discovery, BeaconPeerCountAndJoinFromListing)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    FakeTransport tClient(hub, kClientAddr);
    FakeTransport tBrowse(hub, kBrowseAddr);
    NetworkSystem host;
    NetworkSystem client;
    NetworkSystem browser;
    host.setTransport(&tHost);
    client.setTransport(&tClient);
    browser.setTransport(&tBrowse);
    host.setPlayerName("Play");
    host.setSceneMode(0);

    World w;
    ASSERT_TRUE(host.host());
    ASSERT_TRUE(client.join(kHostAddr));
    pump(host, client, w);
    ASSERT_EQ(host.role(), NetRole::Host);
    ASSERT_EQ(client.role(), NetRole::Client);
    EXPECT_EQ(host.peerCount(), 1u);

    ASSERT_TRUE(browser.browse());
    host.flush(w, kNetBeaconIntervalSec);
    browser.poll(w, 0.f);
    ASSERT_EQ(browser.sessionCount(), 1u);

    NetSessionInfo info{};
    ASSERT_TRUE(browser.sessionAt(0, info));
    EXPECT_STREQ(info.name, "Play");
    EXPECT_EQ(info.sceneMode, 0);
    EXPECT_EQ(info.peerCount, 1);
    EXPECT_EQ(info.address, kHostAddr);

    ASSERT_TRUE(browser.join(info.address));
    pump(host, browser, w);
    EXPECT_EQ(browser.role(), NetRole::Client);
    EXPECT_EQ(host.peerCount(), 2u);
}

TEST(Discovery, BrowseDoesNotSendQuery)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    FakeTransport tBrowse(hub, kBrowseAddr);
    NetworkSystem host;
    NetworkSystem browser;
    host.setTransport(&tHost);
    browser.setTransport(&tBrowse);

    World w;
    ASSERT_TRUE(browser.browse());
    browser.poll(w, 0.1f);
    browser.flush(w, 0.1f);

    Address  src{};
    uint8_t  buf[kNetMaxPayload]{};
    uint32_t n = 0;
    EXPECT_FALSE(tHost.recvFrom(src, buf, sizeof(buf), n));
}

TEST(Discovery, ShutdownStopsBrowse)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    FakeTransport tBrowse(hub, kBrowseAddr);
    NetworkSystem host;
    NetworkSystem browser;
    host.setTransport(&tHost);
    browser.setTransport(&tBrowse);

    World w;
    ASSERT_TRUE(browser.browse());
    ASSERT_TRUE(host.host());
    host.flush(w, 0.f);
    browser.poll(w, 0.f);
    ASSERT_EQ(browser.sessionCount(), 1u);

    browser.shutdown();
    EXPECT_EQ(browser.sessionCount(), 0u);
    EXPECT_EQ(browser.role(), NetRole::Idle);
}
