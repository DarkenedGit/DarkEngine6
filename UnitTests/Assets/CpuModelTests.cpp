#include <gtest/gtest.h>
#include <memory>

#include "Assets/AssetManager.h"
#include "Assets/GltfLoader.h"
#include "Assets/Image.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Assets/MeshData.h"
#include "Math/Color.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Assets/GltfTestUtil.h"

using Dark::AssetManager;
using Dark::GltfCpuModel;
using Dark::GltfCpuPrimitive;
using Dark::MeshData;
using Dark::Model;
using Dark::NULL_ASSET;
using Dark::materialRecipeKey;

namespace
{
    MeshData makeTri()
    {
        MeshData m;
        m.positions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
        m.normals   = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
        m.uvs       = { { 0, 0 }, { 1, 0 }, { 0, 1 } };
        m.indices   = { 0, 1, 2 };
        return m;
    }
} // namespace

TEST(CpuModel, CreateFromParsedNoRenderer)
{
    AssetManager assets;
    GltfCpuModel cpu;
    GltfCpuPrimitive p;
    p.mesh        = makeTri();
    p.baseColor[0] = 0.2f;
    p.baseColor[1] = 0.4f;
    p.baseColor[2] = 0.6f;
    p.baseColor[3] = 1.0f;
    p.metallic     = 0.1f;
    p.roughness    = 0.5f;
    cpu.primitives.push_back(p);

    Model model;
    ASSERT_TRUE(model.createFromParsed(assets, cpu, "unit:/tri"));
    ASSERT_EQ(model.opaque().size(), 1u);
    ASSERT_TRUE(model.opaque()[0].material);
    EXPECT_NE(model.opaque()[0].material->id, NULL_ASSET);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->metallic(), 0.1f);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->roughness(), 0.5f);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->baseColor()[0], 0.2f);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->baseColor()[1], 0.4f);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->baseColor()[2], 0.6f);
    EXPECT_EQ(model.opaque()[0].mesh.indices.size(), 3u);
    EXPECT_FALSE(model.opaque()[0].material->normalImage());
    EXPECT_FALSE(model.opaque()[0].material->ormImage());
    EXPECT_FALSE(model.opaque()[0].material->emissiveImage());
    EXPECT_FLOAT_EQ(model.opaque()[0].material->ao(), 1.0f);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->normalScale(), 1.0f);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->alphaCutoff(), 0.5f);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->emissive(), 0.0f);
}

TEST(CpuModel, GltfSolid_WhiteImageLinearTint)
{
    AssetManager assets;
    GltfCpuModel cpu;
    GltfCpuPrimitive p;
    p.mesh         = makeTri();
    p.baseColor[0] = 0.5f;
    p.baseColor[1] = 0.25f;
    p.baseColor[2] = 0.125f;
    p.baseColor[3] = 1.0f;
    cpu.primitives.push_back(p);

    Model model;
    ASSERT_TRUE(model.createFromParsed(assets, cpu, "unit:/solid-tint"));
    ASSERT_EQ(model.opaque().size(), 1u);
    ASSERT_TRUE(model.opaque()[0].material);
    ASSERT_TRUE(model.opaque()[0].material->albedo());
    EXPECT_EQ(model.opaque()[0].material->albedo()->width(), 1u);
    EXPECT_EQ(model.opaque()[0].material->albedo()->height(), 1u);
    const uint8_t* px = model.opaque()[0].material->albedo()->pixels();
    ASSERT_NE(px, nullptr);
    EXPECT_EQ(px[0], 255);
    EXPECT_EQ(px[1], 255);
    EXPECT_EQ(px[2], 255);
    EXPECT_EQ(px[3], 255);
    EXPECT_NE(px[0], 127);
    EXPECT_NE(px[1], 63);
    EXPECT_NE(px[2], 31);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->baseColor()[0], 0.5f);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->baseColor()[1], 0.25f);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->baseColor()[2], 0.125f);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->baseColor()[3], 1.0f);
    EXPECT_FALSE(model.opaque()[0].material->normalImage());
    EXPECT_FALSE(model.opaque()[0].material->ormImage());
    EXPECT_FALSE(model.opaque()[0].material->emissiveImage());
    EXPECT_EQ(model.opaque()[0].material->albedo()->colorSpace(), Dark::Color::ColorSpace::sRGB);
}

