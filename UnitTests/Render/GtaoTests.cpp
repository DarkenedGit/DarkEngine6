#include <gtest/gtest.h>

#include "Render/Camera3D.h"
#include "Render/GtaoPipeline.h"
#include "Render/SceneBuffers.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <type_traits>
#include <utility>
#include <wrl/client.h>

using Dark::Camera3D;
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
