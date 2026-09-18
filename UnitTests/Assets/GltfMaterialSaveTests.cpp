#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Assets/GltfLoader.h"
#include "Assets/GltfMaterialSave.h"
#include "Assets/Image.h"
#include "Assets/Material.h"
#include "Assets/MeshData.h"
#include "Assets/Model.h"
#include "Core/ContentRoots.h"

#include "third_party/nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using json = nlohmann::json;
using namespace Dark;

namespace
{

std::filesystem::path findContentFile(const char* relativeUnderContent)
{
    for (const auto& root : contentRootCandidates())
    {
        std::error_code ec;
        const auto      p = root / relativeUnderContent;
        if (std::filesystem::exists(p, ec) && !ec)
            return p;
    }
    return {};
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

MeshData makeTri()
{
    MeshData m;
    m.positions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    m.normals   = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
    m.uvs       = { { 0, 0 }, { 1, 0 }, { 0, 1 } };
    m.indices   = { 0, 1, 2 };
    return m;
}

bool makeIndexedModel(AssetManager& assets, Model& model, int materialIndex = 0)
{
    GltfCpuModel cpu;
    GltfCpuPrimitive p;
    p.mesh          = makeTri();
    p.materialIndex = materialIndex;
    cpu.primitives.push_back(p);
    return model.createFromParsed(assets, cpu, "unit:/save-tri");
}

constexpr const char* kTexturedMatJson = R"({
  "asset": { "version": "2.0" },
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorFactor": [1, 1, 1, 1],
      "metallicFactor": 0,
      "roughnessFactor": 1,
      "baseColorTexture": { "index": 0 },
      "metallicRoughnessTexture": { "index": 1 }
    },
    "normalTexture": { "index": 2, "scale": 1 },
    "occlusionTexture": { "index": 3, "strength": 1 },
    "emissiveTexture": { "index": 4 },
    "emissiveFactor": [0, 0, 0]
  }],
  "textures": [
    { "source": 0 }, { "source": 1 }, { "source": 2 }, { "source": 3 }, { "source": 4 }
  ],
  "images": [
    { "uri": "albedo.png" },
    { "uri": "mr.png" },
    { "uri": "normal.png" },
    { "uri": "occ.png" },
    { "uri": "emis.png" }
  ]
})";

} // namespace

TEST(GltfMaterialSave, PatchWritesBaseColorAndPbr)
{
    const auto src = findContentFile("models/unit_cube.gltf");
    ASSERT_FALSE(src.empty());

    AssetManager assets;
    auto         model = std::make_shared<Model>();
    ASSERT_TRUE(model->createFromFile(assets, src));
    ASSERT_GE(model->partCount(), 1u);
    const Model::Part* part = model->partAt(0);
    ASSERT_NE(part, nullptr);
    ASSERT_TRUE(part->material);
    EXPECT_EQ(part->materialIndex, 0);
    part->material->setBaseColor(0.1f, 0.2f, 0.3f, 1.0f);
    part->material->setMetallicRoughness(0.4f, 0.5f);
    part->material->setAlphaMode(MaterialAlphaMode::Blend);

    std::string patched;
    ASSERT_TRUE(patchGltfMaterialsJson(readText(src).c_str(), *model, patched, nullptr));
    const json root = json::parse(patched, nullptr, false);
    ASSERT_FALSE(root.is_discarded());
    ASSERT_TRUE(root.contains("materials"));
    const json& mat = root["materials"][0];
    ASSERT_TRUE(mat["pbrMetallicRoughness"]["baseColorFactor"].is_array());
    EXPECT_NEAR(mat["pbrMetallicRoughness"]["baseColorFactor"][0].get<float>(), 0.1f, 1.0e-5f);
    EXPECT_NEAR(mat["pbrMetallicRoughness"]["metallicFactor"].get<float>(), 0.4f, 1.0e-5f);
    EXPECT_NEAR(mat["pbrMetallicRoughness"]["roughnessFactor"].get<float>(), 0.5f, 1.0e-5f);
    EXPECT_EQ(mat["alphaMode"].get<std::string>(), "BLEND");
}

