#include <gtest/gtest.h>

#include <cstring>

#include "Core/Log.h"
#include "Core/MemoryTracker.h"
#include "Debug/DebugClient.h"
#include "Debug/DebugServer.h"
#include "Debug/PerfCounters.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Network/FakeTcp.h"

using namespace Dark;

namespace
{
    void pumpHandshake(DebugServer& server, DebugClient& client, int steps = 16)
    {
        for (int i = 0; i < steps; ++i)
        {
            client.poll(0.0f);
            server.poll(0.0f);
            DebugFrameStats fs{};
            server.flush(nullptr, fs, 0, 0, 0.0f, 0.05f);
            client.poll(0.0f);
            if (client.handshakeOk() && server.hasClient())
                return;
        }
    }
}

TEST(MemoryTracker, SetSnapshotClear)
{
    MemoryTracker::reset();
    MemoryTracker::set("Transform", 128, 256, 4);
    DebugMemoryPool pools[8]{};
    uint32_t        n = 0;
    MemoryTracker::snapshot(pools, 8, n);
    ASSERT_EQ(n, 1u);
    EXPECT_STREQ(pools[0].name, "Transform");
    EXPECT_EQ(pools[0].used, 128u);
    EXPECT_EQ(pools[0].count, 4u);
    MemoryTracker::clear("Transform");
    MemoryTracker::snapshot(pools, 8, n);
    EXPECT_EQ(n, 0u);
    MemoryTracker::reset();
}

TEST(WorldPools, StatsAfterEmplace)
{
    World world;
    Entity e = world.createEntity();
    world.emplace<TagComponent>(e, "x");
    world.emplace<TransformComponent>(e);

    uint32_t saw = 0;
    world.forEachPool([&](const IComponentPool& pool) {
        if (std::strcmp(pool.typeName(), "Tag") == 0)
        {
            EXPECT_EQ(pool.count(), 1u);
            EXPECT_GE(pool.bytesUsed(), sizeof(TagComponent));
            ++saw;
        }
        if (std::strcmp(pool.typeName(), "Transform") == 0)
        {
            EXPECT_EQ(pool.count(), 1u);
            EXPECT_EQ(pool.stride(), sizeof(TransformComponent));
            ++saw;
        }
    });
    EXPECT_EQ(saw, 2u);

    world.destroyEntity(e);
    world.forEachPool([&](const IComponentPool& pool) {
        if (std::strcmp(pool.typeName(), "Tag") == 0)
            EXPECT_EQ(pool.count(), 0u);
    });
}

TEST(PerfCounters, RecordsSlotAndHistory)
{
    PerfCounters perf;
    perf.beginFrame();
    perf.add(PerfSlot::Update, 0.004f);
    DebugFrameStats fs{};
    fs.drawCalls = 3;
    perf.endFrame(0.016f, fs, 1, 2, 12.0f);
    EXPECT_NEAR(perf.last().phaseMs[static_cast<uint32_t>(PerfSlot::Update)], 4.0f, 0.01f);
    EXPECT_EQ(perf.last().drawCalls, 3u);
    EXPECT_EQ(perf.historyCount(), 1u);
    float ms = 0.0f;
    ASSERT_TRUE(perf.historyAt(0, ms));
    EXPECT_NEAR(ms, 16.0f, 0.05f);
}

