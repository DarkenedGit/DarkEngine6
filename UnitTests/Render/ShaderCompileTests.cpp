#include <gtest/gtest.h>

#include "Render/ShaderCompile.h"

#include <d3d12.h>
#include <fstream>
#include <filesystem>

using Dark::compileShaderFromFile;
using Dark::makeThermalMacros;
using Dark::makeUvMacros;
using Dark::ShaderStage;
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

TEST(ShaderCompile, PassesDefinesToTheCompiler)
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
    EXPECT_FALSE(compileShaderFromFile(path, "PSMain", ShaderStage::Pixel, blob, failMacros));

    D3D_SHADER_MACRO okMacros[] = { { "WANT_FAIL", "0" }, { nullptr, nullptr } };
    EXPECT_TRUE(compileShaderFromFile(path, "PSMain", ShaderStage::Pixel, blob, okMacros));
    EXPECT_NE(blob.Get(), nullptr);

    blob.Reset();
    EXPECT_TRUE(compileShaderFromFile(path, "PSMain", ShaderStage::Pixel, blob));
    EXPECT_NE(blob.Get(), nullptr);
}

TEST(ShaderCompile, DeferredLightingIblCompiles)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/DeferredLighting.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/DeferredLighting.hlsl not on content roots";

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMain", ShaderStage::Vertex, vs));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", ShaderStage::Pixel, ps));
}

TEST(ShaderCompile, TerrainGBufferCompiles)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/TerrainGBuffer.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/TerrainGBuffer.hlsl not on content roots";

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMain", ShaderStage::Vertex, vs));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", ShaderStage::Pixel, ps));
    D3D_SHADER_MACRO thermalMacros[3];
    makeThermalMacros("0.41", thermalMacros);
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", ShaderStage::Pixel, ps, thermalMacros));
    D3D_SHADER_MACRO uvMacros[3];
    makeUvMacros("0.12", uvMacros);
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", ShaderStage::Pixel, ps, uvMacros));
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
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMain", ShaderStage::Vertex, vs, cubeMacros));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSEquirect", ShaderStage::Pixel, psEquirect, equirectMacros));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSIrradiance", ShaderStage::Pixel, psIrr, cubeMacros));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSPrefilter", ShaderStage::Pixel, psPref, cubeMacros));
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
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMain", ShaderStage::Vertex, vs));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSGtao", ShaderStage::Pixel, psGtao));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSUpsampleTemporal", ShaderStage::Pixel, psUp));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSCompose", ShaderStage::Pixel, psComp));
}

TEST(ShaderCompile, Decal)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/Decal.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/Decal.hlsl not on content roots";

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> vsFs;
    ComPtr<ID3DBlob> ps;
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMain", ShaderStage::Vertex, vs));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSFullscreen", ShaderStage::Vertex, vsFs));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", ShaderStage::Pixel, ps));
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
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMain", ShaderStage::Vertex, vs));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSTrace", ShaderStage::Pixel, psTrace));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSUpsampleTemporal", ShaderStage::Pixel, psUp));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSDownsample", ShaderStage::Pixel, psDown));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSDebugConf", ShaderStage::Pixel, psDebug));
}

TEST(ShaderCompile, SkyEval_Included)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/Sky.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/Sky.hlsl not on content roots";

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMainDeferred", ShaderStage::Vertex, vs));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", ShaderStage::Pixel, ps));
}

TEST(ShaderCompile, SkyCloudLayer)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/Sky.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/Sky.hlsl not on content roots";

    const D3D_SHADER_MACRO off[] = {
        { "CLOUD_LAYER", "0" },
        { nullptr, nullptr },
    };
    const D3D_SHADER_MACRO on[] = {
        { "CLOUD_LAYER", "1" },
        { nullptr, nullptr },
    };
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> psOff;
    ComPtr<ID3DBlob> psOn;
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMainDeferred", ShaderStage::Vertex, vs, off));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", ShaderStage::Pixel, psOff, off));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", ShaderStage::Pixel, psOn, on));
}

TEST(ShaderCompile, Underwater)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/Underwater.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/Underwater.hlsl not on content roots";

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMain", ShaderStage::Vertex, vs));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", ShaderStage::Pixel, ps));
}

