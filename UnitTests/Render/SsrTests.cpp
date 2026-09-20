#include <gtest/gtest.h>

#include "Math/MathDefines.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"
#include "Render/Camera3D.h"
#include "Render/DebugRenderState.h"
#include "Render/DeferredLightingPipeline.h"
#include "Render/SceneBuffers.h"
#include "Render/SsrPipeline.h"
#include "Render/SsrSettings.h"

#include <cmath>
#include <cstddef>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <type_traits>
#include <utility>
#include <wrl/client.h>

using Dark::Camera3D;
using Dark::DebugRenderState;
using Dark::DeferredLightingPipeline;
using Dark::LightingConstants;
using Dark::SceneBuffers;
using Dark::SsrMissKind;
using Dark::SsrPipeline;
using Dark::SsrSettings;
using Dark::ssrClampDepthForReconstruct;
using Dark::ssrEdgeFadeAt;
using Dark::ssrHitViewZ;
using Dark::ssrIsSkyDepth;
using Dark::ssrLinearizeViewZ;
using Dark::ssrPassesRoughGate;
using Dark::ssrPerspectiveCorrectViewZ;
using Dark::ssrReconstructWorld;
using Dark::ssrReprojectionMatrix;
using Dark::ssrSetupDda;
using Dark::Math::Matrix4f;
using Dark::Math::Pi;
using Dark::Math::Vector3f;
using Dark::Math::Vector4f;
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

TEST(Ssr, GpuParams_Size)
{
    EXPECT_EQ(sizeof(Dark::SsrGpuParams), 64u * sizeof(float));
    EXPECT_EQ(sizeof(Dark::SsrGpuParams), 256u);
    EXPECT_EQ(sizeof(Dark::SsrGpuParams), SsrPipeline::kCbBytes);
}

TEST(Ssr, Cbv_CaptureSlotDistinctFromTrace)
{
    EXPECT_EQ(SsrPipeline::cbvByteOffset(0, false), 0u);
    EXPECT_EQ(SsrPipeline::cbvByteOffset(0, true), SsrPipeline::kCbBytes);
    EXPECT_EQ(SsrPipeline::cbvByteOffset(1, false), 2u * SsrPipeline::kCbBytes);
    EXPECT_EQ(SsrPipeline::cbvByteOffset(1, true), 3u * SsrPipeline::kCbBytes);
    EXPECT_EQ(SsrPipeline::cbvByteOffset(2, false), SsrPipeline::cbvByteOffset(0, false));
    EXPECT_EQ(SsrPipeline::cbvByteOffset(2, true), SsrPipeline::cbvByteOffset(0, true));
    EXPECT_NE(SsrPipeline::cbvByteOffset(0, false), SsrPipeline::cbvByteOffset(0, true));
    EXPECT_NE(SsrPipeline::cbvByteOffset(1, false), SsrPipeline::cbvByteOffset(1, true));
    EXPECT_EQ(SsrPipeline::kFrameCount * SsrPipeline::kCbSlotsPerFrame * SsrPipeline::kCbBytes, 1024u);
    EXPECT_LE(SsrPipeline::cbvByteOffset(1, true) + SsrPipeline::kCbBytes,
              SsrPipeline::kFrameCount * SsrPipeline::kCbSlotsPerFrame * SsrPipeline::kCbBytes);
}

TEST(Ssr, Project_RoundTrip_ReverseZ)
{
    Camera3D cam;
    cam.SetLens(Pi * 0.25f, 16.0f / 9.0f, 0.18f, 2000.0f);
    cam.LookAt(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));
    const Matrix4f vp  = cam.GetViewProj();
    const Matrix4f inv = vp.Inverse();

    auto roundTrip = [&](float viewZ) -> float {
        const Vector3f world(0.0f, 0.0f, viewZ);
        const Vector4f clip = vp * Vector4f(world, 1.0f);
        EXPECT_GT(std::fabs(clip.w), 1.0e-6f);
        if (std::fabs(clip.w) <= 1.0e-6f)
            return 0.0f;
        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        const float ndcZ = clip.z / clip.w;
        EXPECT_GT(ndcZ, 0.0f);
        const Vector3f rec   = ssrReconstructWorld(ndcX, ndcY, ndcZ, inv);
        const Vector4f clip2 = vp * Vector4f(rec, 1.0f);
        EXPECT_GT(std::fabs(clip2.w), 1.0e-6f);
        if (std::fabs(clip2.w) <= 1.0e-6f)
            return ndcZ;
        const float uvx  = clip.x / clip.w * 0.5f + 0.5f;
        const float uvy  = clip.y / clip.w * -0.5f + 0.5f;
        const float uvx2 = clip2.x / clip2.w * 0.5f + 0.5f;
        const float uvy2 = clip2.y / clip2.w * -0.5f + 0.5f;
        EXPECT_NEAR(uvx2, uvx, 1.0e-4f);
        EXPECT_NEAR(uvy2, uvy, 1.0e-4f);
        return ndcZ;
    };

    const float ndcNear = roundTrip(0.18f);
    const float ndcFar  = roundTrip(100.0f);
    EXPECT_NEAR(ndcNear, 1.0f, 1.0e-4f);
    EXPECT_LT(ndcFar, ndcNear);
    EXPECT_GT(ssrClampDepthForReconstruct(0.0f), 0.0f);
}

