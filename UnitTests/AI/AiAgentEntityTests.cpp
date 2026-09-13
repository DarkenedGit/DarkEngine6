#include <gtest/gtest.h>

#include <memory>

#include "AI/AiSystem.h"
#include "Assets/AssetManager.h"
#include "Assets/Material.h"
#include "Character/HealthComponent.h"
#include "Core/AssetPinTable.h"
#include "ECS/World.h"
#include "Weapons/HittableComponent.h"

using Dark::AiAgentComponent;
using Dark::AiSystem;
using Dark::AssetManager;
using Dark::AssetPinTable;
using Dark::BrainComponent;
using Dark::Entity;
using Dark::HealthComponent;
using Dark::HittableComponent;
using Dark::Material;
using Dark::TransformComponent;
using Dark::World;

TEST(AiAgentEntity, SpawnHasBrainHealthHittable)
{
    World          world;
    AssetManager   assets;
    AssetPinTable  pins;
    AiSystem       ai;
    auto           mat = std::make_shared<Material>();
    ASSERT_TRUE(mat->createSolid(assets, 220, 90, 40));
    ASSERT_NE(assets.registerAsset(mat), Dark::NULL_ASSET);

    TransformComponent xf{};
    xf.position = Dark::Math::Vector3f{ 1.0f, 2.0f, 3.0f };
    xf.scale    = Dark::Math::Vector3f{ 2.0f, 2.0f, 2.0f };
    Entity e    = ai.spawnHunter(world, pins, assets, mat, xf);
    ASSERT_TRUE(e.valid());
    ASSERT_TRUE(world.has<HealthComponent>(e));
    ASSERT_TRUE(world.has<HittableComponent>(e));
    ASSERT_TRUE(world.has<AiAgentComponent>(e));
    ASSERT_TRUE(world.has<BrainComponent>(e));
    ASSERT_NE(world.get<BrainComponent>(e)->brain, nullptr);
    EXPECT_TRUE(world.get<HealthComponent>(e)->health.alive());
}

TEST(AiAgentEntity, DamageThenDestroyFreesBrain)
{
    World          world;
    AssetManager   assets;
    AssetPinTable  pins;
    AiSystem       ai;
    auto           mat = std::make_shared<Material>();
    ASSERT_TRUE(mat->createSolid(assets, 220, 90, 40));
    assets.registerAsset(mat);

    TransformComponent xf{};
    Entity e = ai.spawnHunter(world, pins, assets, mat, xf);
    ASSERT_TRUE(e.valid());
    EXPECT_TRUE(ai.applyHunterDamage(world, e, 200.0f));
    EXPECT_TRUE(world.get<HealthComponent>(e)->health.dead());
    world.destroyEntity(e);
    EXPECT_FALSE(world.alive(e));
    EXPECT_FALSE(world.has<BrainComponent>(e));
}
