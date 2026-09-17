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

TEST(Material, CreateSolidKeepsSrgbBytesAndTagsSrgb)
{
    AssetManager assets;
    Material     mat;
    ASSERT_TRUE(mat.createSolid(assets, 188, 188, 188));
    ASSERT_TRUE(mat.isValid());
    const uint8_t* px = mat.albedo()->pixels();
    ASSERT_NE(px, nullptr);
    EXPECT_EQ(px[0], 188);
    EXPECT_EQ(px[1], 188);
    EXPECT_EQ(px[2], 188);
    EXPECT_EQ(px[3], 255);
    EXPECT_EQ(mat.albedo()->colorSpace(), Dark::Color::ColorSpace::sRGB);
    EXPECT_FALSE(mat.albedo()->colorSpaceWasDefaulted());
    EXPECT_FLOAT_EQ(mat.baseColor()[0], 1.0f);
    EXPECT_FLOAT_EQ(mat.baseColor()[1], 1.0f);
    EXPECT_FLOAT_EQ(mat.baseColor()[2], 1.0f);
    EXPECT_FLOAT_EQ(mat.baseColor()[3], 1.0f);
}

TEST(Material, SetBaseColorFromSrgb8DecodesRgbIdentityAlpha)
{
    Material mat;
    mat.setBaseColorFromSrgb8(188, 188, 188, 255);
    EXPECT_NEAR(mat.baseColor()[0], 0.5028864580325687, 1.0e-6);
    EXPECT_NEAR(mat.baseColor()[1], 0.5028864580325687, 1.0e-6);
    EXPECT_NEAR(mat.baseColor()[2], 0.5028864580325687, 1.0e-6);
    EXPECT_FLOAT_EQ(mat.baseColor()[3], 1.0f);
}

TEST(Material, SetBaseColorFromSrgb8AlphaIsIdentity)
{
    Material mat;
    mat.setBaseColorFromSrgb8(0, 0, 0, 188);
    EXPECT_FLOAT_EQ(mat.baseColor()[0], 0.0f);
    EXPECT_NEAR(mat.baseColor()[3], 188.0f / 255.0f, 1.0e-7);
}