TEST(CpuModel, GltfBadAlbedoBytesFallsBackToWhiteLinearTint)
{
    AssetManager assets;
    GltfCpuModel cpu;
    GltfCpuPrimitive p;
    p.mesh         = makeTri();
    p.albedo.bytes      = { 0, 1, 2, 3 };
    p.albedo.imageIndex = 0;
    p.baseColor[0] = 0.5f;
    p.baseColor[1] = 0.25f;
    p.baseColor[2] = 0.125f;
    p.baseColor[3] = 1.0f;
    cpu.primitives.push_back(p);

    Model model;
    ASSERT_TRUE(model.createFromParsed(assets, cpu, "unit:/bad-bytes"));
    ASSERT_EQ(model.opaque().size(), 1u);
    ASSERT_TRUE(model.opaque()[0].material);
    ASSERT_TRUE(model.opaque()[0].material->albedo());
    const uint8_t* px = model.opaque()[0].material->albedo()->pixels();
    ASSERT_NE(px, nullptr);
    EXPECT_EQ(px[0], 255);
    EXPECT_EQ(px[1], 255);
    EXPECT_EQ(px[2], 255);
    EXPECT_EQ(px[3], 255);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->baseColor()[0], 0.5f);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->baseColor()[1], 0.25f);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->baseColor()[2], 0.125f);
}

TEST(CpuModel, SameAlbedoDifferentMetallicInternsTwoMaterials)
{
    AssetManager assets;
    GltfCpuModel cpu;
    GltfCpuPrimitive a;
    a.mesh      = makeTri();
    a.metallic  = 0.0f;
    a.roughness = 1.0f;
    GltfCpuPrimitive b = a;
    b.metallic         = 1.0f;
    cpu.primitives.push_back(a);
    cpu.primitives.push_back(b);

    Model model;
    ASSERT_TRUE(model.createFromParsed(assets, cpu, "unit:/two"));
    ASSERT_EQ(model.opaque().size(), 2u);
    ASSERT_TRUE(model.opaque()[0].material);
    ASSERT_TRUE(model.opaque()[1].material);
    EXPECT_NE(model.opaque()[0].material->id, model.opaque()[1].material->id);
    ASSERT_TRUE(model.opaque()[0].material->albedo());
    ASSERT_TRUE(model.opaque()[1].material->albedo());
    EXPECT_EQ(model.opaque()[0].material->albedo()->id, model.opaque()[1].material->albedo()->id);
    EXPECT_NE(materialRecipeKey(*model.opaque()[0].material), materialRecipeKey(*model.opaque()[1].material));
}

TEST(CpuModel, CreateFromParts)
{
    auto mat = std::make_shared<Dark::Material>();
    Model::Part part;
    part.mesh        = makeTri();
    part.material    = mat;
    part.name        = "Tri";
    part.localToRoot = Dark::Math::Matrix4f::TranslationMatrix(10.0f, 0.0f, 0.0f);

    Model model;
    ASSERT_TRUE(model.createFromParts({ part }));
    ASSERT_EQ(model.opaque().size(), 1u);
    EXPECT_EQ(model.opaque()[0].name, "Tri");
    EXPECT_TRUE(model.bounds().Contains(Dark::Math::Vector3f{ 10.0f, 0.0f, 0.0f }));
    EXPECT_TRUE(model.bounds().Contains(Dark::Math::Vector3f{ 11.0f, 0.0f, 0.0f }));
    EXPECT_TRUE(model.bounds().Contains(Dark::Math::Vector3f{ 10.0f, 1.0f, 0.0f }));
}

TEST(CpuModel, CreateFromPartsRejectsEmpty)
{
    Model model;
    Model::Part empty;
    EXPECT_FALSE(model.createFromParts({ empty }));
    EXPECT_FALSE(model.valid());
}

TEST(CpuModel, InternProceduralModel)
{
    AssetManager assets;
    auto mat = std::make_shared<Dark::Material>();
    ASSERT_TRUE(mat->createSolid(assets, 10, 20, 30));
    Dark::AssetRef<Model> model = Dark::internProceduralModel(assets, makeTri(), mat, "runtime:/unit/tri");
    ASSERT_TRUE(model);
    EXPECT_NE(model->id, NULL_ASSET);
    ASSERT_EQ(model->opaque().size(), 1u);
    ASSERT_TRUE(model->opaque()[0].material);
    EXPECT_NE(model->opaque()[0].material->id, NULL_ASSET);
}

