#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Assets/Material.h"

#include <memory>

using Dark::AssetManager;
using Dark::Material;
using Dark::MaterialAlphaMode;
using Dark::NULL_ASSET;

TEST(Material, DefaultsAndSetters)
{
    Material mat;
    EXPECT_FLOAT_EQ(mat.metallic(), 0.0f);
    EXPECT_FLOAT_EQ(mat.roughness(), 1.0f);
    EXPECT_EQ(mat.alphaMode(), MaterialAlphaMode::Opaque);
    mat.setMetallicRoughness(0.2f, 0.4f);
    mat.setBaseColor(0.1f, 0.2f, 0.3f, 0.4f);
    mat.setEmissive(1.5f);
    mat.setAlphaMode(MaterialAlphaMode::Blend);
    EXPECT_FLOAT_EQ(mat.metallic(), 0.2f);
    EXPECT_FLOAT_EQ(mat.roughness(), 0.4f);
    EXPECT_FLOAT_EQ(mat.emissive(), 1.5f);
    EXPECT_FLOAT_EQ(mat.baseColor()[3], 0.4f);
    EXPECT_EQ(mat.alphaMode(), MaterialAlphaMode::Blend);
    mat.setEmissive(-2.0f);
    EXPECT_FLOAT_EQ(mat.emissive(), 0.0f);
    EXPECT_FALSE(mat.isValid());
}

TEST(Material, CreateSolidWithoutRenderer)
{
    AssetManager assets;
    Material     mat;
    ASSERT_TRUE(mat.createSolid(assets, 8, 16, 24, 255));
    ASSERT_TRUE(mat.isValid());
    EXPECT_EQ(mat.albedo()->width(), 1u);
}

TEST(Material, CopyFromDuplicatesSurface)
{
    AssetManager assets;
    Material     src;
    ASSERT_TRUE(src.createSolid(assets, 10, 20, 30, 40));
    src.setBaseColor(0.2f, 0.4f, 0.6f, 0.8f);
    src.setMetallicRoughness(0.3f, 0.7f);
    src.setEmissive(2.0f);
    src.setAlphaMode(MaterialAlphaMode::Mask);

    Material dst;
    ASSERT_TRUE(dst.copyFrom(src));
    EXPECT_TRUE(dst.isValid());
    EXPECT_EQ(dst.albedo().get(), src.albedo().get());
    EXPECT_FLOAT_EQ(dst.baseColor()[0], 0.2f);
    EXPECT_FLOAT_EQ(dst.baseColor()[3], 0.8f);
    EXPECT_FLOAT_EQ(dst.metallic(), 0.3f);
    EXPECT_FLOAT_EQ(dst.roughness(), 0.7f);
    EXPECT_FLOAT_EQ(dst.emissive(), 2.0f);
    EXPECT_EQ(dst.alphaMode(), MaterialAlphaMode::Mask);
}

TEST(Material, InternAssignsId)
{
    AssetManager assets;
    auto         mat = std::make_shared<Material>();
    ASSERT_TRUE(mat->createSolid(assets, 1, 2, 3, 4));
    mat = assets.internMaterial(mat);
    ASSERT_TRUE(mat);
    EXPECT_NE(mat->id, NULL_ASSET);
    EXPECT_EQ(mat->sortKey(), mat->id);
}