TEST(GltfMaterialSave, RoundTripFile)
{
    const auto src = findContentFile("models/unit_cube.gltf");
    ASSERT_FALSE(src.empty());

    const auto dest = std::filesystem::temp_directory_path() / "darkengine6_mat_save.gltf";
    std::error_code ec;
    std::filesystem::copy_file(src, dest, std::filesystem::copy_options::overwrite_existing, ec);
    ASSERT_FALSE(ec);

    AssetManager assets;
    auto         model = std::make_shared<Model>();
    ASSERT_TRUE(model->createFromFile(assets, dest));
    ASSERT_NE(model->partAt(0), nullptr);
    model->partAt(0)->material->setBaseColor(0.2f, 0.4f, 0.6f, 1.0f);
    model->partAt(0)->material->setMetallicRoughness(0.15f, 0.85f);

    std::string err;
    ASSERT_TRUE(saveGltfMaterials(dest, *model, &err)) << err;

    GltfCpuModel cpu;
    ASSERT_TRUE(parseGltfFile(dest, cpu));
    ASSERT_FALSE(cpu.primitives.empty());
    EXPECT_NEAR(cpu.primitives[0].baseColor[0], 0.2f, 1.0e-4f);
    EXPECT_NEAR(cpu.primitives[0].baseColor[2], 0.6f, 1.0e-4f);
    EXPECT_NEAR(cpu.primitives[0].metallic, 0.15f, 1.0e-4f);
    EXPECT_NEAR(cpu.primitives[0].roughness, 0.85f, 1.0e-4f);

    std::filesystem::remove(dest, ec);
}

TEST(GltfMaterialSave, PreservesTextureIndices)
{
    AssetManager assets;
    Model        model;
    ASSERT_TRUE(makeIndexedModel(assets, model));
    ASSERT_TRUE(model.partAt(0) && model.partAt(0)->material);
    model.partAt(0)->material->setBaseColor(0.1f, 0.2f, 0.3f, 1.0f);
    model.partAt(0)->material->setMetallicRoughness(0.4f, 0.5f);
    model.partAt(0)->material->setNormalScale(1.75f);
    model.partAt(0)->material->setAo(0.6f);

    std::string patched;
    ASSERT_TRUE(patchGltfMaterialsJson(kTexturedMatJson, model, patched, nullptr));
    const json root = json::parse(patched, nullptr, false);
    ASSERT_FALSE(root.is_discarded());
    const json& mat = root["materials"][0];
    const json& pbr = mat["pbrMetallicRoughness"];
    EXPECT_EQ(pbr["baseColorTexture"]["index"].get<int>(), 0);
    EXPECT_EQ(pbr["metallicRoughnessTexture"]["index"].get<int>(), 1);
    EXPECT_EQ(mat["normalTexture"]["index"].get<int>(), 2);
    EXPECT_EQ(mat["occlusionTexture"]["index"].get<int>(), 3);
    EXPECT_EQ(mat["emissiveTexture"]["index"].get<int>(), 4);
    EXPECT_EQ(root["images"][0]["uri"].get<std::string>(), "albedo.png");
    EXPECT_EQ(root["textures"].size(), 5u);
    EXPECT_NEAR(mat["normalTexture"]["scale"].get<float>(), 1.75f, 1.0e-5f);
    EXPECT_NEAR(mat["occlusionTexture"]["strength"].get<float>(), 0.6f, 1.0e-5f);
}

TEST(GltfMaterialSave, WritesCutoffAndEmissiveFactor)
{
    AssetManager assets;
    Model        model;
    ASSERT_TRUE(makeIndexedModel(assets, model));
    ASSERT_TRUE(model.partAt(0) && model.partAt(0)->material);
    model.partAt(0)->material->setAlphaMode(MaterialAlphaMode::Mask);
    model.partAt(0)->material->setAlphaCutoff(0.25f);
    model.partAt(0)->material->setEmissiveColor(0.2f, 0.4f, 0.8f);
    model.partAt(0)->material->setEmissive(2.0f);

    std::string patched;
    ASSERT_TRUE(patchGltfMaterialsJson(R"({"asset":{"version":"2.0"},"materials":[{}]})", model, patched, nullptr));
    const json root = json::parse(patched, nullptr, false);
    ASSERT_FALSE(root.is_discarded());
    const json& mat = root["materials"][0];
    EXPECT_EQ(mat["alphaMode"].get<std::string>(), "MASK");
    EXPECT_NEAR(mat["alphaCutoff"].get<float>(), 0.25f, 1.0e-5f);
    ASSERT_TRUE(mat["emissiveFactor"].is_array());
    EXPECT_NEAR(mat["emissiveFactor"][0].get<float>(), 0.4f, 1.0e-5f);
    EXPECT_NEAR(mat["emissiveFactor"][1].get<float>(), 0.8f, 1.0e-5f);
    EXPECT_NEAR(mat["emissiveFactor"][2].get<float>(), 1.6f, 1.0e-5f);
}