TEST(CpuModel, PacksOrmAndTagsMapColorSpaces)
{
    const auto albedoPath = Dark::GltfTest::writeTempBmp("cpu_albedo.bmp", 10, 20, 30, 255);
    const auto mrPath     = Dark::GltfTest::writeTempBmp("cpu_mr.bmp", 1, 64, 192, 255);
    const auto occPath    = Dark::GltfTest::writeTempBmp("cpu_occ.bmp", 32, 0, 0, 255);
    const auto nrmPath    = Dark::GltfTest::writeTempBmp("cpu_nrm.bmp", 128, 128, 255, 255);
    const auto emisPath   = Dark::GltfTest::writeTempBmp("cpu_emis.bmp", 255, 0, 0, 255);

    AssetManager assets;
    GltfCpuModel cpu;
    GltfCpuPrimitive p;
    p.mesh               = makeTri();
    p.materialIndex      = 0;
    p.albedo.file        = albedoPath;
    p.albedo.imageIndex  = 0;
    p.metallicRoughness.file       = mrPath;
    p.metallicRoughness.imageIndex = 1;
    p.occlusion.file               = occPath;
    p.occlusion.imageIndex         = 2;
    p.normal.file                  = nrmPath;
    p.normal.imageIndex            = 3;
    p.emissive.file                = emisPath;
    p.emissive.imageIndex          = 4;
    p.normalScale    = 1.5f;
    p.ao             = 0.7f;
    p.alphaCutoff    = 0.3f;
    p.emissiveColor[0] = 0.2f;
    p.emissiveColor[1] = 0.4f;
    p.emissiveColor[2] = 0.6f;
    cpu.primitives.push_back(p);

    Model model;
    ASSERT_TRUE(model.createFromParsed(assets, cpu, "unit:/maps"));
    ASSERT_EQ(model.opaque().size(), 1u);
    const auto& mat = model.opaque()[0].material;
    ASSERT_TRUE(mat);
    ASSERT_TRUE(mat->albedo());
    ASSERT_TRUE(mat->normalImage());
    ASSERT_TRUE(mat->ormImage());
    ASSERT_TRUE(mat->emissiveImage());
    EXPECT_EQ(mat->albedo()->colorSpace(), Dark::Color::ColorSpace::sRGB);
    EXPECT_EQ(mat->normalImage()->colorSpace(), Dark::Color::ColorSpace::Linear);
    EXPECT_EQ(mat->ormImage()->colorSpace(), Dark::Color::ColorSpace::Linear);
    EXPECT_EQ(mat->emissiveImage()->colorSpace(), Dark::Color::ColorSpace::sRGB);
    EXPECT_NE(mat->ormImage()->id, mat->albedo()->id);
    EXPECT_NE(mat->ormImage()->id, mat->normalImage()->id);
    const auto mrImg = assets.loadImageFile(mrPath);
    ASSERT_TRUE(mrImg);
    EXPECT_NE(mat->ormImage()->id, mrImg->id);
    const uint8_t* orm = mat->ormImage()->pixels();
    ASSERT_NE(orm, nullptr);
    EXPECT_EQ(orm[0], 32);
    EXPECT_EQ(orm[1], 64);
    EXPECT_EQ(orm[2], 192);
    EXPECT_EQ(orm[3], 255);
    EXPECT_FLOAT_EQ(mat->normalScale(), 1.5f);
    EXPECT_FLOAT_EQ(mat->ao(), 0.7f);
    EXPECT_FLOAT_EQ(mat->alphaCutoff(), 0.3f);
    EXPECT_FLOAT_EQ(mat->emissive(), 1.0f);
    EXPECT_FLOAT_EQ(mat->emissiveColor()[0], 0.2f);
    EXPECT_FLOAT_EQ(mat->emissiveColor()[1], 0.4f);
    EXPECT_FLOAT_EQ(mat->emissiveColor()[2], 0.6f);
}

