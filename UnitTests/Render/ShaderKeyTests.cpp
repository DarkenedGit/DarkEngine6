#include <gtest/gtest.h>

#include "Render/ShaderCache.h"
#include "Render/ShaderKey.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Dark;

TEST(ShaderKey, ProfilesFollowTheModelConstants)
{
    EXPECT_EQ(shaderProfile(ShaderStage::Vertex, false), std::string("vs_") + kShaderModelDxc);
    EXPECT_EQ(shaderProfile(ShaderStage::Pixel, false), std::string("ps_") + kShaderModelDxc);
    EXPECT_EQ(shaderProfile(ShaderStage::Compute, true), "cs_5_1");
    ShaderStage stage = ShaderStage::Vertex;
    EXPECT_TRUE(shaderStageFromId("cs", stage));
    EXPECT_EQ(stage, ShaderStage::Compute);
    EXPECT_FALSE(shaderStageFromId("gs", stage));
}

TEST(ShaderKey, DefinesAreSortedByName)
{
    const ShaderDefine defines[] = { { "DE_SURFACE_CATEGORY", "0" }, { "DE_GBUFFER_SURFACE_TAG", "1" } };
    EXPECT_EQ(canonicalShaderDefines(defines), "DE_GBUFFER_SURFACE_TAG=1,DE_SURFACE_CATEGORY=0");
    EXPECT_EQ(canonicalShaderDefines({}), "");

    std::vector<ShaderDefine> split;
    ASSERT_TRUE(splitShaderDefines("A=1,B=", split));
    ASSERT_EQ(split.size(), 2u);
    EXPECT_EQ(split[1].name, "B");
    EXPECT_EQ(split[1].value, "");
    EXPECT_FALSE(splitShaderDefines("A", split));
    EXPECT_FALSE(splitShaderDefines("1A=2", split));
}

TEST(ShaderKey, FileFromContentPath)
{
    std::string file;
    EXPECT_TRUE(shaderFileFromContentPath("shaders/TerrainGBuffer.hlsl", file));
    EXPECT_EQ(file, "TerrainGBuffer.hlsl");
    EXPECT_TRUE(shaderFileFromContentPath("shaders\\sub\\X.hlsl", file));
    EXPECT_EQ(file, "sub/X.hlsl");
    EXPECT_FALSE(shaderFileFromContentPath("textures/x.png", file));
}

// cmake/ShaderBake.cmake builds the same names (checked against de_baked_shader_name).
TEST(ShaderKey, BakedNamesMatchTheCMakeRule)
{
    EXPECT_EQ(bakedShaderFileName({ "Bloom.hlsl", "PSExtract", ShaderStage::Pixel, "" }), "Bloom.PSExtract.ps.dxil");
    EXPECT_EQ(bakedShaderFileName({ "TerrainGBuffer.hlsl", "PSMain", ShaderStage::Pixel, "DE_GBUFFER_SURFACE_TAG=1,DE_SURFACE_CATEGORY=0" }), "TerrainGBuffer.PSMain.ps.DE_GBUFFER_SURFACE_TAG-1_DE_SURFACE_CATEGORY-0.dxil");
    EXPECT_EQ(bakedShaderFileName({ "sub/Odd Name.hlsl", "CSMain", ShaderStage::Compute, "X=a b" }), "sub_Odd_Name.CSMain.cs.X-a_b.dxil");
}

TEST(ShaderManifest, ParsesLinesAndComments)
{
    ShaderManifest manifest;
    std::string    error;
    ASSERT_TRUE(parseShaderManifest("# comment\n\nBloom.hlsl VSMain vs\r\n  Sky.hlsl PSMain ps CLOUD_LAYER=1  \n", manifest, error)) << error;
    ASSERT_EQ(manifest.keys.size(), 2u);
    EXPECT_EQ(manifest.keys[1].file, "Sky.hlsl");
    EXPECT_EQ(manifest.keys[1].stage, ShaderStage::Pixel);
    EXPECT_EQ(manifest.keys[1].defines, "CLOUD_LAYER=1");
    EXPECT_TRUE(manifest.contains({ "Bloom.hlsl", "VSMain", ShaderStage::Vertex, "" }));
    EXPECT_FALSE(manifest.contains({ "Bloom.hlsl", "VSMain", ShaderStage::Pixel, "" }));
    EXPECT_EQ(formatShaderKey(manifest.keys[1]), "Sky.hlsl PSMain ps CLOUD_LAYER=1");
}

