#include <gtest/gtest.h>

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/Vector3f.h"
#include "Network/FakeTransport.h"
#include "Network/NetworkSystem.h"
#include "Network/Replication.h"

#include <cmath>
#include <cstdint>
#include <limits>
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
    constexpr float kTick     = 1.f / kNetTickHz;

    struct NetPair
    {
        FakeHub       hub;
        FakeTransport tHost;
        FakeTransport tClient;
        NetworkSystem host;
        NetworkSystem client;
        World         hw;
        World         cw;

        NetPair() :
            tHost(hub, kHostAddr),
            tClient(hub, kClientAddr)
        {
            host.setTransport(&tHost);
            client.setTransport(&tClient);
        }
    };

    void pump(NetworkSystem& a, NetworkSystem& b, World& aw, World& bw, float dt = 0.f, int n = 8)
    {
        for (int i = 0; i < n; ++i)
        {
            a.poll(aw, dt);
            b.poll(bw, dt);
            a.flush(aw, dt);
            b.flush(bw, dt);
        }
    }

    bool handshake(NetPair& n)
    {
        if (!n.host.host(9999))
            return false;
        if (!n.client.join(kHostAddr))
            return false;
        pump(n.host, n.client, n.hw, n.cw);
        return n.host.role() == NetRole::Host && n.client.role() == NetRole::Client && n.host.peerCount() == 1u;
    }

    void tick(NetPair& n, int count = 1)
    {
        pump(n.host, n.client, n.hw, n.cw, kTick, count);
    }

    Entity makeCube(World& w, float x, float y, float z)
    {
        Entity e = w.createEntity();
        TransformComponent xf;
        xf.position = Math::Vector3f(x, y, z);
        w.emplace<TransformComponent>(e, xf);
        return e;
    }

    uint32_t countNetworked(World& w)
    {
        uint32_t n = 0;
        w.each<NetworkedComponent>([&](Entity, NetworkedComponent&) { ++n; });
        return n;
    }

    TransformComponent* xfOf(World& w, Entity e)
    {
        return w.get<TransformComponent>(e);
    }

    struct ReplicaLog
    {
        int                   spawns   = 0;
        int                   despawns = 0;
        std::vector<NetId>    despawnIds;
        std::vector<NetPrefab> prefabs;
        bool                  rejectSpawn = false;
    };

    bool onSpawn(World&, Entity, NetPrefab prefab, const TransformComponent&, uint32_t, void* user)
    {
        auto* log = static_cast<ReplicaLog*>(user);
        ++log->spawns;
        log->prefabs.push_back(prefab);
        return !log->rejectSpawn;
    }

    void onDespawn(World&, Entity, NetId id, void* user)
    {
        auto* log = static_cast<ReplicaLog*>(user);
        ++log->despawns;
        log->despawnIds.push_back(id);
    }
} // namespace

TEST(Replication, IdleRegisterThenHostSpawnsOnClient)
{
    NetPair n;
    Entity  cube = makeCube(n.hw, 1.f, 2.f, 3.f);
    ASSERT_TRUE(n.host.registerEntity(n.hw, cube, NetPrefab::Cube, ClientId::Host, 0x11223344u));
    EXPECT_EQ(n.host.netIdFor(cube), NULL_NET_ID);
    EXPECT_TRUE(n.hw.has<NetworkedComponent>(cube));

    ASSERT_TRUE(handshake(n));
    const NetId id = n.host.netIdFor(cube);
    EXPECT_NE(id, NULL_NET_ID);
    EXPECT_EQ(n.host.entityFor(id), cube);

    Entity remote = n.client.entityFor(id);
    ASSERT_TRUE(remote.valid());
    EXPECT_TRUE(n.cw.alive(remote));
    ASSERT_TRUE(n.cw.has<NetworkedComponent>(remote));
    const NetworkedComponent* nc = n.cw.get<NetworkedComponent>(remote);
    ASSERT_NE(nc, nullptr);
    EXPECT_EQ(nc->netId, id);
    EXPECT_EQ(nc->prefab, NetPrefab::Cube);
    EXPECT_EQ(nc->owner, ClientId::Host);
    EXPECT_EQ(nc->colorRgba8, 0x11223344u);
    const TransformComponent* xf = xfOf(n.cw, remote);
    ASSERT_NE(xf, nullptr);
    EXPECT_NEAR(xf->position.x, 1.f, 1.0e-4f);
    EXPECT_NEAR(xf->position.y, 2.f, 1.0e-4f);
    EXPECT_NEAR(xf->position.z, 3.f, 1.0e-4f);
}

