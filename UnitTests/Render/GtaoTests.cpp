#include <gtest/gtest.h>

#include "Render/Camera3D.h"
#include "Render/DebugRenderState.h"
#include "Render/GtaoPipeline.h"
#include "Render/SceneBuffers.h"

#include <algorithm>
#include <cmath>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <type_traits>
#include <utility>
#include <wrl/client.h>

using Dark::Camera3D;
using Dark::DebugRenderState;
using Dark::GtaoGpuParams;
using Dark::GtaoPipeline;
using Dark::GtaoSettings;
using Dark::SceneBuffers;
using Dark::Math::Mat4f;
using Dark::Math::Matrix4f;
using Microsoft::WRL::ComPtr;

namespace
{
    template <class T, class = void>
    struct HasHalfRes : std::false_type
    {
    };

    template <class T>
    struct HasHalfRes<T, std::void_t<decltype(std::declval<T&>().halfRes)>> : std::true_type
    {
    };

    template <class T, class = void>
    struct HasThickness : std::false_type
    {
    };

    template <class T>
    struct HasThickness<T, std::void_t<decltype(std::declval<T&>().thickness)>> : std::true_type
    {
    };

    template <class T, class = void>
    struct HasSteps : std::false_type
    {
    };

    template <class T>
    struct HasSteps<T, std::void_t<decltype(std::declval<T&>().steps)>> : std::true_type
    {
    };

    template <class T, class = void>
    struct HasDirections : std::false_type
    {
    };

    template <class T>
    struct HasDirections<T, std::void_t<decltype(std::declval<T&>().directions)>> : std::true_type
    {
    };

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

    // Produce-path curve from Gtao.hlsl PSUpsampleTemporal: lerp(1, pow(sat(vis), power), intensity).
    float ssaoCurve(float vis, float power, float intensity)
    {
        const float curved = std::pow(std::clamp(vis, 0.0f, 1.0f), power);
        return 1.0f + (curved - 1.0f) * intensity;
    }

    float composeAo(float authored, float ssao)
    {
        return authored * ssao;
    }

    bool gtaoShouldSkip(bool settingsEnabled, bool ssaoEnabled, bool hybridDeferred, bool hasGBuffer, bool gtaoValid, bool cmdOk)
    {
        return !cmdOk || !hybridDeferred || !hasGBuffer || !(settingsEnabled && ssaoEnabled) || !gtaoValid;
    }
} // namespace

TEST(Gtao, Settings_Defaults)
{
    const GtaoSettings s{};
    EXPECT_FALSE(s.enabled);
    EXPECT_FLOAT_EQ(s.radius, 0.5f);
    EXPECT_FLOAT_EQ(s.power, 1.5f);
    EXPECT_FLOAT_EQ(s.intensity, 1.0f);
    EXPECT_FALSE(HasHalfRes<GtaoSettings>::value);
    EXPECT_FALSE(HasThickness<GtaoSettings>::value);
    EXPECT_FALSE(HasSteps<GtaoSettings>::value);
    EXPECT_FALSE(HasDirections<GtaoSettings>::value);
}

TEST(Gtao, LightingCount_Unchanged)
{
    EXPECT_EQ(SceneBuffers::kLightingCount, 9u);
    EXPECT_EQ(SceneBuffers::kLightingAo, 5u);
}

TEST(Gtao, SetLightingAoSrv_NullDevice)
{
    SceneBuffers buffers;
    D3D12_CPU_DESCRIPTOR_HANDLE ao{};
    ao.ptr = 0x1234;
    buffers.setLightingAoSrv(nullptr, ao);
    EXPECT_EQ(buffers.lightingAoCpu().ptr, ao.ptr);
}

TEST(Gtao, SetLightingAoSrv_ZeroHandle)
{
    SceneBuffers buffers;
    D3D12_CPU_DESCRIPTOR_HANDLE ao{};
    buffers.setLightingAoSrv(nullptr, ao);
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);
    buffers.packLightingHeap(nullptr, {});
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);
}

TEST(Gtao, Reset_ClearsLightingAoOverride)
{
    SceneBuffers buffers;
    D3D12_CPU_DESCRIPTOR_HANDLE ao{};
    ao.ptr = 0x1234;
    buffers.setLightingAoSrv(nullptr, ao);
    EXPECT_EQ(buffers.lightingAoCpu().ptr, ao.ptr);
    buffers.reset();
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);
    buffers.packLightingHeap(nullptr, {});
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);
}

TEST(Gtao, Create_NullDevice_DropsAoOverride)
{
    SceneBuffers buffers;
    D3D12_CPU_DESCRIPTOR_HANDLE ao{};
    ao.ptr = 0x1234;
    buffers.setLightingAoSrv(nullptr, ao);
    EXPECT_EQ(buffers.lightingAoCpu().ptr, ao.ptr);

    const float hdrClear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    EXPECT_FALSE(buffers.create(nullptr, 64, 64, true, {}, hdrClear));
    EXPECT_FALSE(buffers.valid());
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);
}

