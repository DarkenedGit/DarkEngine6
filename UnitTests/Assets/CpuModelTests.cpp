#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Assets/GltfLoader.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Render/MeshData.h"

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
