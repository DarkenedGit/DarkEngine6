#include <gtest/gtest.h>

#include "Save/PersistentId.h"

using namespace Dark;

TEST(PersistentId, PinnedProceduralIds)
{
    EXPECT_EQ(static_cast<uint64_t>(makeProceduralId("")), 0xcbf29ce484222325ull);
    EXPECT_EQ(static_cast<uint64_t>(makeProceduralId("session")), 0xbca2c49553a457d7ull);
    EXPECT_EQ(static_cast<uint64_t>(makeProceduralId("sandbox/player")), 0xce9917c29453c4bcull);
}

TEST(PersistentId, SameKeyIsStableAndDistinct)
{
    EXPECT_EQ(makeProceduralId("sandbox/pathchase/hunter/0"), makeProceduralId("sandbox/pathchase/hunter/0"));
    EXPECT_NE(makeProceduralId("sandbox/pathchase/hunter/0"), makeProceduralId("sandbox/pathchase/hunter/1"));
    EXPECT_EQ(persistOriginName(PersistOrigin::Spawned), std::string_view("spawned"));
    PersistOrigin origin = PersistOrigin::Authored;
    EXPECT_TRUE(tryParsePersistOrigin("procedural", origin));
    EXPECT_EQ(origin, PersistOrigin::Procedural);
}
