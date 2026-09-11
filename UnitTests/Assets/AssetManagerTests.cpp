#include <gtest/gtest.h>

#include "Animation/AnimationSet.h"
#include "Assets/AssetHandle.h"
#include "Assets/AssetManager.h"
#include "Assets/GltfTestUtil.h"
#include "Assets/Model.h"

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

TEST(AssetManager, LoadAnimationSetInternsSamePath)
{
    const auto path = GltfTest::writeSkinnedGltf("intern_set.gltf");
    AssetManager mgr;
    mgr.mountDirectory(path.parent_path());

    auto a = mgr.loadAnimationSet(path.filename().string());
    ASSERT_TRUE(a);
    EXPECT_EQ(a->type, AssetType::AnimationSet);
    EXPECT_EQ(a->clipCount(), 1u);
    EXPECT_TRUE(a->findClip("Walk"));
    EXPECT_EQ(a->jointNames().size(), 2u);

    auto b = mgr.loadAnimationSet(path.filename().string());
    ASSERT_TRUE(b);
    EXPECT_EQ(a.get(), b.get());
    EXPECT_EQ(a->id, b->id);
}

TEST(AssetManager, LoadAnimationSetStaticMeshReturnsEmpty)
{
    const auto path = GltfTest::writeTempGltf("static_cube.gltf", std::string(R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [{ "nodes": [0] }],
  "nodes": [{ "mesh": 0 }],
  "meshes": [{ "primitives": [{ "attributes": { "POSITION": 0 }, "indices": 1 }] }],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 8, "type": "VEC3", "min": [-0.5,-0.5,-0.5], "max": [0.5,0.5,0.5] },
    { "bufferView": 1, "componentType": 5123, "count": 36, "type": "SCALAR" }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 96 },
    { "buffer": 0, "byteOffset": 96, "byteLength": 72 }
  ],
  "buffers": [{ "byteLength": 168, "uri": "data:application/octet-stream;base64,AAAAvwAAAL8AAAC/AAAAPwAAAL8AAAC/AAAAPwAAAD8AAAC/AAAAvwAAAD8AAAC/AAAAvwAAAL8AAAA/AAAAPwAAAL8AAAA/AAAAPwAAAD8AAAA/AAAAvwAAAD8AAAA/AAABAAIAAAACAAMAAQAFAAYAAQAGAAIABQAEAAcABQAHAAYABAAAAAMABAADAAcAAwACAAYAAwAGAAcABAAFAAEABAABAAAA" }]
})"));
    AssetManager mgr;
    mgr.mountDirectory(path.parent_path());
    auto set = mgr.loadAnimationSet(path.filename().string());
    EXPECT_FALSE(set);
}

TEST(AssetManager, ModelHoldsAnimationSetAcrossGc)
{
    const auto path = GltfTest::writeSkinnedGltf("gc_set.gltf");
    AssetManager mgr;
    mgr.mountDirectory(path.parent_path());

    auto set = mgr.loadAnimationSet(path.filename().string());
    ASSERT_TRUE(set);
    const AssetID setId = set->id;

    auto model = std::make_shared<Model>();
    model->setAnimationSet(set);
    const AssetID modelId = mgr.registerAsset(model, "unit:/models/gc_holder.glb");
    ASSERT_NE(modelId, NULL_ASSET);

    set.reset();
    mgr.collectGarbage();
    EXPECT_TRUE(mgr.get(setId));
    EXPECT_TRUE(mgr.get(modelId));
    EXPECT_TRUE(model->animationSet());

    model.reset();
    mgr.collectGarbage();
    mgr.collectGarbage();
    EXPECT_FALSE(mgr.get(setId));
    EXPECT_FALSE(mgr.get(modelId));
}
