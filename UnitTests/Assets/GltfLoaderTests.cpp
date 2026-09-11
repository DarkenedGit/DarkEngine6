#include <gtest/gtest.h>

#include "Assets/GltfLoader.h"
#include "Math/MathHelper.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Dark::GltfCpuModel;
using Dark::parseGltfFile;

namespace
{
    std::filesystem::path writeTempGltf(const std::string& name, const std::string& json)
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

    std::filesystem::path writeTempGltf(const std::string& name, const char* json)
    {
        return writeTempGltf(name, std::string(json));
    }

    void appendU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }

    void appendU16(std::vector<uint8_t>& b, uint16_t v)
    {
        b.push_back(static_cast<uint8_t>(v & 0xff));
        b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    }

    void appendF32(std::vector<uint8_t>& b, float v)
    {
        uint32_t u = 0;
        std::memcpy(&u, &v, 4);
        b.push_back(static_cast<uint8_t>(u & 0xff));
        b.push_back(static_cast<uint8_t>((u >> 8) & 0xff));
        b.push_back(static_cast<uint8_t>((u >> 16) & 0xff));
        b.push_back(static_cast<uint8_t>((u >> 24) & 0xff));
    }

    void appendMat4Translation(std::vector<uint8_t>& b, float x, float y, float z)
    {
        const float m[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            x, y, z, 1
        };
        for (int i = 0; i < 16; ++i)
            appendF32(b, m[i]);
    }

    std::string base64Encode(const std::vector<uint8_t>& data)
    {
        static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((data.size() + 2) / 3) * 4);
        size_t i = 0;
        while (i + 3 <= data.size())
        {
            const uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | uint32_t(data[i + 2]);
            out.push_back(tbl[(n >> 18) & 63]);
            out.push_back(tbl[(n >> 12) & 63]);
            out.push_back(tbl[(n >> 6) & 63]);
            out.push_back(tbl[n & 63]);
            i += 3;
        }
        if (i < data.size())
        {
            uint32_t n = uint32_t(data[i]) << 16;
            if (i + 1 < data.size())
                n |= uint32_t(data[i + 1]) << 8;
            out.push_back(tbl[(n >> 18) & 63]);
            out.push_back(tbl[(n >> 12) & 63]);
            out.push_back((i + 1 < data.size()) ? tbl[(n >> 6) & 63] : '=');
            out.push_back('=');
        }
        return out;
    }

    std::string makeSkinnedGltfJson(const std::vector<uint8_t>& buf, bool childFirstJoints)
    {
        const std::string uri = "data:application/octet-stream;base64," + base64Encode(buf);
        const char* joints = childFirstJoints ? "[2, 1]" : "[1, 2]";
        std::string json;
        json += "{\n  \"asset\": { \"version\": \"2.0\" },\n  \"scene\": 0,\n";
        json += "  \"scenes\": [{ \"nodes\": [0, 3] }],\n  \"nodes\": [\n";
        json += "    { \"name\": \"Armature\", \"translation\": [0, 4, 0], \"children\": [1] },\n";
        json += "    { \"name\": \"Hips\", \"children\": [2] },\n";
        json += "    { \"name\": \"Spine\", \"translation\": [0, 1, 0] },\n";
        json += "    { \"name\": \"Mesh\", \"translation\": [3, 0, 0], \"mesh\": 0, \"skin\": 0 }\n  ],\n";
        json += "  \"skins\": [{ \"joints\": ";
        json += joints;
        json += ", \"skeleton\": 0, \"inverseBindMatrices\": 4 }],\n";
        json += "  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0, \"JOINTS_0\": 1, \"WEIGHTS_0\": 2 }, \"indices\": 3 }] }],\n";
        json += "  \"animations\": [{ \"name\": \"Walk\", \"channels\": [{ \"sampler\": 0, \"target\": { \"node\": 1, \"path\": \"translation\" } }],\n";
        json += "    \"samplers\": [{ \"input\": 5, \"output\": 6, \"interpolation\": \"LINEAR\" }] }],\n";
        json += "  \"accessors\": [\n";
        json += "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n";
        json += "    { \"bufferView\": 1, \"componentType\": 5121, \"count\": 3, \"type\": \"VEC4\" },\n";
        json += "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC4\" },\n";
        json += "    { \"bufferView\": 3, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" },\n";
        json += "    { \"bufferView\": 4, \"componentType\": 5126, \"count\": 2, \"type\": \"MAT4\" },\n";
        json += "    { \"bufferView\": 5, \"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\" },\n";
        json += "    { \"bufferView\": 6, \"componentType\": 5126, \"count\": 2, \"type\": \"VEC3\" }\n  ],\n";
        json += "  \"bufferViews\": [\n";
        json += "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36 },\n";
        json += "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 12 },\n";
        json += "    { \"buffer\": 0, \"byteOffset\": 48, \"byteLength\": 48 },\n";
        json += "    { \"buffer\": 0, \"byteOffset\": 96, \"byteLength\": 6 },\n";
        json += "    { \"buffer\": 0, \"byteOffset\": 104, \"byteLength\": 128 },\n";
        json += "    { \"buffer\": 0, \"byteOffset\": 232, \"byteLength\": 8 },\n";
        json += "    { \"buffer\": 0, \"byteOffset\": 240, \"byteLength\": 24 }\n  ],\n";
        json += "  \"buffers\": [{ \"byteLength\": 264, \"uri\": \"";
        json += uri;
        json += "\" }]\n}\n";
        return json;
    }

    std::vector<uint8_t> makeSkinnedBuffer()
    {
        std::vector<uint8_t> b;
        b.reserve(264);
        appendF32(b, 0); appendF32(b, 0); appendF32(b, 0);
        appendF32(b, 1); appendF32(b, 0); appendF32(b, 0);
        appendF32(b, 0); appendF32(b, 1); appendF32(b, 0);
        appendU8(b, 0); appendU8(b, 0); appendU8(b, 0); appendU8(b, 0);
        appendU8(b, 0); appendU8(b, 0); appendU8(b, 0); appendU8(b, 0);
        appendU8(b, 0); appendU8(b, 0); appendU8(b, 0); appendU8(b, 0);
        for (int v = 0; v < 3; ++v)
        {
            appendF32(b, 1); appendF32(b, 0); appendF32(b, 0); appendF32(b, 0);
        }
        appendU16(b, 0); appendU16(b, 1); appendU16(b, 2);
        while (b.size() < 104)
            b.push_back(0);
        appendMat4Translation(b, 0, -4, 0);
        appendMat4Translation(b, 0, -5, 0);
        appendF32(b, 0); appendF32(b, 1);
        appendF32(b, 0); appendF32(b, 0); appendF32(b, 0);
        appendF32(b, 1); appendF32(b, 0); appendF32(b, 0);
        return b;
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

TEST(GltfLoader, ParsesSkinAndClip)
{
    const auto path = writeTempGltf("skinned.gltf", makeSkinnedGltfJson(makeSkinnedBuffer(), false));
    GltfCpuModel model;
    ASSERT_TRUE(parseGltfFile(path, model));
    ASSERT_EQ(model.skeleton.joints.size(), 2u);
    ASSERT_EQ(model.clips.size(), 1u);
    EXPECT_EQ(model.clips[0].name, "Walk");
    ASSERT_EQ(model.primitives.size(), 1u);
    EXPECT_TRUE(model.primitives[0].skinned);
    EXPECT_EQ(model.primitives[0].mesh.jointPacked.size(), 3u);
    EXPECT_EQ(model.primitives[0].mesh.weights.size(), 3u);
    EXPECT_NEAR(model.skeleton.joints[0].restT.x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(model.skeleton.joints[0].restT.y, 0.0f, 1.0e-4f);
    EXPECT_GT(std::fabs(model.skeleton.joints[0].ancestorBindWorld.GetTranslation().y), 3.0f);
    EXPECT_NEAR(model.skeleton.meshWorld.GetTranslation().x, 3.0f, 1.0e-3f);
}

TEST(GltfLoader, ChildFirstJointsKeepAccessorOrder)
{
    const auto path = writeTempGltf("childfirst.gltf", makeSkinnedGltfJson(makeSkinnedBuffer(), true));
    GltfCpuModel model;
    ASSERT_TRUE(parseGltfFile(path, model));
    ASSERT_EQ(model.skeleton.joints.size(), 2u);
    EXPECT_EQ(model.skeleton.joints[0].name, "Spine");
    EXPECT_EQ(model.skeleton.joints[1].name, "Hips");
    EXPECT_EQ(model.skeleton.joints[0].parent, 1);
    ASSERT_EQ(model.skeleton.fkOrder.size(), 2u);
    EXPECT_EQ(model.skeleton.fkOrder[0], 1u);
    EXPECT_EQ(model.skeleton.fkOrder[1], 0u);
}

TEST(GltfLoader, TooManyJointsFails)
{
    std::string nodes = "[";
    std::string joints = "[";
    for (int i = 0; i < 65; ++i)
    {
        if (i)
        {
            nodes += ",";
            joints += ",";
        }
        nodes += "{}";
        joints += std::to_string(i);
    }
    nodes += ",{\"mesh\":0}]";
    joints += "]";
    const std::string json = std::string(R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [{ "nodes": [65] }],
  "nodes": )") + nodes + R"(,
  "skins": [{ "joints": )" + joints + R"( }],
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
})";
    const auto path = writeTempGltf("toomany.gltf", json);
    GltfCpuModel model;
    EXPECT_FALSE(parseGltfFile(path, model));
}

TEST(GltfLoader, HasMatrixJointDecomposesTranslation)
{
    std::vector<uint8_t> buf;
    appendF32(buf, 0); appendF32(buf, 0); appendF32(buf, 0);
    appendF32(buf, 1); appendF32(buf, 0); appendF32(buf, 0);
    appendF32(buf, 0); appendF32(buf, 1); appendF32(buf, 0);
    appendU8(buf, 0); appendU8(buf, 0); appendU8(buf, 0); appendU8(buf, 0);
    appendU8(buf, 0); appendU8(buf, 0); appendU8(buf, 0); appendU8(buf, 0);
    appendU8(buf, 0); appendU8(buf, 0); appendU8(buf, 0); appendU8(buf, 0);
    for (int v = 0; v < 3; ++v)
    {
        appendF32(buf, 1); appendF32(buf, 0); appendF32(buf, 0); appendF32(buf, 0);
    }
    appendU16(buf, 0); appendU16(buf, 1); appendU16(buf, 2);
    while (buf.size() < 104)
        buf.push_back(0);
    appendMat4Translation(buf, -3, 0, 0);
    const std::string uri = "data:application/octet-stream;base64," + base64Encode(buf);
    std::string json;
    json += "{\n  \"asset\": { \"version\": \"2.0\" },\n  \"scene\": 0,\n";
    json += "  \"scenes\": [{ \"nodes\": [0] }],\n";
    json += "  \"nodes\": [{ \"matrix\": [1,0,0,0, 0,1,0,0, 0,0,1,0, 3,0,0,1], \"mesh\": 0, \"skin\": 0 }],\n";
    json += "  \"skins\": [{ \"joints\": [0], \"inverseBindMatrices\": 4 }],\n";
    json += "  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0, \"JOINTS_0\": 1, \"WEIGHTS_0\": 2 }, \"indices\": 3 }] }],\n";
    json += "  \"accessors\": [\n";
    json += "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n";
    json += "    { \"bufferView\": 1, \"componentType\": 5121, \"count\": 3, \"type\": \"VEC4\" },\n";
    json += "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC4\" },\n";
    json += "    { \"bufferView\": 3, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" },\n";
    json += "    { \"bufferView\": 4, \"componentType\": 5126, \"count\": 1, \"type\": \"MAT4\" }\n  ],\n";
    json += "  \"bufferViews\": [\n";
    json += "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36 },\n";
    json += "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 12 },\n";
    json += "    { \"buffer\": 0, \"byteOffset\": 48, \"byteLength\": 48 },\n";
    json += "    { \"buffer\": 0, \"byteOffset\": 96, \"byteLength\": 6 },\n";
    json += "    { \"buffer\": 0, \"byteOffset\": 104, \"byteLength\": 64 }\n  ],\n";
    json += "  \"buffers\": [{ \"byteLength\": ";
    json += std::to_string(buf.size());
    json += ", \"uri\": \"";
    json += uri;
    json += "\" }]\n}\n";
    const auto path = writeTempGltf("matrixjoint.gltf", json);
    GltfCpuModel model;
    ASSERT_TRUE(parseGltfFile(path, model));
    ASSERT_EQ(model.skeleton.joints.size(), 1u);
    EXPECT_NEAR(model.skeleton.joints[0].restT.x, 3.0f, 1.0e-3f);
}

TEST(GltfLoader, StaticCubeStillUnskinned)
{
    const auto path = writeTempGltf("cube2.gltf", kCubeJson);
    GltfCpuModel model;
    ASSERT_TRUE(parseGltfFile(path, model));
    EXPECT_TRUE(model.skeleton.joints.empty());
    EXPECT_TRUE(model.clips.empty());
    EXPECT_FALSE(model.primitives[0].skinned);
}