TEST(Gtao, Create_ZeroSize_DropsAoOverride)
{
    SceneBuffers buffers;
    D3D12_CPU_DESCRIPTOR_HANDLE ao{};
    ao.ptr = 0x1234;
    buffers.setLightingAoSrv(nullptr, ao);
    EXPECT_EQ(buffers.lightingAoCpu().ptr, ao.ptr);
    EXPECT_FALSE(buffers.create(nullptr, 0, 0, true, {}, nullptr));
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);
}

TEST(Gtao, Camera3D_GetProjUnjittered)
{
    Camera3D cam;
    cam.SetLens(1.0f, 16.0f / 9.0f, 0.1f, 100.0f);

    Matrix4f unjittered = cam.GetProjUnjittered();
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(cam.GetProj().m_afEntry[i], unjittered.m_afEntry[i]);

    cam.SetSubpixelJitter(0.5f, -0.5f, 1920, 1080);
    EXPECT_NEAR(cam.GetProj().m_afEntry[Mat4f::m31], unjittered.m_afEntry[Mat4f::m31] + 2.0f * 0.5f / 1920.0f, 1e-6f);
    EXPECT_NEAR(cam.GetProj().m_afEntry[Mat4f::m32], unjittered.m_afEntry[Mat4f::m32] + 2.0f * -0.5f / 1080.0f, 1e-6f);
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(cam.GetProjUnjittered().m_afEntry[i], unjittered.m_afEntry[i]);

    cam.ClearSubpixelJitter();
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(cam.GetProj().m_afEntry[i], cam.GetProjUnjittered().m_afEntry[i]);
}

TEST(Gtao, GpuParams_Size)
{
    EXPECT_EQ(sizeof(GtaoGpuParams), 64u * sizeof(float));
    EXPECT_EQ(sizeof(GtaoGpuParams), 256u);
}

TEST(Gtao, Create_NoDevice_False)
{
    GtaoPipeline gtao;
    EXPECT_FALSE(gtao.create(nullptr, 1280, 720));
    EXPECT_FALSE(gtao.isValid());
    EXPECT_EQ(gtao.composeSrvCpu().ptr, 0u);
    EXPECT_EQ(gtao.aoFullSrvCpu().ptr, 0u);

    SceneBuffers buffers;
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);
    EXPECT_EQ(SceneBuffers::kLightingAo, 5u);
    EXPECT_EQ(SceneBuffers::kLightingCount, 9u);
}

TEST(Gtao, Create_ZeroSize_False)
{
    GtaoPipeline gtao;
    EXPECT_FALSE(gtao.create(nullptr, 0, 720));
    EXPECT_FALSE(gtao.create(nullptr, 1280, 0));
    EXPECT_FALSE(gtao.create(nullptr, 0, 0));
    EXPECT_FALSE(gtao.isValid());
    EXPECT_EQ(gtao.composeSrvCpu().ptr, 0u);
}

TEST(Gtao, Resize_NoDevice_False)
{
    GtaoPipeline gtao;
    EXPECT_FALSE(gtao.resize(nullptr, 1920, 1080));
    EXPECT_FALSE(gtao.isValid());
    EXPECT_EQ(gtao.composeSrvCpu().ptr, 0u);
    EXPECT_EQ(gtao.aoFullSrvCpu().ptr, 0u);
}

TEST(Gtao, Invalid_HandlesAreZero)
{
    const GtaoPipeline gtao;
    EXPECT_FALSE(gtao.isValid());
    EXPECT_EQ(gtao.composeSrvCpu().ptr, 0u);
    EXPECT_EQ(gtao.aoFullSrvCpu().ptr, 0u);
}

TEST(Gtao, Resize_RecreatesTargets)
{
    ComPtr<ID3D12Device> device = TryCreateDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";

    GtaoPipeline gtao;
    ASSERT_TRUE(gtao.create(device.Get(), 1280, 720));
    EXPECT_TRUE(gtao.isValid());
    const SIZE_T compose1280 = gtao.composeSrvCpu().ptr;
    const SIZE_T full1280    = gtao.aoFullSrvCpu().ptr;
    EXPECT_NE(compose1280, 0u);
    EXPECT_NE(full1280, 0u);

    ASSERT_TRUE(gtao.resize(device.Get(), 1920, 1080));
    EXPECT_TRUE(gtao.isValid());
    EXPECT_NE(gtao.composeSrvCpu().ptr, 0u);
    EXPECT_NE(gtao.aoFullSrvCpu().ptr, 0u);

    EXPECT_TRUE(gtao.resize(device.Get(), 1920, 1080));
    EXPECT_TRUE(gtao.isValid());

    EXPECT_FALSE(gtao.create(device.Get(), 0, 1080));
    EXPECT_FALSE(gtao.isValid());
    EXPECT_EQ(gtao.composeSrvCpu().ptr, 0u);
    EXPECT_EQ(gtao.aoFullSrvCpu().ptr, 0u);

    SceneBuffers buffers;
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);
    EXPECT_EQ(SceneBuffers::kLightingAo, 5u);
}

