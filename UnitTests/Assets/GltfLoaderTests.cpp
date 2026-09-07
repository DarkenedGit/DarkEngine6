#include <gtest/gtest.h>

#include "Assets/GltfLoader.h"

#include <filesystem>
#include <fstream>
#include <string>

using Dark::GltfCpuModel;
using Dark::parseGltfFile;

namespace
{
    std::filesystem::path writeTempGltf(const std::string& name, const char* json)
    {
        const auto dir = std::filesystem::temp_directory_path() / "de_gltf_tests";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const auto path = dir / name;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << json;
        out.close();
        return path;
    }

    constexpr const char* kCubeJson = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [{ "nodes": [0] }],
  "nodes": [{ "mesh": 0 }],
  "meshes": [{ "primitives": [{ "attributes": { "POSITION": 0 }, "indices": 1, "material": 0 }] }],
  "materials": [{ "pbrMetallicRoughness": { "baseColorFactor": [1, 0, 0, 1], "metallicFactor": 0.1, "roughnessFactor": 0.4 } }],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 8, "type": "VEC3", "min": [-0.5,-0.5,-0.5], "max": [0.5,0.5,0.5] },
    { "bufferView": 1, "componentType": 5123, "count": 36, "type": "SCALAR" }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 96 },
    { "buffer": 0, "byteOffset": 96, "byteLength": 72 }
  ],
  "buffers": [{ "byteLength": 168, "uri": "data:application/octet-stream;base64,AAAAvwAAAL8AAAC/AAAAPwAAAL8AAAC/AAAAPwAAAD8AAAC/AAAAvwAAAD8AAAC/AAAAvwAAAL8AAAA/AAAAPwAAAL8AAAA/AAAAPwAAAD8AAAA/AAAAvwAAAD8AAAA/AAABAAIAAAACAAMAAQAFAAYAAQAGAAIABQAEAAcABQAHAAYABAAAAAMABAADAAcAAwACAAYAAwAGAAcABAAFAAEABAABAAAA" }]
})";

    constexpr const char* kGlassJson = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [{ "nodes": [0] }],
  "nodes": [{ "mesh": 0 }],
  "meshes": [{ "primitives": [{ "attributes": { "POSITION": 0 }, "indices": 1, "material": 0 }] }],
  "materials": [{ "alphaMode": "BLEND", "pbrMetallicRoughness": { "baseColorFactor": [0.2, 0.6, 1, 0.3] } }],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 8, "type": "VEC3", "min": [-0.5,-0.5,-0.5], "max": [0.5,0.5,0.5] },
    { "bufferView": 1, "componentType": 5123, "count": 36, "type": "SCALAR" }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 96 },
    { "buffer": 0, "byteOffset": 96, "byteLength": 72 }
  ],
  "buffers": [{ "byteLength": 168, "uri": "data:application/octet-stream;base64,AAAAvwAAAL8AAAC/AAAAPwAAAL8AAAC/AAAAPwAAAD8AAAC/AAAAvwAAAD8AAAC/AAAAvwAAAL8AAAA/AAAAPwAAAL8AAAA/AAAAPwAAAD8AAAA/AAAAvwAAAD8AAAA/AAABAAIAAAACAAMAAQAFAAYAAQAGAAIABQAEAAcABQAHAAYABAAAAAMABAADAAcAAwACAAYAAwAGAAcABAAFAAEABAABAAAA" }]
})";
} // namespace

TEST(GltfLoader, ParsesOpaqueCube)
{
    const auto path = writeTempGltf("cube.gltf", kCubeJson);
    GltfCpuModel model;
    ASSERT_TRUE(parseGltfFile(path, model));
    ASSERT_EQ(model.primitives.size(), 1u);
    EXPECT_FALSE(model.primitives[0].translucent);
    EXPECT_EQ(model.primitives[0].mesh.positions.size(), 8u);
    EXPECT_EQ(model.primitives[0].mesh.indices.size(), 36u);
    EXPECT_EQ(model.primitives[0].mesh.normals.size(), 8u);
    EXPECT_NEAR(model.primitives[0].baseColor[0], 1.0f, 1.0e-4f);
    EXPECT_NEAR(model.primitives[0].metallic, 0.1f, 1.0e-4f);
    EXPECT_NEAR(model.primitives[0].roughness, 0.4f, 1.0e-4f);
}

TEST(GltfLoader, MarksBlendMaterialTranslucent)
{
    const auto path = writeTempGltf("glass.gltf", kGlassJson);
    GltfCpuModel model;
    ASSERT_TRUE(parseGltfFile(path, model));
    ASSERT_EQ(model.primitives.size(), 1u);
    EXPECT_TRUE(model.primitives[0].translucent);
    EXPECT_NEAR(model.primitives[0].baseColor[3], 0.3f, 1.0e-4f);
}

TEST(GltfLoader, MissingFileFails)
{
    GltfCpuModel model;
    EXPECT_FALSE(parseGltfFile("Z:/definitely/not/a/model.gltf", model));
    EXPECT_TRUE(model.primitives.empty());
}

TEST(GltfLoader, ComputesNormalsWhenMissing)
{
    const auto path = writeTempGltf("cube.gltf", kCubeJson);
    GltfCpuModel model;
    ASSERT_TRUE(parseGltfFile(path, model));
    float len = 0.0f;
    for (const auto& n : model.primitives[0].mesh.normals)
        len += n.x * n.x + n.y * n.y + n.z * n.z;
    EXPECT_GT(len, 1.0f);
}