TEST(ShaderManifest, RejectsBadLines)
{
    ShaderManifest manifest;
    std::string    error;
    EXPECT_FALSE(parseShaderManifest("Bloom.hlsl VSMain gs\n", manifest, error));
    EXPECT_NE(error.find("line 1"), std::string::npos);
    EXPECT_FALSE(parseShaderManifest("Bloom.hlsl VSMain\n", manifest, error));
    EXPECT_FALSE(parseShaderManifest("Bloom.fx VSMain vs\n", manifest, error));
    EXPECT_FALSE(parseShaderManifest("X.hlsl PSMain ps B=1,A=1\n", manifest, error));
    EXPECT_NE(error.find("sorted"), std::string::npos);
    EXPECT_FALSE(parseShaderManifest("X.hlsl PSMain ps\nX.hlsl PSMain ps\n", manifest, error));
    EXPECT_NE(error.find("line 2"), std::string::npos);
    EXPECT_FALSE(parseShaderManifest("X.hlsl PSMain ps A=1 extra\n", manifest, error));
}

namespace
{
    ShaderRootSet engineAndGameRoots()
    {
        ShaderRootSet                  roots;
        std::string                    error;
        const std::vector<std::string> engineFiles = { "Bloom.hlsl", "GBuffer.hlsli", "sub/Fog.hlsl" };
        const std::vector<std::string> gameFiles   = { "Echo.hlsl" };
        EXPECT_TRUE(roots.add("shaders", "Bloom.hlsl VSMain vs\nsub/Fog.hlsl PSMain ps\nListed.hlsl PSMain ps\n", engineFiles, error)) << error;
        EXPECT_TRUE(roots.add("game/shaders", "Echo.hlsl PSSnapshot ps ECHO_SNAPSHOT=1\n", gameFiles, error)) << error;
        return roots;
    }
} // namespace

TEST(ShaderRootSet, ResolvesFilesFromEveryRoot)
{
    const ShaderRootSet roots = engineAndGameRoots();
    ASSERT_EQ(roots.roots().size(), 2u);
    std::string file;
    EXPECT_TRUE(roots.fileFromContentPath("game/shaders/Echo.hlsl", file));
    EXPECT_EQ(file, "Echo.hlsl");
    EXPECT_TRUE(roots.fileFromContentPath("game\\shaders\\Echo.hlsl", file));
    EXPECT_EQ(file, "Echo.hlsl");
    EXPECT_TRUE(roots.fileFromContentPath("shaders/sub/Fog.hlsl", file));
    EXPECT_EQ(file, "sub/Fog.hlsl");
    file = "unchanged";
    EXPECT_FALSE(roots.fileFromContentPath("game/Echo.hlsl", file));
    EXPECT_FALSE(roots.fileFromContentPath("game/shadersX/Echo.hlsl", file));
    EXPECT_FALSE(roots.fileFromContentPath("game/shaders/", file));
    EXPECT_FALSE(roots.fileFromContentPath("textures/x.png", file));
    EXPECT_EQ(file, "unchanged");

    EXPECT_TRUE(roots.contains({ "Echo.hlsl", "PSSnapshot", ShaderStage::Pixel, "ECHO_SNAPSHOT=1" }));
    EXPECT_TRUE(roots.contains({ "Bloom.hlsl", "VSMain", ShaderStage::Vertex, "" }));
    EXPECT_FALSE(roots.contains({ "Echo.hlsl", "PSSnapshot", ShaderStage::Pixel, "" }));
    ASSERT_NE(roots.find("game/shaders"), nullptr);
    EXPECT_EQ(roots.find("game/shaders")->files, std::vector<std::string>{ "Echo.hlsl" });
    EXPECT_EQ(roots.find("game"), nullptr);
}

