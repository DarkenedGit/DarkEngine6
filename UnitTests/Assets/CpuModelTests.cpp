#include <gtest/gtest.h>
#include <memory>

#include "Assets/AssetManager.h"
#include "Assets/GltfLoader.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Assets/MeshData.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"

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
    EXPECT_EQ(model.opaque()[0].mesh.indices.size(), 3u);
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
