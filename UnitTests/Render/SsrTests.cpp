#include <gtest/gtest.h>

#include "Render/DebugRenderState.h"
#include "Render/DeferredLightingPipeline.h"
#include "Render/Renderer.h"
#include "Render/SceneBuffers.h"
#include "Render/SsrSettings.h"

#include <cstddef>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <type_traits>
#include <utility>
#include <wrl/client.h>

using Dark::DebugRenderState;
using Dark::DeferredLightingPipeline;
using Dark::LightingConstants;
using Dark::Renderer;
using Dark::SceneBuffers;
using Dark::SsrSettings;
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
    struct HasMaxSteps : std::false_type
    {
    };

    template <class T>
    struct HasMaxSteps<T, std::void_t<decltype(std::declval<T&>().maxSteps)>> : std::true_type
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
} // namespace

TEST(Ssr, Settings_Defaults)
{
    const SsrSettings s{};
    EXPECT_FALSE(s.enabled);
    EXPECT_FLOAT_EQ(s.maxRoughness, 0.4f);
    EXPECT_FLOAT_EQ(s.thickness, 0.2f);
    EXPECT_FLOAT_EQ(s.stride, 2.0f);
    EXPECT_FLOAT_EQ(s.edgeFade, 0.1f);
    EXPECT_FALSE(HasHalfRes<SsrSettings>::value);
    EXPECT_FALSE(HasMaxSteps<SsrSettings>::value);
}

TEST(Ssr, LightingCount_IsTen)
{
    EXPECT_EQ(SceneBuffers::kLightingSsr, 9u);
    EXPECT_EQ(SceneBuffers::kLightingCount, 10u);
    EXPECT_EQ(SceneBuffers::kLightingIblBrdfLut + 1u, SceneBuffers::kLightingSsr);
    EXPECT_EQ(SceneBuffers::kLightingSsr + 1u, SceneBuffers::kLightingCount);
}

TEST(Ssr, LightingRs_DwordBudget)
{
    const UINT cbDwords = static_cast<UINT>(sizeof(LightingConstants) / sizeof(float));
    EXPECT_EQ(cbDwords, 56u);
    EXPECT_EQ(DeferredLightingPipeline::kRootSsrSrv, 6u);
    EXPECT_EQ(DeferredLightingPipeline::kRootIblSrv + 1u, DeferredLightingPipeline::kRootSsrSrv);
    EXPECT_LE(cbDwords + 1u + 2u + 1u + 1u + 1u + 1u, 64u);
    EXPECT_EQ(cbDwords + 1u + 2u + 1u + 1u + 1u + 1u, 63u);
}

TEST(Ssr, LightingConstants_Still56)
{
    LightingConstants lc{};
    EXPECT_EQ(sizeof(LightingConstants), 56u * sizeof(float));
    EXPECT_EQ(offsetof(LightingConstants, ssrEnabled), 46u * sizeof(float));
    EXPECT_EQ(offsetof(LightingConstants, pbrLightColor), 48u * sizeof(float));
    EXPECT_FLOAT_EQ(lc.ssrEnabled, 0.0f);
}

TEST(Ssr, DebugRenderState_Defaults)
{
    const DebugRenderState s{};
    EXPECT_FALSE(s.ssrEnabled);
    EXPECT_EQ(s.ssrDebug, 0);
}

TEST(Ssr, SetLightingSsrSrv_NullDevice)
{
    SceneBuffers                buffers;
    D3D12_CPU_DESCRIPTOR_HANDLE ssr{};
    ssr.ptr = 0x1234;
    buffers.setLightingSsrSrv(nullptr, ssr);
    EXPECT_EQ(buffers.lightingSsrCpu().ptr, ssr.ptr);
}

TEST(Ssr, SetLightingSsrSrv_ZeroHandle)
{
    SceneBuffers                buffers;
    D3D12_CPU_DESCRIPTOR_HANDLE ssr{};
    buffers.setLightingSsrSrv(nullptr, ssr);
    EXPECT_EQ(buffers.lightingSsrCpu().ptr, 0u);
    buffers.packLightingHeap(nullptr, {});
    EXPECT_EQ(buffers.lightingSsrCpu().ptr, 0u);
    EXPECT_EQ(buffers.ssrTableGpu().ptr, 0u);
}

