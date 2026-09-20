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

TEST(ShaderCompile, TerrainGBufferCompiles)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/TerrainGBuffer.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/TerrainGBuffer.hlsl not on content roots";

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMain", "vs_5_0", vs));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", "ps_5_0", ps));
}

TEST(ShaderCompile, TerrainForwardCompiles)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/Terrain.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/Terrain.hlsl not on content roots";

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    D3D_SHADER_MACRO macros[2];
    makeEncodeSrgbMacros(true, macros);
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMain", "vs_5_0", vs, macros));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", "ps_5_0", ps, macros));
    makeEncodeSrgbMacros(false, macros);
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", "ps_5_0", ps, macros));
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

TEST(ShaderCompile, Gtao)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/Gtao.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/Gtao.hlsl not on content roots";

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> psGtao;
    ComPtr<ID3DBlob> psUp;
    ComPtr<ID3DBlob> psComp;
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMain", "vs_5_0", vs));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSGtao", "ps_5_0", psGtao));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSUpsampleTemporal", "ps_5_0", psUp));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSCompose", "ps_5_0", psComp));
}

TEST(ShaderCompile, Ssr)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/Ssr.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/Ssr.hlsl not on content roots";

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> psTrace;
    ComPtr<ID3DBlob> psUp;
    ComPtr<ID3DBlob> psDown;
    ComPtr<ID3DBlob> psDebug;
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMain", "vs_5_0", vs));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSTrace", "ps_5_0", psTrace));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSUpsampleTemporal", "ps_5_0", psUp));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSDownsample", "ps_5_0", psDown));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSDebugConf", "ps_5_0", psDebug));
}

TEST(ShaderCompile, SkyEval_Included)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/Sky.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/Sky.hlsl not on content roots";

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMainDeferred", "vs_5_0", vs));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", "ps_5_0", ps));
}

TEST(ShaderCompile, ReverseZSceneShaders)
{
    const char* cases[][3] = {
        { "shaders/Sky.hlsl", "VSMainDeferred", "vs_5_0" },
        { "shaders/Taa.hlsl", "PSMain", "ps_5_0" },
        { "shaders/MotionBlur.hlsl", "PSMain", "ps_5_0" },
        { "shaders/Tonemap.hlsl", "PSMain", "ps_5_0" },
        { "shaders/LocalLightVolume.hlsl", "PSMain", "ps_5_0" },
    };
    for (const auto& c : cases)
    {
        const std::filesystem::path hlsl = Dark::resolveContentPath(c[0]);
        if (hlsl.empty())
            GTEST_SKIP() << c[0] << " not on content roots";
        ComPtr<ID3DBlob> blob;
        EXPECT_TRUE(compileShaderFromFile(hlsl, c[1], c[2], blob)) << c[0] << " " << c[1];
    }
}