TEST(ShaderRootSet, RejectsAFileNameInTwoRoots)
{
    ShaderRootSet roots = engineAndGameRoots();
    std::string   error;
    // On disk in the new root only.
    const std::vector<std::string> onDisk = { "GBuffer.hlsli" };
    EXPECT_FALSE(roots.add("mod/shaders", "", onDisk, error));
    EXPECT_NE(error.find("GBuffer.hlsli"), std::string::npos) << error;
    EXPECT_NE(error.find("shaders/"), std::string::npos) << error;
    // Listed by the new manifest, on disk in an old root; names compare without case.
    EXPECT_FALSE(roots.add("mod/shaders", "bloom.hlsl PSMain ps\n", {}, error));
    EXPECT_NE(error.find("unique"), std::string::npos) << error;
    // On disk in the new root with a backslash path, on disk in an old root.
    const std::vector<std::string> fog = { "sub\\Fog.hlsl" };
    EXPECT_FALSE(roots.add("mod/shaders", "", fog, error));
    // In an old root's manifest only (not in that root's file list).
    const std::vector<std::string> listed = { "Listed.hlsl" };
    EXPECT_FALSE(roots.add("mod/shaders", "", listed, error));
    EXPECT_NE(error.find("Listed.hlsl"), std::string::npos) << error;
    EXPECT_EQ(roots.roots().size(), 2u);
    EXPECT_EQ(roots.find("mod/shaders"), nullptr);
}

TEST(ShaderRootSet, RejectsABakedNameInTwoRoots)
{
    ShaderRootSet roots = engineAndGameRoots();
    std::string   error;
    // sub_Fog.hlsl and sub/Fog.hlsl are different files with one baked name.
    EXPECT_FALSE(roots.add("mod/shaders", "sub_Fog.hlsl PSMain ps\n", {}, error));
    EXPECT_NE(error.find("sub_Fog.PSMain.ps.dxil"), std::string::npos) << error;
    EXPECT_EQ(roots.roots().size(), 2u);
    // A different entry point is a different baked name.
    EXPECT_TRUE(roots.add("mod/shaders", "sub_Fog.hlsl VSMain vs\n", {}, error)) << error;
}

TEST(ShaderRootSet, RejectsBadOrOverlappingRoots)
{
    ShaderRootSet roots = engineAndGameRoots();
    std::string   error;
    for (const char* dir : { "", "/abs", "game/", "game\\other", "C:/x", "a/../b", "a//b", ".", "shaders", "Game/Shaders", "game", "shaders/extra" })
    {
        EXPECT_FALSE(roots.add(dir, "", {}, error)) << dir;
        EXPECT_FALSE(error.empty()) << dir;
    }
    EXPECT_FALSE(roots.add("mod/shaders", "X.hlsl PSMain gs\n", {}, error));
    EXPECT_NE(error.find("mod/shaders/shaders.manifest line 1"), std::string::npos) << error;
    EXPECT_EQ(roots.roots().size(), 2u);
    EXPECT_TRUE(roots.add("game/shaders2", "", {}, error)) << error;
}

TEST(ShaderKey, DxilContainerDetection)
{
    // Container with two parts: 'ISG1' then 'DXIL'.
    std::vector<uint8_t> bytes(32 + 8 + 16, 0);
    bytes[0] = 'D', bytes[1] = 'X', bytes[2] = 'B', bytes[3] = 'C';
    bytes[28] = 2;
    bytes[32] = 48; // part 0 at 48
    bytes[36] = 40; // part 1 at 40
    bytes.resize(64, 0);
    bytes[40] = 'D', bytes[41] = 'X', bytes[42] = 'I', bytes[43] = 'L';
    bytes[48] = 'I', bytes[49] = 'S', bytes[50] = 'G', bytes[51] = '1';
    EXPECT_TRUE(isDxilContainer(bytes.data(), bytes.size()));
    bytes[40] = 'S', bytes[41] = 'H', bytes[42] = 'E', bytes[43] = 'X'; // FXC
    EXPECT_FALSE(isDxilContainer(bytes.data(), bytes.size()));
    bytes[28] = 200; // part count past the end
    EXPECT_FALSE(isDxilContainer(bytes.data(), bytes.size()));
    EXPECT_FALSE(isDxilContainer(nullptr, 0));
}

