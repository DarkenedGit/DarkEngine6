#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Assets/Material.h"
#include "Core/AssetPinTable.h"
#include "Core/EntityPins.h"
#include "ECS/Components.h"
#include "ECS/World.h"

using Dark::AssetID;
using Dark::AssetManager;
using Dark::AssetPinTable;
using Dark::Entity;
using Dark::Material;
using Dark::MeshComponent;
using Dark::NULL_ASSET;
using Dark::World;
using Dark::onEntityRemoved;
using Dark::setMeshComponent;

namespace
{
    AssetID internSolid(AssetManager& assets, uint8_t r, uint8_t g, uint8_t b)
    {
        auto mat = std::make_shared<Material>();
        if (!mat->createSolid(assets, r, g, b))
            return NULL_ASSET;
        return assets.registerAsset(mat);
    }
} // namespace

TEST(AssetPinTable, PinHoldsThroughCollectGarbage)
{
    AssetManager assets;
    AssetPinTable pins;
    const AssetID id = internSolid(assets, 10, 20, 30);
    ASSERT_NE(id, NULL_ASSET);

    pins.pin(assets, id);
    EXPECT_TRUE(pins.get(id) != nullptr);
    assets.collectGarbage();
    EXPECT_TRUE(assets.get(id) != nullptr);

    pins.unpin(id);
    EXPECT_TRUE(pins.get(id) == nullptr);
    assets.collectGarbage();
    EXPECT_TRUE(assets.get(id) == nullptr);
}

TEST(AssetPinTable, SharedPinRefcount)
{
    AssetManager assets;
    AssetPinTable pins;
    const AssetID id = internSolid(assets, 1, 2, 3);
    ASSERT_NE(id, NULL_ASSET);

    pins.pin(assets, id);
    pins.pin(assets, id);
    pins.unpin(id);
    assets.collectGarbage();
    EXPECT_TRUE(assets.get(id) != nullptr);
    pins.unpin(id);
    assets.collectGarbage();
    EXPECT_TRUE(assets.get(id) == nullptr);
}

TEST(AssetPinTable, NullAndMissingAreNoOps)
{
    AssetManager assets;
    AssetPinTable pins;
    pins.pin(assets, NULL_ASSET);
    pins.unpin(NULL_ASSET);
    pins.pin(assets, 999999);
    EXPECT_TRUE(pins.get(999999) == nullptr);
}

TEST(EntityPins, SetMeshComponentReplaceDoesNotLeak)
{
    AssetManager assets;
    AssetPinTable pins;
    World         world;
    const AssetID a = internSolid(assets, 8, 8, 8);
    const AssetID b = internSolid(assets, 9, 9, 9);
    ASSERT_NE(a, NULL_ASSET);
    ASSERT_NE(b, NULL_ASSET);

    Entity e = world.createEntity();
    MeshComponent mc{};
    mc.matAssetID = a;
    setMeshComponent(world, pins, assets, e, mc);

    mc.matAssetID = b;
    setMeshComponent(world, pins, assets, e, mc);
    assets.collectGarbage();
    EXPECT_TRUE(assets.get(a) == nullptr);
    EXPECT_TRUE(assets.get(b) != nullptr);
}

TEST(EntityPins, OnEntityRemovedUnpins)
{
    AssetManager assets;
    AssetPinTable pins;
    World         world;
    const AssetID id = internSolid(assets, 4, 5, 6);
    ASSERT_NE(id, NULL_ASSET);

    Entity e = world.createEntity();
    MeshComponent mc{};
    mc.matAssetID = id;
    setMeshComponent(world, pins, assets, e, mc);

    onEntityRemoved(world, e, &pins);
    world.destroyEntity(e);
    assets.collectGarbage();
    EXPECT_TRUE(assets.get(id) == nullptr);
}

TEST(EntityPins, OnEntityRemovedNullPinsIsNoOp)
{
    World world;
    Entity e = world.createEntity();
    world.emplace<MeshComponent>(e);
    onEntityRemoved(world, e, nullptr);
    EXPECT_TRUE(world.has<MeshComponent>(e));
}
