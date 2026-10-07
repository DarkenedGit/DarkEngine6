#include <gtest/gtest.h>

#include "Render/ShaderCompile.h"
#include "Render/SkyPipeline.h"
#include "Render/SsrPipeline.h"
#include "Render/WaterPipeline.h"
#include "Sky/CloudLayer.h"
#include "Sky/Environment.h"

#include <cmath>
#include <cstddef>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

using namespace Dark;
using namespace Dark::Math;
using namespace Dark::Sky;
using Microsoft::WRL::ComPtr;

namespace
{

ComPtr<ID3D12Device> TryCreateDevice()
{
    ComPtr<ID3D12Device>  device;
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return {};
    ComPtr<IDXGIAdapter> warp;
    if (SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))))
    {
        if (SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
            return device;
    }
    device.Reset();
    if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
        return device;
    return {};
}

} // namespace

TEST(CloudLayer, ShellHitStraightUpAndMisses)
{
    float t = -1.0f;
    EXPECT_TRUE(cloudShellHit(10.0f, 1.0f, 2000.0f, kCloudPlanetRadius, t));
    EXPECT_NEAR(t, 1990.0f, 2.0f);

    t = -1.0f;
    EXPECT_FALSE(cloudShellHit(10.0f, 0.0f, 2000.0f, kCloudPlanetRadius, t));
    EXPECT_FALSE(cloudShellHit(10.0f, -0.4f, 2000.0f, kCloudPlanetRadius, t));

    float cameraY = 2000.0f;
    EXPECT_FALSE(cloudShellHit(cameraY, 1.0f, 2000.0f, kCloudPlanetRadius, t));
    EXPECT_FLOAT_EQ(cameraY, 2000.0f);
}

TEST(CloudLayer, SanitizeClampsArtistKnobs)
{
    CloudLayerDesc layer{};
    layer.altitude     = 10.0f;
    layer.amount       = 9.0f;
    layer.windSpeedMps = -4.0f;
    sanitizeCloudLayer(layer);
    EXPECT_FLOAT_EQ(layer.altitude, 500.0f);
    EXPECT_FLOAT_EQ(layer.amount, 2.0f);
    EXPECT_FLOAT_EQ(layer.windSpeedMps, 0.0f);
}

TEST(CloudLayer, WindMovesWhenTimeIsHeld)
{
    Environment env;
    env.timeScale = 0.0f;
    env.evaluate();
    const Vector2f held = env.cloudWindBase();
    env.timeOfDay       = 3.0f;
    env.evaluate();
    EXPECT_FLOAT_EQ(env.cloudWindBase().x, held.x);
    EXPECT_FLOAT_EQ(env.cloudWindBase().y, held.y);
    EXPECT_FLOAT_EQ(env.cloudWindDetail().x, 0.0f);

    env.tick(0.5f);
    EXPECT_NEAR(env.cloudWindBase().Magnitude(), 4.0f, 1.0e-3f);
    EXPECT_NEAR(env.cloudWindDetail().Magnitude(), 6.0f, 1.0e-3f);
    EXPECT_FLOAT_EQ(env.timeScale, 0.0f);
}

TEST(CloudLayer, WeatherButtonsSetWindSpeed)
{
    EXPECT_FLOAT_EQ(cloudWindSpeedMpsFor(WeatherState::Clear()), 3.0f);
    EXPECT_FLOAT_EQ(cloudWindSpeedMpsFor(WeatherState::PartlyCloudy()), 8.0f);
    EXPECT_FLOAT_EQ(cloudWindSpeedMpsFor(WeatherState::Overcast()), 10.0f);
    EXPECT_FLOAT_EQ(cloudWindSpeedMpsFor(WeatherState::Storm()), 16.0f);
}

TEST(CloudLayer, WriteTailUsesAnalyticSky)
{
    Environment env;
    env.cloudLayer.amount       = 1.5f;
    env.cloudLayer.altitude     = 2500.0f;
    env.cloudLayer.windSpeedMps = 4.0f;
    env.evaluate();
    env.tick(0.25f);

    CloudLayerGpu gpu{};
    writeCloudLayer(gpu, env);
    EXPECT_FLOAT_EQ(gpu.clLightColor[3], 1.5f);
    EXPECT_FLOAT_EQ(gpu.clLightDir[3], 2500.0f);
    EXPECT_FLOAT_EQ(gpu.clSkyTop[0], env.skyZenith().x);
    EXPECT_FLOAT_EQ(gpu.clSkyTop[1], env.skyZenith().y);
    EXPECT_FLOAT_EQ(gpu.clSkyTop[2], env.skyZenith().z);
    EXPECT_FLOAT_EQ(gpu.clSkyTop[3], 0.0f);
    EXPECT_FLOAT_EQ(gpu.clSkyBottom[0], env.skyHorizon().x);
    EXPECT_FLOAT_EQ(gpu.clSkyBottom[3], 0.0f);
    EXPECT_GT(gpu.clWind[0] * gpu.clWind[0] + gpu.clWind[1] * gpu.clWind[1], 0.1f);
    const float dir2 = gpu.clLightDir[0] * gpu.clLightDir[0] + gpu.clLightDir[1] * gpu.clLightDir[1] + gpu.clLightDir[2] * gpu.clLightDir[2];
    EXPECT_NEAR(dir2, 1.0f, 1.0e-4f);
}

TEST(CloudLayer, ConstantLayout)
{
    EXPECT_EQ(sizeof(SkyEvalParams), 44u * sizeof(float));
    EXPECT_EQ(offsetof(SkyEvalParams, sunDir), 0u);
    EXPECT_EQ(offsetof(SkyEvalParams, coverage), 3u * sizeof(float));
    EXPECT_EQ(offsetof(SkyEvalParams, cloudTime), 20u * sizeof(float));
    EXPECT_EQ(offsetof(SkyEvalParams, clLightColor), 24u * sizeof(float));
    EXPECT_EQ(SkyPipeline::kRootParameterCount, 3u);
    EXPECT_EQ((sizeof(WaterFrameConstants) + 255u) & ~255u, 768u);
    EXPECT_EQ(SsrPipeline::kSkyEvalCbBytes, 256u);
}

TEST(CloudLayer, DeviceCreatesDeckPso)
{
    if (resolveContentPath("shaders/Sky.hlsl").empty())
        GTEST_SKIP() << "shaders/Sky.hlsl not on content roots";

    ComPtr<ID3D12Device> device = TryCreateDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";

    SkyPipeline sky;
    ASSERT_TRUE(sky.create(device.Get(), SkyPass::DeferredLast, DXGI_FORMAT_R16G16B16A16_FLOAT));
    EXPECT_TRUE(sky.isValid());
    EXPECT_TRUE(sky.hasCloudLayer());
}
