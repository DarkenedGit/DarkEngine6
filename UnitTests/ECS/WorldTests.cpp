#include <gtest/gtest.h>

#include "ECS/Components.h"
#include "ECS/FrameEntitySet.h"
#include "ECS/PoolIterate.h"
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

TEST(FrameEntitySet, ClearAndGenerationDoNotAlias)
{
    FrameEntitySet set;
    const EntityID first = makeEntityID(3, 1);
    const EntityID recycled = makeEntityID(3, 2);
    EXPECT_FALSE(set.contains(NULL_ENTITY));
    set.insert(NULL_ENTITY);
    EXPECT_FALSE(set.contains(NULL_ENTITY));

    set.insert(first);
    EXPECT_TRUE(set.contains(first));
    EXPECT_FALSE(set.contains(recycled));

    set.clear();
    EXPECT_FALSE(set.contains(first));
    set.insert(recycled);
    EXPECT_TRUE(set.contains(recycled));
    EXPECT_FALSE(set.contains(first));
}

TEST(World, EachNotesAMutationAndNestedReadsAreFine)
{
    poolIterateTrap() = false;
    const uint32_t before = poolIterateViolations();
    World world;
    const Entity a = world.createEntity();
    const Entity b = world.createEntity();
    world.emplace<TagComponent>(a, "A");
    world.emplace<TagComponent>(b, "B");
    world.each<TagComponent>([&](Entity e, TagComponent&) { world.emplace<TagComponent>(e, "Replaced"); });
    EXPECT_GT(poolIterateViolations(), before);
    const uint32_t afterReplace = poolIterateViolations();
    world.each<TagComponent>([&](Entity, TagComponent&) { world.remove<TagComponent>(b); });
    EXPECT_GT(poolIterateViolations(), afterReplace);
    const uint32_t afterRemove = poolIterateViolations();
    world.each<TagComponent>([&](Entity e, TagComponent&) {
        world.each<TagComponent>([&](Entity, TagComponent&) {});
        world.emplace<TransformComponent>(e);
    });
    EXPECT_EQ(poolIterateViolations(), afterRemove);
    EXPECT_NE(world.get<TransformComponent>(a), nullptr);
    poolIterateTrap() = true;
}

TEST(World, SceneGenerationStartsAtOneAndBumps)
{
    World world;
    EXPECT_EQ(world.generation(), 1u);
    world.bumpGeneration();
    EXPECT_EQ(world.generation(), 2u);
}

TEST(World, MeshComponentDefaultsPrimitiveNone)
{
    MeshComponent mc{};
    EXPECT_EQ(mc.primitive, PrimitiveMesh::None);
    EXPECT_EQ(mc.meshAssetID, NULL_ASSET);
    EXPECT_EQ(mc.matAssetID, NULL_ASSET);
}