TEST(Ssr, IsSkyDepth_Miss)
{
    EXPECT_TRUE(ssrIsSkyDepth(0.0f));
    EXPECT_FALSE(ssrIsSkyDepth(1.0e-8f));
    EXPECT_FALSE(ssrIsSkyDepth(1.0f));
    EXPECT_FALSE(ssrIsSkyDepth(0.0f) == false);
}

TEST(Ssr, RoughGate)
{
    EXPECT_FALSE(ssrPassesRoughGate(0.41f, 0.4f));
    EXPECT_TRUE(ssrPassesRoughGate(0.4f, 0.4f));
    EXPECT_TRUE(ssrPassesRoughGate(0.2f, 0.4f));
    EXPECT_TRUE(ssrPassesRoughGate(0.0f, 0.4f));
}

TEST(Ssr, EdgeFade_ZeroAtBorder)
{
    EXPECT_FLOAT_EQ(ssrEdgeFadeAt(0.0f, 0.5f, 0.1f), 0.0f);
    EXPECT_FLOAT_EQ(ssrEdgeFadeAt(1.0f, 0.5f, 0.1f), 0.0f);
    EXPECT_FLOAT_EQ(ssrEdgeFadeAt(0.5f, 0.0f, 0.1f), 0.0f);
    EXPECT_FLOAT_EQ(ssrEdgeFadeAt(0.5f, 1.0f, 0.1f), 0.0f);
    EXPECT_FLOAT_EQ(ssrEdgeFadeAt(0.5f, 0.5f, 0.1f), 1.0f);
}

TEST(Ssr, Create_NullDevice_False)
{
    SsrPipeline ssr;
    EXPECT_FALSE(ssr.create(nullptr, 128, 128));
    EXPECT_FALSE(ssr.isValid());
    EXPECT_EQ(ssr.fullSrvCpu().ptr, 0u);
    EXPECT_EQ(ssr.debugConfSrvCpu().ptr, 0u);
    EXPECT_EQ(ssr.sceneColorSrvCpu().ptr, 0u);
    EXPECT_FALSE(ssr.hasSceneColor());
}

TEST(Ssr, Create_ZeroSize_False)
{
    SsrPipeline ssr;
    EXPECT_FALSE(ssr.create(nullptr, 0, 128));
    EXPECT_FALSE(ssr.create(nullptr, 128, 0));
    EXPECT_FALSE(ssr.create(nullptr, 0, 0));
    EXPECT_FALSE(ssr.isValid());
    EXPECT_EQ(ssr.fullSrvCpu().ptr, 0u);
}

TEST(Ssr, Create_ZeroSize_WithDevice_False)
{
    ComPtr<ID3D12Device> device = TryCreateDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";

    SsrPipeline ssr;
    EXPECT_FALSE(ssr.create(device.Get(), 0, 64));
    EXPECT_FALSE(ssr.create(device.Get(), 64, 0));
    EXPECT_FALSE(ssr.isValid());
    EXPECT_EQ(ssr.fullSrvCpu().ptr, 0u);
    EXPECT_FALSE(ssr.hasSceneColor());
}

TEST(Ssr, Invalid_HandlesAreZero)
{
    const SsrPipeline ssr;
    EXPECT_FALSE(ssr.isValid());
    EXPECT_EQ(ssr.fullSrvCpu().ptr, 0u);
    EXPECT_EQ(ssr.debugConfSrvCpu().ptr, 0u);
    EXPECT_EQ(ssr.sceneColorSrvCpu().ptr, 0u);
    EXPECT_FALSE(ssr.hasSceneColor());
}

TEST(Ssr, Create_ReadyHandles)
{
    ComPtr<ID3D12Device> device = TryCreateDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";

    SsrPipeline ssr;
    ASSERT_TRUE(ssr.create(device.Get(), 128, 64));
    EXPECT_TRUE(ssr.isValid());
    EXPECT_NE(ssr.fullSrvCpu().ptr, 0u);
    EXPECT_NE(ssr.debugConfSrvCpu().ptr, 0u);
    EXPECT_NE(ssr.sceneColorSrvCpu().ptr, 0u);
    EXPECT_FALSE(ssr.hasSceneColor());
    EXPECT_TRUE(ssr.resize(device.Get(), 128, 64));
    ASSERT_TRUE(ssr.resize(device.Get(), 192, 96));
    EXPECT_TRUE(ssr.isValid());
    EXPECT_FALSE(ssr.hasSceneColor());
}

