#include <gtest/gtest.h>

#include "Render/ShaderCompile.h"

#include <d3d12.h>
#include <fstream>
#include <filesystem>

using Dark::compileShaderFromFile;
using Dark::encodeSrgbForColorFormat;
using Dark::makeEncodeSrgbMacros;
using Microsoft::WRL::ComPtr;

namespace
{

struct ScopedTempFile
{
    std::filesystem::path path;

    explicit ScopedTempFile(std::filesystem::path p)
        : path(std::move(p))
    {
    }

    ~ScopedTempFile()
    {
        std::error_code ec;
        if (!path.empty())
            std::filesystem::remove(path, ec);
    }

    ScopedTempFile(const ScopedTempFile&)            = delete;
    ScopedTempFile& operator=(const ScopedTempFile&) = delete;
};

} // namespace

TEST(ShaderCompile, EncodeSrgbMacroForUnorm)
{
    EXPECT_TRUE(encodeSrgbForColorFormat(DXGI_FORMAT_R8G8B8A8_UNORM));
    D3D_SHADER_MACRO macros[2]{};
    makeEncodeSrgbMacros(true, macros);
    EXPECT_STREQ(macros[0].Name, "ENCODE_SRGB");
    EXPECT_STREQ(macros[0].Definition, "1");
    EXPECT_EQ(macros[1].Name, nullptr);
    EXPECT_EQ(macros[1].Definition, nullptr);
}

TEST(ShaderCompile, EncodeSrgbMacroOffForHdrAndSrgbRtv)
{
    EXPECT_FALSE(encodeSrgbForColorFormat(DXGI_FORMAT_R16G16B16A16_FLOAT));
    EXPECT_FALSE(encodeSrgbForColorFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));
    D3D_SHADER_MACRO macros[2]{};
    makeEncodeSrgbMacros(false, macros);
    EXPECT_STREQ(macros[0].Definition, "0");
}

TEST(ShaderCompile, PassesDefinesToD3DCompile)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "de_shader_macro_test.hlsl";
    ScopedTempFile              guard(path);
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.good());
        file << "#if WANT_FAIL\n#error should not compile\n#endif\n"
                "float4 PSMain() : SV_TARGET { return 0; }\n";
        ASSERT_TRUE(file.good());
    }

    ComPtr<ID3DBlob> blob;
    D3D_SHADER_MACRO failMacros[] = { { "WANT_FAIL", "1" }, { nullptr, nullptr } };
    EXPECT_FALSE(compileShaderFromFile(path, "PSMain", "ps_5_0", blob, failMacros));

    D3D_SHADER_MACRO okMacros[] = { { "WANT_FAIL", "0" }, { nullptr, nullptr } };
    EXPECT_TRUE(compileShaderFromFile(path, "PSMain", "ps_5_0", blob, okMacros));
    EXPECT_NE(blob.Get(), nullptr);

    blob.Reset();
    EXPECT_TRUE(compileShaderFromFile(path, "PSMain", "ps_5_0", blob));
    EXPECT_NE(blob.Get(), nullptr);
}

TEST(ShaderCompile, EncodeSrgbCompilesWithColorInclude)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/BasicMesh.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/BasicMesh.hlsl not on content roots";

    ComPtr<ID3DBlob> blob;
    D3D_SHADER_MACRO macros[2];
    makeEncodeSrgbMacros(true, macros);
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", "ps_5_0", blob, macros));
    makeEncodeSrgbMacros(false, macros);
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", "ps_5_0", blob, macros));
}

TEST(ShaderCompile, DeferredLightingIblCompiles)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/DeferredLighting.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/DeferredLighting.hlsl not on content roots";

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMain", "vs_5_0", vs));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", "ps_5_0", ps));
}

TEST(ShaderCompile, IblBakeCompiles)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/IblBake.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/IblBake.hlsl not on content roots";

    D3D_SHADER_MACRO equirectMacros[] = { { "IBL_SRC_EQUIRECT", "1" }, { nullptr, nullptr } };
    D3D_SHADER_MACRO cubeMacros[]     = { { "IBL_SRC_CUBE", "1" }, { nullptr, nullptr } };

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> psEquirect;
    ComPtr<ID3DBlob> psIrr;
    ComPtr<ID3DBlob> psPref;
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMain", "vs_5_0", vs, cubeMacros));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSEquirect", "ps_5_0", psEquirect, equirectMacros));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSIrradiance", "ps_5_0", psIrr, cubeMacros));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSPrefilter", "ps_5_0", psPref, cubeMacros));
}
