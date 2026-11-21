#include <gtest/gtest.h>

#include "Render/RhiDevice.h"
#include "Render/ShaderCompile.h"
#include "Render/ShaderDxc.h"
#include "Render/ShaderKey.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{

    const Dark::ShaderManifest* repoManifest()
    {
        return Dark::loadedShaderManifest();
    }

} // namespace

// The checked-in manifest parses, names real files, and gives every line a distinct baked file name.
TEST(ShaderManifestRepo, ParsesAndNamesRealFiles)
{
    const Dark::ShaderManifest* manifest = repoManifest();
    ASSERT_NE(manifest, nullptr);
    ASSERT_FALSE(manifest->keys.empty());

    const std::filesystem::path dir = Dark::resolveContentPath("shaders");
    std::set<std::string>       names;
    for (const Dark::ShaderKey& key : manifest->keys)
    {
        EXPECT_TRUE(std::filesystem::exists(dir / key.file)) << Dark::formatShaderKey(key);
        EXPECT_TRUE(names.insert(Dark::bakedShaderFileName(key)).second) << Dark::formatShaderKey(key);
    }
}

// Every .hlsl under content/shaders has at least one manifest line (.hlsli are includes).
TEST(ShaderManifestRepo, CoversEveryHlslFile)
{
    const Dark::ShaderManifest* manifest = repoManifest();
    ASSERT_NE(manifest, nullptr);
    std::set<std::string> files;
    for (const Dark::ShaderKey& key : manifest->keys)
        files.insert(key.file);

    const std::filesystem::path dir = Dark::resolveContentPath("shaders");
    std::error_code             ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".hlsl")
            continue;
        const std::string rel = std::filesystem::relative(entry.path(), dir, ec).generic_string();
        EXPECT_TRUE(files.count(rel) == 1) << rel << " has no line in shaders.manifest";
    }
}

// Every manifest permutation compiles with the runtime DXC at the engine's shader model (same set the
// Release bake compiles with the build-time dxc.exe).
TEST(ShaderManifestRepo, EveryLineCompilesWithDxc)
{
    if (!Dark::dxcAvailable())
        GTEST_SKIP() << "runtime DXC not next to UnitTests.exe";
    const Dark::ShaderManifest* manifest = repoManifest();
    ASSERT_NE(manifest, nullptr);

    const std::filesystem::path dir = Dark::resolveContentPath("shaders");
    for (const Dark::ShaderKey& key : manifest->keys)
    {
        std::string source;
        ASSERT_TRUE(Dark::readTextFile(dir / key.file, source)) << key.file;

        Dark::DxcCompileRequest request;
        request.path   = dir / key.file;
        request.source = &source;
        request.entry  = key.entry.c_str();
        request.stage  = key.stage;
        ASSERT_TRUE(Dark::splitShaderDefines(key.defines, request.defines)) << Dark::formatShaderKey(key);

        ComPtr<ID3DBlob>                         blob;
        std::vector<Dark::ShaderCacheDependency> deps;
        EXPECT_TRUE(Dark::dxcCompile(request, blob, deps)) << Dark::formatShaderKey(key);
        if (blob)
        {
            EXPECT_TRUE(Dark::isDxilContainer(blob->GetBufferPointer(), blob->GetBufferSize())) << Dark::formatShaderKey(key);
        }
    }
}

// WARP meets the engine floor the tests rely on (FL 12_1) and reports its caps.
TEST(RhiDevice, WarpTestDeviceReportsCaps)
{
    Dark::RhiCaps        caps;
    ComPtr<ID3D12Device> device = Dark::createWarpTestDevice(&caps);
    if (!device)
        GTEST_SKIP() << "no WARP device at FL 12_1";
    EXPECT_TRUE(caps.software);
    EXPECT_GE(static_cast<uint32_t>(caps.featureLevel), 0xc100u);
    EXPECT_GE(static_cast<uint32_t>(caps.shaderModel), 0x60u);
    EXPECT_FALSE(caps.adapterName.empty());
}