TEST(ShaderCompile, Water)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/Water.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/Water.hlsl not on content roots";

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMain", ShaderStage::Vertex, vs));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSMain", ShaderStage::Pixel, ps));
}

TEST(ShaderCompile, AutoExposure)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/AutoExposure.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/AutoExposure.hlsl not on content roots";

    const D3D_SHADER_MACRO reduceMacros[] = {
        { "AE_REDUCE", "1" },
        { nullptr, nullptr },
    };
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> reduce;
    ComPtr<ID3DBlob> down;
    EXPECT_TRUE(compileShaderFromFile(hlsl, "VSMain", ShaderStage::Vertex, vs));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSReduce", ShaderStage::Pixel, reduce, reduceMacros));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "PSDownsample", ShaderStage::Pixel, down));
}

TEST(ShaderCompile, TerrainErosion)
{
    const std::filesystem::path hlsl = Dark::resolveContentPath("shaders/TerrainErosion.hlsl");
    if (hlsl.empty())
        GTEST_SKIP() << "shaders/TerrainErosion.hlsl not on content roots";

    ComPtr<ID3DBlob> thermal;
    ComPtr<ID3DBlob> pipe;
    EXPECT_TRUE(compileShaderFromFile(hlsl, "CSThermal", ShaderStage::Compute, thermal));
    EXPECT_TRUE(compileShaderFromFile(hlsl, "CSPipe", ShaderStage::Compute, pipe));
    EXPECT_NE(thermal.Get(), nullptr);
    EXPECT_NE(pipe.Get(), nullptr);
}

TEST(ShaderCompile, InstancedMesh)
{
    const std::filesystem::path gbuffer = Dark::resolveContentPath("shaders/BasicMeshGBufferInstanced.hlsl");
    const std::filesystem::path depth   = Dark::resolveContentPath("shaders/BasicMeshDepthInstanced.hlsl");
    if (gbuffer.empty() || depth.empty())
        GTEST_SKIP() << "instanced mesh shaders not on content roots";

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    ComPtr<ID3DBlob> depthVs;
    EXPECT_TRUE(compileShaderFromFile(gbuffer, "VSMain", ShaderStage::Vertex, vs));
    EXPECT_TRUE(compileShaderFromFile(gbuffer, "PSMain", ShaderStage::Pixel, ps));
    D3D_SHADER_MACRO thermalMacros[3];
    makeThermalMacros("0.41", thermalMacros);
    EXPECT_TRUE(compileShaderFromFile(gbuffer, "PSMain", ShaderStage::Pixel, ps, thermalMacros));
    D3D_SHADER_MACRO uvMacros[3];
    makeUvMacros("0.00", uvMacros);
    // gUv is register(t0, space1). ps_5_0 rejects register spaces.
    EXPECT_TRUE(compileShaderFromFile(gbuffer, "PSMain", ShaderStage::Pixel, ps, uvMacros));
    EXPECT_TRUE(compileShaderFromFile(depth, "VSMain", ShaderStage::Vertex, depthVs));
    EXPECT_NE(vs.Get(), nullptr);
    EXPECT_NE(ps.Get(), nullptr);
    EXPECT_NE(depthVs.Get(), nullptr);
}

TEST(ShaderCompile, ReverseZSceneShaders)
{
    struct Case
    {
        const char* file;
        const char* entry;
        ShaderStage stage;
    };
    const Case cases[] = {
        { "shaders/Sky.hlsl", "VSMainDeferred", ShaderStage::Vertex },
        { "shaders/Sky.hlsl", "PSMain", ShaderStage::Pixel },
        { "shaders/Taa.hlsl", "PSMain", ShaderStage::Pixel },
        { "shaders/MotionBlur.hlsl", "PSMain", ShaderStage::Pixel },
        { "shaders/Tonemap.hlsl", "PSMain", ShaderStage::Pixel },
        { "shaders/LocalLightVolume.hlsl", "PSMain", ShaderStage::Pixel },
    };
    for (const auto& c : cases)
    {
        const std::filesystem::path hlsl = Dark::resolveContentPath(c.file);
        if (hlsl.empty())
            GTEST_SKIP() << c.file << " not on content roots";
        ComPtr<ID3DBlob> blob;
        EXPECT_TRUE(compileShaderFromFile(hlsl, c.entry, c.stage, blob)) << c.file << " " << c.entry;
    }
}