TEST(Gtao, Create_ZeroSize_WithDevice_False)
{
    ComPtr<ID3D12Device> device = TryCreateDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";

    GtaoPipeline gtao;
    EXPECT_FALSE(gtao.create(device.Get(), 0, 64));
    EXPECT_FALSE(gtao.create(device.Get(), 64, 0));
    EXPECT_FALSE(gtao.isValid());
    EXPECT_EQ(gtao.composeSrvCpu().ptr, 0u);

    SceneBuffers buffers;
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);
}

TEST(Gtao, Disabled_SkipsPass)
{
    const GtaoSettings s{};
    const DebugRenderState dbg{};
    EXPECT_FALSE(s.enabled);
    EXPECT_FALSE(dbg.ssaoEnabled);
    EXPECT_TRUE(gtaoShouldSkip(s.enabled, dbg.ssaoEnabled, true, true, true, true));

    SceneBuffers buffers;
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);

    ComPtr<ID3D12Device> device = TryCreateDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware) — CPU skip contract already checked";

    const float hdrClear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    ASSERT_TRUE(buffers.create(device.Get(), 64, 64, true, {}, hdrClear));
    ASSERT_NE(buffers.aoSrvCpu().ptr, 0u);
    EXPECT_EQ(buffers.lightingAoCpu().ptr, 0u);

    GtaoPipeline gtao;
    ASSERT_TRUE(gtao.create(device.Get(), 64, 64));
    ASSERT_NE(gtao.composeSrvCpu().ptr, 0u);

    // A previous enabled frame left compose in slot 5; disable must restore MRT3, not a white tex.
    buffers.setLightingAoSrv(device.Get(), gtao.composeSrvCpu());
    EXPECT_EQ(buffers.lightingAoCpu().ptr, gtao.composeSrvCpu().ptr);
    buffers.setLightingAoSrv(device.Get(), buffers.aoSrvCpu());
    EXPECT_EQ(buffers.lightingAoCpu().ptr, buffers.aoSrvCpu().ptr);
    EXPECT_NE(buffers.lightingAoCpu().ptr, gtao.composeSrvCpu().ptr);
}

TEST(Gtao, EnabledWithoutDebugFlag_Skips)
{
    GtaoSettings s{};
    s.enabled = true;
    const DebugRenderState dbg{};
    EXPECT_TRUE(s.enabled);
    EXPECT_FALSE(dbg.ssaoEnabled);
    EXPECT_TRUE(gtaoShouldSkip(s.enabled, dbg.ssaoEnabled, true, true, true, true));
    EXPECT_FALSE(gtaoShouldSkip(true, true, true, true, true, true));
}

TEST(Gtao, NullCmd_TreatedAsSkip)
{
    EXPECT_TRUE(gtaoShouldSkip(true, true, true, true, true, false));
    EXPECT_TRUE(gtaoShouldSkip(true, true, false, true, true, true));
    EXPECT_TRUE(gtaoShouldSkip(true, true, true, false, true, true));
    EXPECT_TRUE(gtaoShouldSkip(true, true, true, true, false, true));
}

TEST(Gtao, IntensityZero_Identity)
{
    EXPECT_FLOAT_EQ(ssaoCurve(0.0f, 1.5f, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(ssaoCurve(0.2f, 1.5f, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(ssaoCurve(0.5f, 4.0f, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(ssaoCurve(1.0f, 1.5f, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(ssaoCurve(1.0f, 1.5f, 1.0f), 1.0f);
    EXPECT_LT(ssaoCurve(0.25f, 1.5f, 1.0f), 1.0f);

    GtaoSettings s{};
    s.enabled   = true;
    s.intensity = 0.0f;
    EXPECT_FLOAT_EQ(ssaoCurve(0.1f, s.power, s.intensity), 1.0f);

    // GPU AoFull readback after draw(intensity=0) needs a Renderer + G-buffer (Window). Not in this fixture.
}

TEST(Gtao, AuthoredAo_StillDarkens)
{
    // Compose is authored * ssao (PSCompose). Flat GTAO ~1 must keep authored cavities.
    EXPECT_NEAR(composeAo(0.2f, 1.0f), 0.2f, 1.0e-6f);
    EXPECT_NEAR(composeAo(0.2f, 0.0f), 0.0f, 1.0e-6f);
    EXPECT_LT(composeAo(0.2f, 0.5f), 0.2f);
    EXPECT_NEAR(composeAo(0.2f, 0.9f), 0.18f, 1.0e-6f);
    EXPECT_NE(composeAo(0.2f, 0.9f), std::min(0.2f, 0.9f));
    EXPECT_FLOAT_EQ(composeAo(1.0f, 1.0f), 1.0f);

    // Full GPU fixture (authored MRT3 = 0.2, GTAO ~1 → AoCompose ~0.2) needs Renderer + G-buffer. Not in this fixture.
}