TEST(Replication, UnregisterQueuesDespawnAndClientDestroys)
{
    NetPair    n;
    ReplicaLog log;
    n.client.setSpawnCallback(onSpawn, &log);
    n.client.setDespawnCallback(onDespawn, &log);

    Entity cube = makeCube(n.hw, 0.f, 0.f, 0.f);
    ASSERT_TRUE(n.host.registerEntity(n.hw, cube, NetPrefab::Cube));
    ASSERT_TRUE(handshake(n));
    const NetId id = n.host.netIdFor(cube);
    ASSERT_NE(id, NULL_NET_ID);
    EXPECT_GE(log.spawns, 1);
    ASSERT_TRUE(n.client.entityFor(id).valid());

    n.host.unregisterEntity(n.hw, cube);
    EXPECT_FALSE(n.hw.alive(cube));
    EXPECT_FALSE(n.hw.has<NetworkedComponent>(cube));
    n.host.unregisterEntity(n.hw, cube); // no double-destroy

    pump(n.host, n.client, n.hw, n.cw);
    EXPECT_FALSE(n.client.entityFor(id).valid());
    EXPECT_GE(log.despawns, 1);
    ASSERT_FALSE(log.despawnIds.empty());
    EXPECT_EQ(log.despawnIds.back(), id);
}

TEST(Replication, DuplicateRegisterIsNoOp)
{
    NetPair n;
    ASSERT_TRUE(n.host.host());
    Entity cube = makeCube(n.hw, 0.f, 0.f, 0.f);
    ASSERT_TRUE(n.host.registerEntity(n.hw, cube, NetPrefab::Cube));
    const NetId id = n.host.netIdFor(cube);
    EXPECT_TRUE(n.host.registerEntity(n.hw, cube, NetPrefab::Sphere, ClientId::Host, 0x01020304u));
    EXPECT_EQ(n.host.netIdFor(cube), id);
    EXPECT_EQ(countNetworked(n.hw), 1u);
    const NetworkedComponent* nc = n.hw.get<NetworkedComponent>(cube);
    ASSERT_NE(nc, nullptr);
    EXPECT_EQ(nc->prefab, NetPrefab::Cube);
}

TEST(Replication, Cap32)
{
    NetPair n;
    ASSERT_TRUE(n.host.host());
    std::vector<Entity> ents;
    ents.reserve(kNetMaxReplicated);
    for (uint32_t i = 0; i < kNetMaxReplicated; ++i)
    {
        Entity e = makeCube(n.hw, static_cast<float>(i), 0.f, 0.f);
        ASSERT_TRUE(n.host.registerEntity(n.hw, e, NetPrefab::Cube)) << i;
        ents.push_back(e);
    }
    EXPECT_EQ(countNetworked(n.hw), kNetMaxReplicated);
    Entity extra = makeCube(n.hw, 99.f, 0.f, 0.f);
    EXPECT_FALSE(n.host.registerEntity(n.hw, extra, NetPrefab::Cube));
    EXPECT_FALSE(n.hw.has<NetworkedComponent>(extra));
    EXPECT_TRUE(n.host.registerEntity(n.hw, ents.front(), NetPrefab::Cube));

    n.host.unregisterEntity(n.hw, ents.back());
    Entity again = makeCube(n.hw, 100.f, 0.f, 0.f);
    EXPECT_TRUE(n.host.registerEntity(n.hw, again, NetPrefab::Sphere));
    EXPECT_NE(n.host.netIdFor(again), n.host.netIdFor(ents.front()));
}

