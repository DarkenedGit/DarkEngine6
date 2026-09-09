#include <gtest/gtest.h>

#include "Assets/AssetHandle.h"
#include "Assets/AssetManager.h"

using namespace Dark;

namespace
{
    struct DummyAsset : Asset
    {
        DummyAsset()
        {
            type = AssetType::Mesh;
        }
    };
}

TEST(AssetManager, RegisterAndGet)
{
    AssetManager mgr;
    const AssetID id = mgr.registerAsset(std::make_shared<DummyAsset>());
    EXPECT_NE(id, NULL_ASSET);
    EXPECT_EQ(mgr.assetCount(), 1u);
    EXPECT_TRUE(mgr.get(id));
    EXPECT_EQ(mgr.get(id)->id, id);
}

TEST(AssetManager, UnloadScrubsPathMapping)
{
    AssetManager mgr;
    const std::string key = "unit:/models/cube.glb";
    const AssetID id = mgr.registerAsset(std::make_shared<DummyAsset>(), key);
    ASSERT_NE(id, NULL_ASSET);
    EXPECT_EQ(mgr.pathMappingCount(), 1u);

    mgr.unload(id);
    EXPECT_FALSE(mgr.get(id));
    EXPECT_EQ(mgr.assetCount(), 0u);
    EXPECT_EQ(mgr.pathMappingCount(), 0u);

    const AssetID id2 = mgr.registerAsset(std::make_shared<DummyAsset>(), key);
    EXPECT_NE(id2, NULL_ASSET);
    EXPECT_NE(id2, id);
    EXPECT_EQ(mgr.pathMappingCount(), 1u);
    EXPECT_TRUE(mgr.get(id2));
}

TEST(AssetManager, CollectGarbageScrubsPathMapping)
{
    AssetManager mgr;
    const std::string key = "unit:/models/orphan.glb";
    AssetID id = NULL_ASSET;
    {
        auto asset = std::make_shared<DummyAsset>();
        id         = mgr.registerAsset(asset, key);
        ASSERT_NE(id, NULL_ASSET);
        EXPECT_EQ(mgr.pathMappingCount(), 1u);
    } // drop external ref — only manager holds the asset

    mgr.collectGarbage();
    EXPECT_FALSE(mgr.get(id));
    EXPECT_EQ(mgr.assetCount(), 0u);
    EXPECT_EQ(mgr.pathMappingCount(), 0u);
}

TEST(AssetManager, CollectGarbageKeepsExternallyHeld)
{
    AssetManager mgr;
    const std::string key = "unit:/models/kept.glb";
    auto asset = std::make_shared<DummyAsset>();
    const AssetID id = mgr.registerAsset(asset, key);
    ASSERT_NE(id, NULL_ASSET);

    mgr.collectGarbage();
    EXPECT_TRUE(mgr.get(id));
    EXPECT_EQ(mgr.assetCount(), 1u);
    EXPECT_EQ(mgr.pathMappingCount(), 1u);

    asset.reset();
    mgr.collectGarbage();
    EXPECT_FALSE(mgr.get(id));
    EXPECT_EQ(mgr.pathMappingCount(), 0u);
}

TEST(AssetManager, NullRegisterRejected)
{
    AssetManager mgr;
    EXPECT_EQ(mgr.registerAsset({}), NULL_ASSET);
    EXPECT_EQ(mgr.assetCount(), 0u);
    EXPECT_EQ(mgr.pathMappingCount(), 0u);
}
