#include <gtest/gtest.h>

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Coin.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"

using Dark::CoinComponent;
using Dark::Entity;
using Dark::TagComponent;
using Dark::TransformComponent;
using Dark::World;
using Dark::Math::Vector2f;
using Dark::Math::Vector3f;

namespace
{
    Entity makeCoin(World& world, const Vector2f& pos)
    {
        Entity e = world.createEntity();
        TransformComponent xf{};
        xf.position = Vector3f(pos.x, pos.y, 0.0f);
        world.emplace<TagComponent>(e, "Coin");
        world.emplace<TransformComponent>(e, xf);
        CoinComponent c{};
        c.pos = pos;
        world.emplace<CoinComponent>(e, c);
        return e;
    }
} // namespace

TEST(CoinComponent, EmplaceCollectAndDestroy)
{
    World world;
    Entity e = makeCoin(world, Vector2f{ 3.0f, 4.0f });
    ASSERT_NE(world.get<CoinComponent>(e), nullptr);
    EXPECT_FALSE(world.get<CoinComponent>(e)->collected);
    EXPECT_NEAR(world.get<CoinComponent>(e)->pos.x, 3.0f, 1.0e-5f);
    EXPECT_NEAR(world.get<CoinComponent>(e)->pos.y, 4.0f, 1.0e-5f);
    world.get<CoinComponent>(e)->collected = true;
    EXPECT_TRUE(world.get<CoinComponent>(e)->collected);
    world.destroyEntity(e);
    EXPECT_FALSE(world.has<CoinComponent>(e));
}

TEST(CoinComponent, EachSkipsCollectedAndDestroyLeavesOthers)
{
    World world;
    Entity a = makeCoin(world, Vector2f{ 1.0f, 2.0f });
    Entity b = makeCoin(world, Vector2f{ 9.0f, 8.0f });
    world.get<CoinComponent>(a)->collected = true;

    int seen = 0;
    int live = 0;
    world.each<CoinComponent>([&](Entity e, CoinComponent& c) {
        ++seen;
        if (!c.collected)
            ++live;
        if (e == a)
            EXPECT_TRUE(c.collected);
        if (e == b)
            EXPECT_FALSE(c.collected);
    });
    EXPECT_EQ(seen, 2);
    EXPECT_EQ(live, 1);

    world.destroyEntity(a);
    EXPECT_FALSE(world.has<CoinComponent>(a));
    ASSERT_NE(world.get<CoinComponent>(b), nullptr);
    EXPECT_FALSE(world.get<CoinComponent>(b)->collected);
    EXPECT_NEAR(world.get<CoinComponent>(b)->pos.x, 9.0f, 1.0e-5f);
}
