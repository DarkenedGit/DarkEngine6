#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Assets/Image.h"
#include "Assets/Material.h"

#include <memory>
#include <string>

using Dark::AssetManager;
using Dark::Image;
using Dark::Material;
using Dark::MaterialAlphaMode;
using Dark::NULL_ASSET;
using Dark::materialRecipeKey;

TEST(Material, DefaultsAndSetters)
{
    Material mat;
    EXPECT_FLOAT_EQ(mat.metallic(), 0.0f);
    EXPECT_FLOAT_EQ(mat.roughness(), 1.0f);
    EXPECT_EQ(mat.alphaMode(), MaterialAlphaMode::Opaque);
    EXPECT_FLOAT_EQ(mat.ao(), 1.0f);
    EXPECT_FLOAT_EQ(mat.normalScale(), 1.0f);
    EXPECT_FLOAT_EQ(mat.alphaCutoff(), 0.5f);
    EXPECT_FLOAT_EQ(mat.emissiveColor()[0], 1.0f);
    EXPECT_FLOAT_EQ(mat.emissiveColor()[1], 1.0f);
    EXPECT_FLOAT_EQ(mat.emissiveColor()[2], 1.0f);
    EXPECT_FALSE(mat.normalImage());
    EXPECT_FALSE(mat.ormImage());
    EXPECT_FALSE(mat.emissiveImage());
    mat.setMetallicRoughness(0.2f, 0.4f);
    mat.setBaseColor(0.1f, 0.2f, 0.3f, 0.4f);
    mat.setEmissive(1.5f);
    mat.setAlphaMode(MaterialAlphaMode::Blend);
    mat.setAo(0.25f);
    mat.setNormalScale(2.5f);
    mat.setAlphaCutoff(0.75f);
    mat.setEmissiveColor(0.1f, 0.2f, 0.3f);
    EXPECT_FLOAT_EQ(mat.metallic(), 0.2f);
    EXPECT_FLOAT_EQ(mat.roughness(), 0.4f);
    EXPECT_FLOAT_EQ(mat.emissive(), 1.5f);
    EXPECT_FLOAT_EQ(mat.baseColor()[3], 0.4f);
    EXPECT_EQ(mat.alphaMode(), MaterialAlphaMode::Blend);
    EXPECT_FLOAT_EQ(mat.ao(), 0.25f);
    EXPECT_FLOAT_EQ(mat.normalScale(), 2.5f);
    EXPECT_FLOAT_EQ(mat.alphaCutoff(), 0.75f);
    EXPECT_FLOAT_EQ(mat.emissiveColor()[0], 0.1f);
    EXPECT_FLOAT_EQ(mat.emissiveColor()[1], 0.2f);
    EXPECT_FLOAT_EQ(mat.emissiveColor()[2], 0.3f);
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

TEST(Material, MissingMapsStillValid)
{
    AssetManager assets;
    Material     mat;
    ASSERT_TRUE(mat.createSolid(assets, 8, 16, 24, 255));
    EXPECT_TRUE(mat.isValid());
    EXPECT_FALSE(mat.normalImage());
    EXPECT_FALSE(mat.ormImage());
    EXPECT_FALSE(mat.emissiveImage());
}

TEST(Material, CreateFromAlbedoImageLeavesMapRefsEmpty)
{
    AssetManager assets;
    Material     mat;
    ASSERT_TRUE(mat.createSolid(assets, 1, 2, 3, 255));
    auto nrm = std::make_shared<Image>();
    ASSERT_TRUE(nrm->createSolidColor(128, 128, 255));
    auto orm = std::make_shared<Image>();
    ASSERT_TRUE(orm->createSolidColor(255, 128, 64));
    auto emis = std::make_shared<Image>();
    ASSERT_TRUE(emis->createSolidColor(10, 20, 30));
    mat.setNormalImage(nrm);
    mat.setOrmImage(orm);
    mat.setEmissiveImage(emis);
    ASSERT_TRUE(mat.normalImage());
    ASSERT_TRUE(mat.createSolid(assets, 4, 5, 6, 255));
    EXPECT_TRUE(mat.isValid());
    EXPECT_FALSE(mat.normalImage());
    EXPECT_FALSE(mat.ormImage());
    EXPECT_FALSE(mat.emissiveImage());
}

TEST(Material, CopyFromCopiesMapRefsAndScalars)
{
    AssetManager assets;
    Material     src;
    ASSERT_TRUE(src.createSolid(assets, 10, 20, 30, 40));
    auto nrm = std::make_shared<Image>();
    ASSERT_TRUE(nrm->createSolidColor(128, 128, 255));
    auto orm = std::make_shared<Image>();
    ASSERT_TRUE(orm->createSolidColor(255, 64, 192));
    auto emis = std::make_shared<Image>();
    ASSERT_TRUE(emis->createSolidColor(8, 16, 24));
    src.setNormalImage(nrm);
    src.setOrmImage(orm);
    src.setEmissiveImage(emis);
    src.setAo(0.4f);
    src.setNormalScale(1.75f);
    src.setAlphaCutoff(0.3f);
    src.setEmissiveColor(0.2f, 0.5f, 0.9f);
    src.setEmissive(3.0f);
    src.setMetallicRoughness(0.15f, 0.85f);

    Material dst;
    ASSERT_TRUE(dst.copyFrom(src));
    EXPECT_EQ(dst.normalImage().get(), src.normalImage().get());
    EXPECT_EQ(dst.ormImage().get(), src.ormImage().get());
    EXPECT_EQ(dst.emissiveImage().get(), src.emissiveImage().get());
    EXPECT_FLOAT_EQ(dst.ao(), 0.4f);
    EXPECT_FLOAT_EQ(dst.normalScale(), 1.75f);
    EXPECT_FLOAT_EQ(dst.alphaCutoff(), 0.3f);
    EXPECT_FLOAT_EQ(dst.emissiveColor()[0], 0.2f);
    EXPECT_FLOAT_EQ(dst.emissiveColor()[1], 0.5f);
    EXPECT_FLOAT_EQ(dst.emissiveColor()[2], 0.9f);
    EXPECT_FLOAT_EQ(dst.emissive(), 3.0f);
}

TEST(Material, CopyFromEmptyMapsFailsWithoutAlbedo)
{
    Material src;
    Material dst;
    EXPECT_FALSE(src.isValid());
    EXPECT_FALSE(dst.copyFrom(src));
}

TEST(Material, SetAoAndCutoffClamp)
{
    Material mat;
    mat.setAo(-1.0f);
    EXPECT_FLOAT_EQ(mat.ao(), 0.0f);
    mat.setAo(2.0f);
    EXPECT_FLOAT_EQ(mat.ao(), 1.0f);
    mat.setAo(0.0f);
    EXPECT_FLOAT_EQ(mat.ao(), 0.0f);
    mat.setAo(1.0f);
    EXPECT_FLOAT_EQ(mat.ao(), 1.0f);
    mat.setAlphaCutoff(-0.25f);
    EXPECT_FLOAT_EQ(mat.alphaCutoff(), 0.0f);
    mat.setAlphaCutoff(1.5f);
    EXPECT_FLOAT_EQ(mat.alphaCutoff(), 1.0f);
    mat.setAlphaCutoff(0.0f);
    EXPECT_FLOAT_EQ(mat.alphaCutoff(), 0.0f);
    mat.setNormalScale(4.0f);
    EXPECT_FLOAT_EQ(mat.normalScale(), 4.0f);
    mat.setNormalScale(-2.0f);
    EXPECT_FLOAT_EQ(mat.normalScale(), -2.0f);
}

TEST(Material, RecipeKeyIncludesMapIdsAndEmissiveColor)
{
    AssetManager assets;
    Material     a;
    Material     b;
    ASSERT_TRUE(a.createSolid(assets, 1, 2, 3, 255));
    ASSERT_TRUE(b.createSolid(assets, 1, 2, 3, 255));
    EXPECT_EQ(materialRecipeKey(a), materialRecipeKey(b));

    auto n1 = std::make_shared<Image>();
    ASSERT_TRUE(n1->createSolidColor(128, 128, 255));
    n1 = assets.internImage(n1, "unit:/mat/n1");
    auto n2 = std::make_shared<Image>();
    ASSERT_TRUE(n2->createSolidColor(127, 128, 255));
    n2 = assets.internImage(n2, "unit:/mat/n2");
    ASSERT_TRUE(n1);
    ASSERT_TRUE(n2);
    EXPECT_NE(n1->id, NULL_ASSET);
    EXPECT_NE(n2->id, NULL_ASSET);
    EXPECT_NE(n1->id, n2->id);
    a.setNormalImage(n1);
    b.setNormalImage(n2);
    EXPECT_NE(materialRecipeKey(a), materialRecipeKey(b));

    Material c;
    Material d;
    ASSERT_TRUE(c.createSolid(assets, 1, 2, 3, 255));
    ASSERT_TRUE(d.createSolid(assets, 1, 2, 3, 255));
    c.setEmissiveColor(1.0f, 0.0f, 0.0f);
    d.setEmissiveColor(0.0f, 1.0f, 0.0f);
    EXPECT_NE(materialRecipeKey(c), materialRecipeKey(d));

    auto orm = std::make_shared<Image>();
    ASSERT_TRUE(orm->createSolidColor(32, 64, 192));
    orm = assets.internImage(orm, "unit:/mat/orm");
    auto emis = std::make_shared<Image>();
    ASSERT_TRUE(emis->createSolidColor(9, 8, 7));
    emis = assets.internImage(emis, "unit:/mat/emis");
    c.setOrmImage(orm);
    c.setEmissiveImage(emis);
    const std::string key = materialRecipeKey(c);
    EXPECT_NE(key.find(std::to_string(orm->id)), std::string::npos);
    EXPECT_NE(key.find(std::to_string(emis->id)), std::string::npos);

    Material missing;
    ASSERT_TRUE(missing.createSolid(assets, 9, 9, 9, 255));
    const std::string missingKey = materialRecipeKey(missing);
    EXPECT_NE(missingKey.find(":0:0:0:"), std::string::npos);
}