TEST(Ssr, Floor_RdotVNear1_SkyMissNotOffscreen)
{
    Camera3D cam;
    cam.SetLens(Pi * 0.25f, 1.0f, 0.18f, 2000.0f);
    cam.LookAt(Vector3f(0.0f, 5.0f, 0.0f), Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f));

    const Vector3f n(0.0f, 1.0f, 0.0f);
    const Vector3f v(0.0f, 1.0f, 0.0f);
    const Vector3f r(0.0f, 1.0f, 0.0f);
    EXPECT_NEAR(n.Dot(v), 1.0f, 1.0e-5f);
    EXPECT_NEAR(r.Dot(v), 1.0f, 1.0e-5f);

    const auto setup = ssrSetupDda(Vector3f(0.0f, 0.0f, 0.0f), r, cam.GetViewProj(), cam.GetNearZ(), 1280.0f, 720.0f, 256.0f);
    EXPECT_LT(setup.lenPx, 1.0f);
    EXPECT_FALSE(setup.ready);
    EXPECT_EQ(setup.miss, SsrMissKind::Sky);
    EXPECT_NE(setup.miss, SsrMissKind::Offscreen);
}

TEST(Ssr, Dda_BothBehindNear_SkyMiss)
{
    Camera3D cam;
    cam.SetLens(Pi * 0.25f, 1.0f, 0.18f, 2000.0f);
    cam.LookAt(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));
    const auto setup = ssrSetupDda(Vector3f(0.0f, 0.0f, -4.0f), Vector3f(0.0f, 0.0f, -1.0f), cam.GetViewProj(), cam.GetNearZ(), 1280.0f, 720.0f, 256.0f);
    EXPECT_FALSE(setup.ready);
    EXPECT_EQ(setup.miss, SsrMissKind::Sky);
}

TEST(Ssr, HitCompare_ViewZNotLength)
{
    const float nearZ       = 0.18f;
    const float viewZ       = 8.0f;
    const float euclidean   = viewZ * std::sqrt(2.0f);
    const float sceneDepth  = nearZ / viewZ;
    const float sceneViewZ  = ssrLinearizeViewZ(sceneDepth, nearZ);
    const float thickness   = 0.2f;
    EXPECT_NEAR(sceneViewZ, viewZ, 1.0e-4f);
    EXPECT_GT(std::fabs(euclidean - viewZ), 1.0f);
    EXPECT_TRUE(ssrHitViewZ(viewZ + 0.05f, sceneViewZ, thickness));
    EXPECT_FALSE(ssrHitViewZ(euclidean, sceneViewZ, thickness));
}

TEST(Ssr, Dda_PerspectiveCorrectViewZ)
{
    const float w0 = 2.0f;
    const float w1 = 10.0f;
    const float t  = 0.5f;
    const float lerpW = w0 + (w1 - w0) * t;
    const float persp = ssrPerspectiveCorrectViewZ(w0, w1, t);
    EXPECT_NEAR(lerpW, 6.0f, 1.0e-5f);
    EXPECT_NEAR(persp, 1.0f / (0.5f * (1.0f / w0) + 0.5f * (1.0f / w1)), 1.0e-5f);
    EXPECT_GT(std::fabs(lerpW - persp), 1.0f);
}

TEST(Ssr, Reprojection_RowVector)
{
    Camera3D cam;
    cam.SetLens(Pi * 0.25f, 16.0f / 9.0f, 0.18f, 2000.0f);
    cam.LookAt(Vector3f(0.0f, 2.0f, -6.0f), Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));
    const Matrix4f prev = cam.GetViewProj();
    cam.SetPosition(Vector3f(0.4f, 2.0f, -6.0f));
    const Matrix4f curr = cam.GetViewProj();

    const Matrix4f got   = ssrReprojectionMatrix(curr, prev);
    const Matrix4f row   = curr.Inverse() * prev;
    const Matrix4f wrong = prev * curr.Inverse();
    bool differs = false;
    for (int i = 0; i < 16; ++i)
    {
        EXPECT_NEAR(got.m_afEntry[i], row.m_afEntry[i], 1.0e-5f);
        if (std::fabs(row.m_afEntry[i] - wrong.m_afEntry[i]) > 1.0e-4f)
            differs = true;
    }
    EXPECT_TRUE(differs);
}
