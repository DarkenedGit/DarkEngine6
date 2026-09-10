#include <gtest/gtest.h>

#include "ECS/Components.h"
#include "ECS/World.h"

using namespace Dark;

namespace
{
    struct TestCounter
    {
        static constexpr const char* kTypeName = "TestCounter";
        int value = 0;
    };
}

TEST(World, CreateAliveDestroy)
{
    World world;
    Entity a = world.createEntity();
    Entity b = world.createEntity();

    EXPECT_TRUE(a.valid());
    EXPECT_TRUE(b.valid());
    EXPECT_NE(a.id(), b.id());
    EXPECT_TRUE(world.alive(a));
    EXPECT_TRUE(world.alive(b));

    world.destroyEntity(a);
    EXPECT_FALSE(world.alive(a));
    EXPECT_TRUE(world.alive(b));
}

TEST(World, RecycleInvalidatesStaleHandle)
{
    World world;
    Entity first = world.createEntity();
    const EntityID firstId = first.id();
    const EntityID index   = entityIndex(firstId);

    world.destroyEntity(first);
    EXPECT_FALSE(world.alive(first));

    Entity second = world.createEntity();
    EXPECT_TRUE(world.alive(second));
    EXPECT_EQ(entityIndex(second.id()), index);
    EXPECT_NE(second.id(), firstId);
    EXPECT_FALSE(world.alive(first));
    EXPECT_NE(entityGeneration(second.id()), entityGeneration(firstId));
}

TEST(World, DoubleEmplaceReplacesComponent)
{
    World world;
    Entity e = world.createEntity();

    auto& a = world.emplace<TestCounter>(e, TestCounter{ 1 });
    EXPECT_EQ(a.value, 1);
    EXPECT_TRUE(world.has<TestCounter>(e));

    auto& b = world.emplace<TestCounter>(e, TestCounter{ 99 });
    EXPECT_EQ(b.value, 99);
    EXPECT_EQ(world.get<TestCounter>(e)->value, 99);

    uint32_t count = 0;
    world.each<TestCounter>([&](Entity ent, TestCounter& c) {
        (void)c;
        if (ent == e)
            ++count;
    });
    EXPECT_EQ(count, 1u);
}

TEST(World, DestroyRemovesComponents)
{
    World world;
    Entity e = world.createEntity();
    world.emplace<TagComponent>(e);
    world.emplace<TestCounter>(e, TestCounter{ 7 });

    EXPECT_TRUE(world.has<TagComponent>(e));
    EXPECT_TRUE(world.has<TestCounter>(e));

    world.destroyEntity(e);
    EXPECT_FALSE(world.alive(e));
    EXPECT_FALSE(world.has<TagComponent>(e));
    EXPECT_FALSE(world.has<TestCounter>(e));
}

TEST(World, PoolRemoveSwapBackPreservesOther)
{
    World world;
    Entity a = world.createEntity();
    Entity b = world.createEntity();
    Entity c = world.createEntity();

    world.emplace<TestCounter>(a, TestCounter{ 1 });
    world.emplace<TestCounter>(b, TestCounter{ 2 });
    world.emplace<TestCounter>(c, TestCounter{ 3 });

    world.remove<TestCounter>(b);
    EXPECT_FALSE(world.has<TestCounter>(b));
    ASSERT_NE(world.get<TestCounter>(a), nullptr);
    ASSERT_NE(world.get<TestCounter>(c), nullptr);
    EXPECT_EQ(world.get<TestCounter>(a)->value, 1);
    EXPECT_EQ(world.get<TestCounter>(c)->value, 3);
}

TEST(World, NullEntityNeverAlive)
{
    World world;
    EXPECT_FALSE(world.alive(Entity{}));
    EXPECT_FALSE(world.alive(Entity{ NULL_ENTITY }));
}