TEST(DebugServerClient, HelloAndLogLine)
{
    MemoryTracker::reset();
    Log::resetFilters();

    FakeTcpHub      hub;
    FakeTcpListener listener(hub);
    DebugServer     server;
    server.setAppTitle("TestHost");
    server.setListener(&listener);
    ASSERT_TRUE(server.listen(26162));
    ASSERT_TRUE(server.isListening());

    FakeTcpStream clientStream;
    ASSERT_TRUE(hub.connectClient(clientStream, 26162));
    ASSERT_TRUE(clientStream.isOpen());

    DebugClient client;
    client.setStream(&clientStream);
    ASSERT_FALSE(client.handshakeOk());
    pumpHandshake(server, client);
    ASSERT_TRUE(client.handshakeOk()) << "handshake failed listening=" << server.isListening()
                                     << " hasClient=" << server.hasClient()
                                     << " connecting=" << client.isConnecting();
    EXPECT_STREQ(client.hostTitle(), "TestHost");

    DE_LOG_WARN(LogCategory::Render, "pool overflow");
    DebugFrameStats fs{};
    server.flush(nullptr, fs, 0, 0, 0.0f, 0.0f);
    client.poll(0.0f);

    bool found = false;
    for (uint32_t i = 0; i < client.logCount(); ++i)
    {
        DebugLogEntry e{};
        ASSERT_TRUE(client.logAt(i, e));
        if (e.category == LogCategory::Render && std::strstr(e.text, "pool overflow"))
        {
            EXPECT_EQ(e.level, LogLevel::Warn);
            found = true;
        }
    }
    EXPECT_TRUE(found);

    server.shutdown();
    client.disconnect();
}

TEST(DebugServerClient, SubscribeHidesMemory)
{
    FakeTcpHub      hub;
    FakeTcpListener listener(hub);
    DebugServer     server;
    server.setListener(&listener);
    ASSERT_TRUE(server.listen(26162));

    FakeTcpStream clientStream;
    ASSERT_TRUE(hub.connectClient(clientStream, 26162));
    DebugClient client;
    client.setSubscribeMask(DebugChannelLog);
    client.setStream(&clientStream);
    pumpHandshake(server, client);
    ASSERT_TRUE(client.handshakeOk());

    World world;
    Entity e = world.createEntity();
    world.emplace<TransformComponent>(e);
    DebugFrameStats fs{};
    server.flush(&world, fs, 0, 0, 0.0f, 1.0f);
    client.poll(0.0f);
    EXPECT_EQ(client.poolCount(), 0u);

    client.setSubscribeMask(DebugChannelAll);
    for (int i = 0; i < 8; ++i)
    {
        server.poll(0.0f);
        server.flush(&world, fs, 0, 0, 0.0f, 1.0f);
        client.poll(0.0f);
    }
    EXPECT_GT(client.poolCount(), 0u);

    server.shutdown();
    client.disconnect();
}

TEST(DebugServerClient, LogFilterDropsTrace)
{
    FakeTcpHub      hub;
    FakeTcpListener listener(hub);
    DebugServer     server;
    server.setListener(&listener);
    ASSERT_TRUE(server.listen(26162));

    FakeTcpStream clientStream;
    ASSERT_TRUE(hub.connectClient(clientStream, 26162));
    DebugClient client;
    client.setLogFilter(LogLevel::Warn, 0xFFFFFFFFu);
    client.setStream(&clientStream);
    pumpHandshake(server, client);
    ASSERT_TRUE(client.handshakeOk());
    for (int i = 0; i < 4; ++i)
    {
        client.poll(0.0f);
        server.poll(0.0f);
        DebugFrameStats fs0{};
        server.flush(nullptr, fs0, 0, 0, 0.0f, 0.0f);
    }
    client.clearLogs();

    DE_LOG_TRACE(LogCategory::Core, "trace-hidden");
    DE_LOG_ERROR(LogCategory::Core, "error-visible");
    DebugFrameStats fs{};
    server.flush(nullptr, fs, 0, 0, 0.0f, 0.0f);
    client.poll(0.0f);

    bool sawTrace = false;
    bool sawError = false;
    for (uint32_t i = 0; i < client.logCount(); ++i)
    {
        DebugLogEntry e{};
        ASSERT_TRUE(client.logAt(i, e));
        if (std::strstr(e.text, "trace-hidden"))
            sawTrace = true;
        if (std::strstr(e.text, "error-visible"))
            sawError = true;
    }
    EXPECT_FALSE(sawTrace);
    EXPECT_TRUE(sawError);

    server.shutdown();
    client.disconnect();
}