TEST(Replication, RegisterOnClientAndJoiningReturnsFalse)
{
    NetPair n;
    Entity  local = makeCube(n.cw, 0.f, 0.f, 0.f);
    ASSERT_TRUE(n.host.host());
    ASSERT_TRUE(n.client.join(kHostAddr));
    EXPECT_EQ(n.client.role(), NetRole::Joining);
    EXPECT_FALSE(n.client.registerEntity(n.cw, local, NetPrefab::Cube));

    pump(n.host, n.client, n.hw, n.cw);
    ASSERT_EQ(n.client.role(), NetRole::Client);
    EXPECT_FALSE(n.client.registerEntity(n.cw, local, NetPrefab::Cube));
    EXPECT_FALSE(n.cw.has<NetworkedComponent>(local));
}

TEST(Replication, SpawnCallbackUnsetStillCreatesEntity)
{
    NetPair n;
    Entity  cube = makeCube(n.hw, 4.f, 5.f, 6.f);
    ASSERT_TRUE(n.host.registerEntity(n.hw, cube, NetPrefab::Cube));
    ASSERT_TRUE(handshake(n));
    const NetId id = n.host.netIdFor(cube);
    Entity      remote = n.client.entityFor(id);
    ASSERT_TRUE(remote.valid());
    EXPECT_TRUE(n.cw.has<TransformComponent>(remote));
    EXPECT_TRUE(n.cw.has<NetworkedComponent>(remote));
}

TEST(Replication, InterpolationWritebackOnFlushSkipsLocalOwner)
{
    NetPair n;
    Entity  cube = makeCube(n.hw, 0.f, 0.f, 0.f);
    ASSERT_TRUE(n.host.registerEntity(n.hw, cube, NetPrefab::Cube));
    ASSERT_TRUE(handshake(n));
    const NetId cubeId = n.host.netIdFor(cube);
    Entity      remoteCube = n.client.entityFor(cubeId);
    ASSERT_TRUE(remoteCube.valid());

    Entity pawn = makeCube(n.hw, 0.f, 0.f, 0.f);
    ASSERT_TRUE(n.host.registerEntity(n.hw, pawn, NetPrefab::PlayerPawn, n.client.localClientId()));
    pump(n.host, n.client, n.hw, n.cw);
    Entity localPawn = n.client.localPawn();
    ASSERT_TRUE(localPawn.valid());
    ASSERT_NE(localPawn, remoteCube);

    TransformComponent* clientPawnXf = xfOf(n.cw, localPawn);
    ASSERT_NE(clientPawnXf, nullptr);
    clientPawnXf->position = Math::Vector3f(5.f, 0.f, 0.f);

    TransformComponent* hostPawnXf = xfOf(n.hw, pawn);
    ASSERT_NE(hostPawnXf, nullptr);
    hostPawnXf->position = Math::Vector3f(99.f, 0.f, 0.f);

    TransformComponent* hostCubeXf = xfOf(n.hw, cube);
    ASSERT_NE(hostCubeXf, nullptr);
    hostCubeXf->position = Math::Vector3f(10.f, 0.f, 0.f);
    tick(n, 1);

    hostCubeXf->position = Math::Vector3f(20.f, 0.f, 0.f);
    tick(n, 1);
    TransformComponent* clientCubeXf = xfOf(n.cw, remoteCube);
    ASSERT_NE(clientCubeXf, nullptr);
    EXPECT_NEAR(clientCubeXf->position.x, 10.f, 0.05f);

    hostCubeXf->position = Math::Vector3f(30.f, 0.f, 0.f);
    tick(n, 1);
    EXPECT_NEAR(clientCubeXf->position.x, 20.f, 0.05f);

    tick(n, 1);
    EXPECT_NEAR(clientCubeXf->position.x, 10.f, 0.05f);

    EXPECT_NEAR(clientPawnXf->position.x, 5.f, 0.05f);
}

