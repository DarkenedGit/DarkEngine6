#include <gtest/gtest.h>

#include "ECS/World.h"
#include "Network/FakeTransport.h"
#include "Network/NetworkSystem.h"
#include "Network/Packet.h"
#include "Network/Protocol.h"
#include "Network/Reliability.h"

#include <cstdint>
#include <cstring>
#include <memory>
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

    const Address kHostAddr   = makeAddr(10, 0, 0, 1, 26160);
    const Address kClientAddr = makeAddr(10, 0, 0, 2, 0);

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

    bool handshake(NetworkSystem& host, NetworkSystem& client, World& w)
    {
        if (!host.host(9999))
            return false;
        if (!client.join(kHostAddr))
            return false;
        pump(host, client, w);
        return host.role() == NetRole::Host && client.role() == NetRole::Client && host.peerCount() == 1u;
    }

    uint16_t nextSeq(uint16_t& seq)
    {
        ++seq;
        if (seq == 0)
            seq = 1;
        return seq;
    }

    bool writeConnectRequestDatagram(uint8_t* buf, uint32_t cap, uint32_t& outSize, uint8_t version, uint8_t sceneMode, uint16_t& seq)
    {
        PacketWriter w;
        if (!w.begin(buf, cap))
            return false;
        DatagramHeader h{};
        h.magic         = kNetMagic;
        h.version       = version;
        h.headerFlags   = 0;
        h.headerSize    = kNetHeaderSize;
        h.seq           = nextSeq(seq);
        h.connToken     = 0;
        h.reliableCount = 0;
        if (!writeDatagramHeader(w, h))
            return false;
        ConnectRequestPayload p{};
        p.sceneMode = sceneMode;
        p.wantsPawn = 1;
        if (!w.writeU8(static_cast<uint8_t>(NetOpcode::ConnectRequest)) || !writeConnectRequest(w, p))
            return false;
        outSize = w.size();
        return true;
    }

    struct PeerLog
    {
        std::vector<NetPeerEvent> events;
        std::vector<ClientId>     ids;
        std::vector<bool>         wantsPawn;
    };

    void peerCb(const NetPeerInfo& info, NetPeerEvent event, void* user)
    {
        auto* log = static_cast<PeerLog*>(user);
        log->events.push_back(event);
        log->ids.push_back(info.id);
        log->wantsPawn.push_back(info.wantsPawn);
    }
} // namespace

TEST(Session, HandshakeWithoutRealBind)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    FakeTransport tClient(hub, kClientAddr);
    NetworkSystem host;
    NetworkSystem client;
    host.setTransport(&tHost);
    client.setTransport(&tClient);

    World w;
    ASSERT_TRUE(handshake(host, client, w));
    EXPECT_EQ(host.boundAddress(), kHostAddr);
    EXPECT_EQ(client.boundAddress(), kClientAddr);
    EXPECT_EQ(host.localClientId(), ClientId::Host);
    EXPECT_NE(client.localClientId(), ClientId::Host);
    EXPECT_NE(client.localClientId(), ClientId::Invalid);
    EXPECT_TRUE(host.isValid());
    EXPECT_TRUE(client.isValid());
    EXPECT_TRUE(host.isConnected(client.localClientId()));
}

TEST(Session, HostJoinWhileNotIdleReturnsFalse)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    FakeTransport tClient(hub, kClientAddr);
    NetworkSystem host;
    NetworkSystem client;
    host.setTransport(&tHost);
    client.setTransport(&tClient);

    ASSERT_TRUE(host.host());
    EXPECT_EQ(host.role(), NetRole::Host);
    EXPECT_FALSE(host.host());
    EXPECT_FALSE(host.join(kClientAddr));
    EXPECT_EQ(host.role(), NetRole::Host);

    ASSERT_TRUE(client.join(kHostAddr));
    EXPECT_EQ(client.role(), NetRole::Joining);
    EXPECT_FALSE(client.join(kHostAddr));
    EXPECT_FALSE(client.host());
    EXPECT_EQ(client.role(), NetRole::Joining);
}

