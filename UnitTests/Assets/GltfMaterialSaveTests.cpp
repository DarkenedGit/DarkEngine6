#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Assets/GltfLoader.h"
#include "Assets/GltfMaterialSave.h"
#include "Assets/Material.h"
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
