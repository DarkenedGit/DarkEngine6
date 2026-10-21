#include <gtest/gtest.h>

#include "Render/EchoPipeline.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{

ComPtr<ID3D12Device> createTestDevice()
{
    ComPtr<ID3D12Device> device;
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

TEST(EchoPipeline, CreatesSnapshotAndCompositePsos)
{
    ComPtr<ID3D12Device> device = createTestDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";

    Dark::EchoPipeline echo;
    ASSERT_TRUE(echo.create(device.Get()));
    EXPECT_TRUE(echo.isValid());
    EXPECT_FALSE(echo.hasSnapshot());
}

TEST(EchoPipeline, BuiltinChirpCrossesFortyMetresBeforeTheNextPulse)
{
    EXPECT_FLOAT_EQ(Dark::EchoPipeline::kPeriodSeconds, 1.25f);
    EXPECT_FLOAT_EQ(Dark::EchoPipeline::kSpeedMetersPerSec, 50.0f);
    EXPECT_FLOAT_EQ(Dark::EchoPipeline::kRangeMeters, 40.0f);
    EXPECT_FLOAT_EQ(Dark::EchoPipeline::kEdgeMeters, 8.0f);
    EXPECT_LT(Dark::EchoPipeline::kExpandSeconds, Dark::EchoPipeline::kPeriodSeconds);
    EXPECT_FLOAT_EQ(Dark::EchoPipeline::kRangeMeters / Dark::EchoPipeline::kSpeedMetersPerSec, Dark::EchoPipeline::kExpandSeconds);
    EXPECT_EQ(Dark::EchoPipeline::kSnapshotDivisor, 8u);
}