TEST(Ssr, Reset_ClearsLightingSsrOverride)
{
    SceneBuffers                buffers;
    D3D12_CPU_DESCRIPTOR_HANDLE ssr{};
    ssr.ptr = 0x1234;
    buffers.setLightingSsrSrv(nullptr, ssr);
    EXPECT_EQ(buffers.lightingSsrCpu().ptr, ssr.ptr);
    buffers.reset();
    EXPECT_EQ(buffers.lightingSsrCpu().ptr, 0u);
    EXPECT_EQ(buffers.ssrTableGpu().ptr, 0u);
    buffers.packLightingHeap(nullptr, {});
    EXPECT_EQ(buffers.lightingSsrCpu().ptr, 0u);
}

TEST(Ssr, Create_NullDevice_DropsSsrOverride)
{
    SceneBuffers                buffers;
    D3D12_CPU_DESCRIPTOR_HANDLE ssr{};
    ssr.ptr = 0x1234;
    buffers.setLightingSsrSrv(nullptr, ssr);
    EXPECT_EQ(buffers.lightingSsrCpu().ptr, ssr.ptr);

    const float hdrClear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    EXPECT_FALSE(buffers.create(nullptr, 64, 64, true, {}, hdrClear));
    EXPECT_FALSE(buffers.valid());
    EXPECT_EQ(buffers.lightingSsrCpu().ptr, 0u);
    EXPECT_EQ(buffers.ssrTableGpu().ptr, 0u);
}

TEST(Ssr, Create_ZeroSize_DropsSsrOverride)
{
    SceneBuffers                buffers;
    D3D12_CPU_DESCRIPTOR_HANDLE ssr{};
    ssr.ptr = 0x1234;
    buffers.setLightingSsrSrv(nullptr, ssr);
    EXPECT_EQ(buffers.lightingSsrCpu().ptr, ssr.ptr);
    EXPECT_FALSE(buffers.create(nullptr, 0, 0, true, {}, nullptr));
    EXPECT_EQ(buffers.lightingSsrCpu().ptr, 0u);
    EXPECT_EQ(buffers.ssrTableGpu().ptr, 0u);
}

TEST(Ssr, Dummy_ConfZero)
{
    // Renderer needs a Window; enableSceneBuffers packs via setLightingSsrSrv(ssrDummyCpu()).
    (void)&Renderer::ssrDummyCpu;
    (void)&Renderer::setLightingSsrSrv;
    (void)&Renderer::ssrTableGpu;

    ComPtr<ID3D12Device> device = TryCreateDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";

    SceneBuffers buffers;
    const float  hdrClear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    ASSERT_TRUE(buffers.create(device.Get(), 64, 64, true, {}, hdrClear));
    ASSERT_TRUE(buffers.hasGBuffer());
    ASSERT_NE(buffers.hdrSrvCpu().ptr, 0u);
    ASSERT_NE(buffers.lightingHeap(), nullptr);
    EXPECT_EQ(buffers.lightingHeap()->GetDesc().NumDescriptors, SceneBuffers::kLightingCount);
    EXPECT_EQ(buffers.lightingSsrCpu().ptr, 0u);

    const D3D12_CPU_DESCRIPTOR_HANDLE dummy = buffers.hdrSrvCpu();
    buffers.setLightingSsrSrv(device.Get(), dummy);
    EXPECT_EQ(buffers.lightingSsrCpu().ptr, dummy.ptr);
    EXPECT_NE(buffers.lightingSsrCpu().ptr, 0u);

    buffers.packLightingHeap(device.Get(), {});
    EXPECT_EQ(buffers.lightingSsrCpu().ptr, dummy.ptr);

    const UINT incr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_GPU_DESCRIPTOR_HANDLE slot9 = buffers.lightingTableGpu();
    slot9.ptr += static_cast<SIZE_T>(SceneBuffers::kLightingSsr) * incr;
    EXPECT_EQ(buffers.ssrTableGpu().ptr, slot9.ptr);

    buffers.reset();
    EXPECT_EQ(buffers.lightingSsrCpu().ptr, 0u);
    EXPECT_EQ(buffers.ssrTableGpu().ptr, 0u);
}