TEST(Session, JoinTimeoutWithSyntheticDt)
{
    FakeHub       hub;
    FakeTransport tClient(hub, kClientAddr);
    NetworkSystem client;
    client.setTransport(&tClient);

    World w;
    ASSERT_TRUE(client.join(kHostAddr));
    EXPECT_EQ(client.role(), NetRole::Joining);

    client.poll(w, 4.9f);
    EXPECT_EQ(client.role(), NetRole::Joining);

    client.poll(w, 0.2f);
    EXPECT_EQ(client.role(), NetRole::Idle);
    EXPECT_EQ(client.localClientId(), ClientId::Invalid);
}

TEST(Session, JoinedLeftCallbackOrder)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    FakeTransport tClient(hub, kClientAddr);
    NetworkSystem host;
    NetworkSystem client;
    host.setTransport(&tHost);
    client.setTransport(&tClient);
    client.setWantsPawn(true);

    PeerLog log;
    host.setPeerCallback(peerCb, &log);

    World w;
    ASSERT_TRUE(handshake(host, client, w));
    ASSERT_EQ(log.events.size(), 1u);
    EXPECT_EQ(log.events[0], NetPeerEvent::Joined);
    EXPECT_EQ(log.ids[0], client.localClientId());
    EXPECT_TRUE(log.wantsPawn[0]);

    client.disconnect();
    EXPECT_EQ(client.role(), NetRole::Idle);
    host.poll(w, 0.f);
    ASSERT_EQ(log.events.size(), 2u);
    EXPECT_EQ(log.events[1], NetPeerEvent::Left);
    EXPECT_EQ(log.ids[1], log.ids[0]);
    EXPECT_EQ(host.peerCount(), 0u);
    EXPECT_EQ(host.role(), NetRole::Host);
}

TEST(Session, ModeMismatchReject)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    FakeTransport tClient(hub, kClientAddr);
    NetworkSystem host;
    NetworkSystem client;
    host.setTransport(&tHost);
    client.setTransport(&tClient);
    host.setSceneMode(0);
    client.setSceneMode(1);

    World w;
    ASSERT_TRUE(host.host());
    ASSERT_TRUE(client.join(kHostAddr));
    pump(host, client, w);
    EXPECT_EQ(client.role(), NetRole::Idle);
    EXPECT_EQ(host.role(), NetRole::Host);
    EXPECT_EQ(host.peerCount(), 0u);
}

TEST(Session, VersionMismatchReject)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    FakeTransport tAttacker(hub, makeAddr(10, 0, 0, 3, 1));
    NetworkSystem host;
    host.setTransport(&tHost);

    World w;
    ASSERT_TRUE(host.host());

    uint8_t  buf[kNetMaxPayload]{};
    uint32_t n   = 0;
    uint16_t seq = 0;
    ASSERT_TRUE(writeConnectRequestDatagram(buf, sizeof(buf), n, /*version*/ 99, /*sceneMode*/ 0, seq));
    ASSERT_TRUE(tAttacker.sendTo(kHostAddr, buf, n));

    host.poll(w, 0.f);
    EXPECT_EQ(host.peerCount(), 0u);

    Address  src{};
    uint8_t  in[kNetMaxPayload]{};
    uint32_t got = 0;
    ASSERT_TRUE(tAttacker.recvFrom(src, in, sizeof(in), got));
    EXPECT_EQ(src, kHostAddr);

    PacketReader r;
    DatagramHeader h{};
    ASSERT_TRUE(r.begin(in, got));
    ASSERT_TRUE(readDatagramHeader(r, h));
    EXPECT_EQ(h.magic, kNetMagic);
    EXPECT_EQ(h.version, kNetProtocolVersion);
    uint8_t op = 0;
    ASSERT_TRUE(r.readU8(op));
    EXPECT_EQ(op, static_cast<uint8_t>(NetOpcode::ConnectReject));
    uint8_t reason = 0;
    ASSERT_TRUE(r.readU8(reason));
    EXPECT_EQ(reason, static_cast<uint8_t>(ConnectRejectReason::Version));
}