TEST(GltfMaterialSave, WritesScaleAndStrengthWhenMissingObjects)
{
    AssetManager assets;
    Model        model;
    ASSERT_TRUE(makeIndexedModel(assets, model));
    ASSERT_TRUE(model.partAt(0) && model.partAt(0)->material);
    model.partAt(0)->material->setNormalScale(2.0f);
    model.partAt(0)->material->setAo(0.3f);

    std::string patched;
    ASSERT_TRUE(patchGltfMaterialsJson(R"({"asset":{"version":"2.0"},"materials":[{}]})", model, patched, nullptr));
    const json root = json::parse(patched, nullptr, false);
    ASSERT_FALSE(root.is_discarded());
    const json& mat = root["materials"][0];
    EXPECT_NEAR(mat["normalTexture"]["scale"].get<float>(), 2.0f, 1.0e-5f);
    EXPECT_FALSE(mat["normalTexture"].contains("index"));
    EXPECT_NEAR(mat["occlusionTexture"]["strength"].get<float>(), 0.3f, 1.0e-5f);
    EXPECT_FALSE(mat["occlusionTexture"].contains("index"));
}

TEST(GltfMaterialSave, LiveMapWithoutJsonIndexWritesFactorsOnly)
{
    AssetManager assets;
    Model        model;
    ASSERT_TRUE(makeIndexedModel(assets, model));
    ASSERT_TRUE(model.partAt(0) && model.partAt(0)->material);
    auto nrm = assets.loadSolidImage(10, 20, 30, 255);
    ASSERT_TRUE(nrm);
    model.partAt(0)->material->setNormalImage(nrm);
    auto emis = assets.loadSolidImage(40, 50, 60, 255);
    ASSERT_TRUE(emis);
    model.partAt(0)->material->setEmissiveImage(emis);

    std::string patched;
    ASSERT_TRUE(patchGltfMaterialsJson(R"({"asset":{"version":"2.0"},"materials":[{}]})", model, patched, nullptr));
    const json root = json::parse(patched, nullptr, false);
    ASSERT_FALSE(root.is_discarded());
    const json& mat = root["materials"][0];
    if (mat.contains("normalTexture"))
        EXPECT_FALSE(mat["normalTexture"].contains("index"));
    if (mat.contains("emissiveTexture"))
        EXPECT_FALSE(mat["emissiveTexture"].contains("index"));
    EXPECT_TRUE(mat.contains("emissiveFactor"));
    EXPECT_TRUE(mat.contains("alphaCutoff"));
}

TEST(GltfMaterialSave, EmptyJsonFails)
{
    AssetManager assets;
    Model        model;
    ASSERT_TRUE(makeIndexedModel(assets, model));
    std::string patched;
    std::string err;
    EXPECT_FALSE(patchGltfMaterialsJson("", model, patched, &err));
    EXPECT_FALSE(err.empty());
    EXPECT_FALSE(patchGltfMaterialsJson("not json", model, patched, &err));
}

TEST(GltfMaterialSave, ModelWithoutMaterialsFails)
{
    Model model;
    Model::Part part;
    part.mesh = makeTri();
    ASSERT_TRUE(model.createFromParts({ part }));
    std::string patched;
    std::string err;
    EXPECT_FALSE(patchGltfMaterialsJson(R"({"asset":{"version":"2.0"}})", model, patched, &err));
    EXPECT_FALSE(err.empty());
}