TEST(ShaderCache, KeyChangesWithEveryInput)
{
    const uint64_t base = shaderCacheKey("src", "a.hlsl", "PSMain", "ps_6_0", "A=1", "-O3", "1.9");
    EXPECT_EQ(base, shaderCacheKey("src", "a.hlsl", "PSMain", "ps_6_0", "A=1", "-O3", "1.9"));
    EXPECT_NE(base, shaderCacheKey("src2", "a.hlsl", "PSMain", "ps_6_0", "A=1", "-O3", "1.9"));
    EXPECT_NE(base, shaderCacheKey("src", "b.hlsl", "PSMain", "ps_6_0", "A=1", "-O3", "1.9"));
    EXPECT_NE(base, shaderCacheKey("src", "a.hlsl", "VSMain", "ps_6_0", "A=1", "-O3", "1.9"));
    EXPECT_NE(base, shaderCacheKey("src", "a.hlsl", "PSMain", "ps_6_6", "A=1", "-O3", "1.9"));
    EXPECT_NE(base, shaderCacheKey("src", "a.hlsl", "PSMain", "ps_6_0", "A=2", "-O3", "1.9"));
    EXPECT_NE(base, shaderCacheKey("src", "a.hlsl", "PSMain", "ps_6_0", "A=1", "-Od", "1.9"));
    EXPECT_NE(base, shaderCacheKey("src", "a.hlsl", "PSMain", "ps_6_0", "A=1", "-O3", "1.10"));
    // Field boundaries are part of the key.
    EXPECT_NE(shaderCacheKey("ab", "c", "", "", "", "", ""), shaderCacheKey("a", "bc", "", "", "", "", ""));
    EXPECT_EQ(shaderCacheFileName(0x0123456789abcdefull), "0123456789abcdef.bin");
}

TEST(ShaderCache, SerializeRoundTripAndCorruption)
{
    ShaderCacheEntry entry;
    entry.key                  = 42;
    entry.dependencies         = { { "C:/x/GBuffer.hlsli", 7 }, { "C:/x/Depth.hlsli", 9 } };
    entry.bytecode             = { 1, 2, 3, 4, 5 };
    std::vector<uint8_t> bytes = serializeShaderCacheEntry(entry);

    ShaderCacheEntry back;
    ASSERT_TRUE(parseShaderCacheEntry(bytes, back));
    EXPECT_EQ(back.key, 42u);
    ASSERT_EQ(back.dependencies.size(), 2u);
    EXPECT_EQ(back.dependencies[1].path, "C:/x/Depth.hlsli");
    EXPECT_EQ(back.dependencies[1].hash, 9u);
    EXPECT_EQ(back.bytecode, entry.bytecode);

    bytes[20] ^= 0xFF;
    EXPECT_FALSE(parseShaderCacheEntry(bytes, back));
    EXPECT_FALSE(parseShaderCacheEntry(std::vector<uint8_t>(5, 0), back));
}

TEST(ShaderCache, StoreLoadAndIncludeInvalidation)
{
    const std::filesystem::path dir     = std::filesystem::temp_directory_path() / "de6_shader_cache_test";
    const std::filesystem::path include = dir / "inc.hlsli";
    std::error_code             ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    {
        std::ofstream f(include, std::ios::binary);
        f << "float4 a;";
    }
    uint64_t incHash = 0;
    ASSERT_TRUE(hashShaderCacheFile(include, incHash));

    ShaderCacheEntry entry;
    entry.key          = 0xfeedull;
    entry.dependencies = { { include.generic_string(), incHash } };
    entry.bytecode     = { 9, 8, 7 };
    ASSERT_TRUE(storeShaderCacheEntry(dir, entry));

    ShaderCacheEntry loaded;
    ASSERT_TRUE(loadShaderCacheEntry(dir, 0xfeedull, loaded));
    EXPECT_EQ(loaded.bytecode, entry.bytecode);
    EXPECT_FALSE(loadShaderCacheEntry(dir, 0xbeefull, loaded));

    {
        std::ofstream f(include, std::ios::binary | std::ios::trunc);
        f << "float4 b;";
    }
    EXPECT_FALSE(loadShaderCacheEntry(dir, 0xfeedull, loaded)) << "a changed include must miss";
    std::filesystem::remove_all(dir, ec);
}