TEST(CpuModel, OrmInternedEvenWhenOccEqualsMr)
{
    const auto mrPath = Dark::GltfTest::writeTempBmp("cpu_same_orm.bmp", 11, 22, 33, 44);

    AssetManager assets;
    GltfCpuModel cpu;
    GltfCpuPrimitive p;
    p.mesh                         = makeTri();
    p.materialIndex                = 3;
    p.metallicRoughness.file       = mrPath;
    p.metallicRoughness.imageIndex = 0;
    p.occlusion.file               = mrPath;
    p.occlusion.imageIndex         = 0;
    cpu.primitives.push_back(p);

    Model model;
    ASSERT_TRUE(model.createFromParsed(assets, cpu, "unit:/same-orm"));
    ASSERT_EQ(model.opaque().size(), 1u);
    const auto& mat = model.opaque()[0].material;
    ASSERT_TRUE(mat);
    ASSERT_TRUE(mat->ormImage());
    const auto mrImg = assets.loadImageFile(mrPath);
    ASSERT_TRUE(mrImg);
    EXPECT_NE(mat->ormImage()->id, mrImg->id);
    const uint8_t* srcPx = mrImg->pixels();
    const uint8_t* px    = mat->ormImage()->pixels();
    ASSERT_NE(srcPx, nullptr);
    ASSERT_NE(px, nullptr);
    EXPECT_EQ(px[0], srcPx[0]);
    EXPECT_EQ(px[1], srcPx[1]);
    EXPECT_EQ(px[2], srcPx[2]);
    EXPECT_EQ(px[3], srcPx[3]);
}

TEST(CpuModel, EmissiveScalarFromExplicitWhiteFactor)
{
    AssetManager assets;
    GltfCpuModel cpu;
    GltfCpuPrimitive p;
    p.mesh             = makeTri();
    p.emissiveColor[0] = 1.0f;
    p.emissiveColor[1] = 1.0f;
    p.emissiveColor[2] = 1.0f;
    p.emissiveScalar   = 1.0f;
    cpu.primitives.push_back(p);

    Model model;
    ASSERT_TRUE(model.createFromParsed(assets, cpu, "unit:/emis-white"));
    ASSERT_TRUE(model.opaque()[0].material);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->emissive(), 1.0f);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->emissiveColor()[0], 1.0f);
}

TEST(CpuModel, EmissiveScalarFromTextureEvenIfFactorZero)
{
    const auto emisPath = Dark::GltfTest::writeTempBmp("cpu_emis_only.bmp", 8, 16, 24, 255);

    AssetManager assets;
    GltfCpuModel cpu;
    GltfCpuPrimitive p;
    p.mesh                = makeTri();
    p.emissive.file       = emisPath;
    p.emissive.imageIndex = 0;
    p.emissiveColor[0]    = 0.0f;
    p.emissiveColor[1]    = 0.0f;
    p.emissiveColor[2]    = 0.0f;
    cpu.primitives.push_back(p);

    Model model;
    ASSERT_TRUE(model.createFromParsed(assets, cpu, "unit:/emis-tex"));
    ASSERT_TRUE(model.opaque()[0].material);
    EXPECT_FLOAT_EQ(model.opaque()[0].material->emissive(), 1.0f);
    ASSERT_TRUE(model.opaque()[0].material->emissiveImage());
}

TEST(CpuModel, MissingOptionalMapFilesSkipThoseSlots)
{
    AssetManager assets;
    GltfCpuModel cpu;
    GltfCpuPrimitive p;
    p.mesh        = makeTri();
    p.normal.file = "Z:/definitely/missing/normal.bmp";
    p.metallicRoughness.file = "Z:/definitely/missing/mr.bmp";
    p.occlusion.file         = "Z:/definitely/missing/occ.bmp";
    p.emissive.file          = "Z:/definitely/missing/emis.bmp";
    cpu.primitives.push_back(p);

    Model model;
    ASSERT_TRUE(model.createFromParsed(assets, cpu, "unit:/missing-maps"));
    ASSERT_EQ(model.opaque().size(), 1u);
    ASSERT_TRUE(model.opaque()[0].material);
    EXPECT_TRUE(model.opaque()[0].material->albedo());
    EXPECT_FALSE(model.opaque()[0].material->normalImage());
    EXPECT_FALSE(model.opaque()[0].material->ormImage());
    EXPECT_FALSE(model.opaque()[0].material->emissiveImage());
}
