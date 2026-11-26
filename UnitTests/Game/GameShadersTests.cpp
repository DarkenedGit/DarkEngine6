#include <gtest/gtest.h>

#include "Game/GameShaders.h"
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

    std::vector<std::string> shaderFilesUnder(const std::filesystem::path& dir)
    {
        std::vector<std::string> files;
        std::error_code          ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec))
        {
            const std::filesystem::path ext = entry.path().extension();
            if (entry.is_regular_file() && (ext == ".hlsl" || ext == ".hlsli"))
                files.push_back(std::filesystem::relative(entry.path(), dir, ec).generic_string());
        }
        return files;
    }

    bool loadRootManifest(const char* root, Dark::ShaderManifest& out)
    {
        std::string                 text;
        std::string                 error;
        const std::filesystem::path dir = Dark::resolveContentPath(root);
        return !dir.empty() && Dark::readTextFile(dir / "shaders.manifest", text) && Dark::parseShaderManifest(text, out, error);
    }

} // namespace

TEST(GameShaders, RootAddsBesideTheEngineRoot)
{
    EXPECT_TRUE(Dark::addGameShaderRoot());
    EXPECT_TRUE(Dark::addGameShaderRoot());
}

// The game manifest names real files under content/game/shaders and covers every .hlsl there.
TEST(GameShaders, ManifestNamesRealFilesAndCoversEveryHlsl)
{
    Dark::ShaderManifest manifest;
    ASSERT_TRUE(loadRootManifest(Dark::kGameShaderRoot, manifest));
    ASSERT_FALSE(manifest.keys.empty());

    const std::filesystem::path dir = Dark::resolveContentPath(Dark::kGameShaderRoot);
    std::set<std::string>       listed;
    for (const Dark::ShaderKey& key : manifest.keys)
    {
        EXPECT_TRUE(std::filesystem::exists(dir / key.file)) << Dark::formatShaderKey(key);
        listed.insert(key.file);
    }
    for (const std::string& file : shaderFilesUnder(dir))
    {
        if (std::filesystem::path(file).extension() == ".hlsl")
            EXPECT_EQ(listed.count(file), 1u) << file << " has no line in content/game/shaders/shaders.manifest";
    }
}

// The checked-in roots share no shader file or baked name (the bake fails the configure on a clash).
TEST(GameShaders, NoFileOrBakedNameInBothRoots)
{
    const std::filesystem::path engineDir = Dark::resolveContentPath("shaders");
    const std::filesystem::path gameDir   = Dark::resolveContentPath(Dark::kGameShaderRoot);
    ASSERT_FALSE(engineDir.empty());
    ASSERT_FALSE(gameDir.empty());
    std::string engineText;
    std::string gameText;
    ASSERT_TRUE(Dark::readTextFile(engineDir / "shaders.manifest", engineText));
    ASSERT_TRUE(Dark::readTextFile(gameDir / "shaders.manifest", gameText));

    Dark::ShaderRootSet roots;
    std::string         error;
    ASSERT_TRUE(roots.add(Dark::kEngineShaderRoot, engineText, shaderFilesUnder(engineDir), error)) << error;
    EXPECT_TRUE(roots.add(Dark::kGameShaderRoot, gameText, shaderFilesUnder(gameDir), error)) << error;
}

// Echo.hlsl includes GBuffer.hlsli from content/shaders: the runtime compile searches it after the file's folder.
TEST(GameShaders, CompileFindsEngineIncludes)
{
    const std::filesystem::path echo = Dark::resolveContentPath("game/shaders/Echo.hlsl");
    ASSERT_FALSE(echo.empty());
    D3D_SHADER_MACRO snap[2] = { { "ECHO_SNAPSHOT", "1" }, { nullptr, nullptr } };
    ComPtr<ID3DBlob> blob;
    EXPECT_TRUE(Dark::compileShaderFromFile(echo, "VSMain", Dark::ShaderStage::Vertex, blob));
    EXPECT_TRUE(Dark::compileShaderFromFile(echo, "PSSnapshot", Dark::ShaderStage::Pixel, blob, snap));
    EXPECT_TRUE(Dark::compileShaderFromFile(echo, "PSComposite", Dark::ShaderStage::Pixel, blob));
}

// Every game manifest line compiles with the runtime DXC, with content/shaders on the include path as
// in the Release bake.
TEST(GameShaders, EveryLineCompilesWithDxc)
{
    if (!Dark::dxcAvailable())
        GTEST_SKIP() << "runtime DXC not next to UnitTests.exe";
    Dark::ShaderManifest manifest;
    ASSERT_TRUE(loadRootManifest(Dark::kGameShaderRoot, manifest));

    const std::filesystem::path dir       = Dark::resolveContentPath(Dark::kGameShaderRoot);
    const std::filesystem::path engineDir = Dark::resolveContentPath("shaders");
    for (const Dark::ShaderKey& key : manifest.keys)
    {
        std::string source;
        ASSERT_TRUE(Dark::readTextFile(dir / key.file, source)) << key.file;

        Dark::DxcCompileRequest request;
        request.path        = dir / key.file;
        request.source      = &source;
        request.entry       = key.entry.c_str();
        request.stage       = key.stage;
        request.includeDirs = { engineDir };
        ASSERT_TRUE(Dark::splitShaderDefines(key.defines, request.defines)) << Dark::formatShaderKey(key);

        ComPtr<ID3DBlob>                         blob;
        std::vector<Dark::ShaderCacheDependency> deps;
        EXPECT_TRUE(Dark::dxcCompile(request, blob, deps)) << Dark::formatShaderKey(key);
    }
}