TEST(Replication, PawnOwnerAndSpeedReject)
{
    NetPair n;
    ASSERT_TRUE(handshake(n));

    Entity pawn = makeCube(n.hw, 0.f, 0.f, 0.f);
    ASSERT_TRUE(n.host.registerEntity(n.hw, pawn, NetPrefab::PlayerPawn, n.client.localClientId()));
    pump(n.host, n.client, n.hw, n.cw);
    Entity localPawn = n.client.localPawn();
    ASSERT_TRUE(localPawn.valid());
    EXPECT_EQ(n.client.netIdFor(localPawn), n.host.netIdFor(pawn));

    tick(n, 4);
    TransformComponent* hostXf = xfOf(n.hw, pawn);
    ASSERT_NE(hostXf, nullptr);
    EXPECT_NEAR(hostXf->position.x, 0.f, 0.05f);

    TransformComponent* clientXf = xfOf(n.cw, localPawn);
    ASSERT_NE(clientXf, nullptr);
    clientXf->position = Math::Vector3f(100.f, 0.f, 0.f);
    n.client.flush(n.cw, kTick);
    n.host.poll(n.hw, kTick);
    EXPECT_NEAR(hostXf->position.x, 0.f, 0.05f);

    clientXf->position = Math::Vector3f(0.4f, 0.f, 0.f);
    tick(n, 4);
    EXPECT_NEAR(hostXf->position.x, 0.4f, 0.1f);
}

TEST(Replication, SnapshotIgnoresUnknownNetIdAndNaN)
{
    NetPair n;
    Entity  cube = makeCube(n.hw, 1.f, 0.f, 0.f);
    ASSERT_TRUE(n.host.registerEntity(n.hw, cube, NetPrefab::Cube));
    ASSERT_TRUE(handshake(n));
    tick(n, 3);

    TransformComponent* hostXf = xfOf(n.hw, cube);
    ASSERT_NE(hostXf, nullptr);
    hostXf->position.x = std::numeric_limits<float>::quiet_NaN();
    tick(n, 2);

    Entity remote = n.client.entityFor(n.host.netIdFor(cube));
    ASSERT_TRUE(remote.valid());
    TransformComponent* clientXf = xfOf(n.cw, remote);
    ASSERT_NE(clientXf, nullptr);
    EXPECT_TRUE(std::isfinite(clientXf->position.x));
}

TEST(Replication, HostRegisterAfterJoinSendsSpawn)
{
    NetPair n;
    ASSERT_TRUE(handshake(n));
    Entity cube = makeCube(n.hw, 7.f, 8.f, 9.f);
    ASSERT_TRUE(n.host.registerEntity(n.hw, cube, NetPrefab::Sphere));
    pump(n.host, n.client, n.hw, n.cw);
    Entity remote = n.client.entityFor(n.host.netIdFor(cube));
    ASSERT_TRUE(remote.valid());
    const NetworkedComponent* nc = n.cw.get<NetworkedComponent>(remote);
    ASSERT_NE(nc, nullptr);
    EXPECT_EQ(nc->prefab, NetPrefab::Sphere);
}

TEST(Replication, LocalPawnRequiresPawnPrefab)
{
    NetPair n;
    Entity  cube = makeCube(n.hw, 0.f, 0.f, 0.f);
    ASSERT_TRUE(n.host.registerEntity(n.hw, cube, NetPrefab::Cube, ClientId::Host));
    ASSERT_TRUE(handshake(n));
    EXPECT_FALSE(n.host.localPawn().valid());

    Entity pawn = makeCube(n.hw, 0.f, 0.f, 0.f);
    ASSERT_TRUE(n.host.registerEntity(n.hw, pawn, NetPrefab::PlayerPawn, ClientId::Host));
    EXPECT_EQ(n.host.localPawn(), pawn);
}