TEST(Session, HostRejectsWhenFull)
{
    FakeHub hub;
    FakeTransport tHost(hub, kHostAddr);
    NetworkSystem host;
    host.setTransport(&tHost);

    World w;
    ASSERT_TRUE(host.host());

    std::vector<std::unique_ptr<FakeTransport>> transports;
    std::vector<std::unique_ptr<NetworkSystem>> clients;
    transports.reserve(kNetMaxClients);
    clients.reserve(kNetMaxClients);

    const uint32_t remoteCap = kNetMaxClients - 1;
    for (uint32_t i = 0; i < remoteCap; ++i)
    {
        const Address addr = makeAddr(10, 0, 0, static_cast<uint8_t>(2 + i), 0);
        transports.push_back(std::make_unique<FakeTransport>(hub, addr));
        clients.push_back(std::make_unique<NetworkSystem>());
        clients.back()->setTransport(transports.back().get());
        ASSERT_TRUE(clients.back()->join(kHostAddr));
        pump(host, *clients.back(), w, 0.f, 6);
        ASSERT_EQ(clients.back()->role(), NetRole::Client) << "remote " << i;
        ASSERT_EQ(host.peerCount(), i + 1);
    }

    EXPECT_EQ(host.peerCount(), remoteCap);

    const Address extraAddr = makeAddr(10, 0, 0, static_cast<uint8_t>(2 + remoteCap), 0);
    FakeTransport tExtra(hub, extraAddr);
    NetworkSystem extra;
    extra.setTransport(&tExtra);
    ASSERT_TRUE(extra.join(kHostAddr));
    pump(host, extra, w, 0.f, 6);
    EXPECT_EQ(extra.role(), NetRole::Idle);
    EXPECT_EQ(host.peerCount(), remoteCap);
    EXPECT_EQ(host.role(), NetRole::Host);
}

TEST(Session, HostDrainsStaleInboxBeforeSession)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    FakeTransport tClient(hub, kClientAddr);
    NetworkSystem host;
    NetworkSystem client;
    host.setTransport(&tHost);
    client.setTransport(&tClient);

    World w;
    ASSERT_TRUE(client.join(kHostAddr));
    EXPECT_EQ(client.role(), NetRole::Joining);
    ASSERT_TRUE(host.host());
    host.poll(w, 0.f);
    EXPECT_EQ(host.peerCount(), 0u);
    EXPECT_EQ(host.role(), NetRole::Host);
    EXPECT_EQ(client.role(), NetRole::Joining);
}

TEST(Session, JoinDrainsStaleAcceptFromPriorSession)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    FakeTransport tClient(hub, kClientAddr);
    NetworkSystem host;
    NetworkSystem client;
    host.setTransport(&tHost);
    client.setTransport(&tClient);

    World w;
    ASSERT_TRUE(handshake(host, client, w));
    client.disconnect();
    host.disconnect();
    EXPECT_EQ(client.role(), NetRole::Idle);

    uint8_t  buf[kNetMaxPayload]{};
    uint32_t n   = 0;
    uint16_t seq = 0;
    PacketWriter wPkt;
    ASSERT_TRUE(wPkt.begin(buf, sizeof(buf)));
    DatagramHeader h{};
    h.magic         = kNetMagic;
    h.version       = kNetProtocolVersion;
    h.headerSize    = kNetHeaderSize;
    h.seq           = nextSeq(seq);
    h.connToken     = 0xDEADBEEFu;
    ASSERT_TRUE(writeDatagramHeader(wPkt, h));
    ASSERT_TRUE(wPkt.writeU8(static_cast<uint8_t>(NetOpcode::ConnectAccept)));
    ConnectAcceptPayload acc{};
    acc.clientId   = 1;
    acc.connToken  = 0xDEADBEEFu;
    acc.netTickHz  = 20;
    acc.maxClients = 8;
    acc.serverTick = 0;
    ASSERT_TRUE(writeConnectAccept(wPkt, acc));
    n = wPkt.size();
    ASSERT_TRUE(tHost.sendTo(kClientAddr, buf, n));

    ASSERT_TRUE(client.join(kHostAddr));
    EXPECT_EQ(client.role(), NetRole::Joining);
    client.poll(w, 0.f);
    EXPECT_EQ(client.role(), NetRole::Joining);
    EXPECT_EQ(client.localClientId(), ClientId::Invalid);
}

TEST(Session, ShutdownIdempotentAndDoesNotDeleteInjected)
{
    FakeHub       hub;
    FakeTransport tHost(hub, kHostAddr);
    NetworkSystem host;
    host.setTransport(&tHost);
    ASSERT_TRUE(host.host());
    host.shutdown();
    EXPECT_EQ(host.role(), NetRole::Idle);
    host.shutdown();
    EXPECT_EQ(host.role(), NetRole::Idle);
    EXPECT_TRUE(host.isValid());
    ASSERT_TRUE(host.host());
    EXPECT_EQ(host.role(), NetRole::Host);
}
