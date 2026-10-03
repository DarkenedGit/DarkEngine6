#include <gtest/gtest.h>

#include "Render/Profile.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{

ComPtr<ID3D12Device> tryCreateDevice()
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

TEST(Profile, CpuScopeNestsAndNullGpuIsSilent)
{
    {
        Dark::CpuScope outer("ECS Update", Dark::ProfileColor::EcsUpdate);
        Dark::CpuScope inner("Frame", Dark::ProfileColor::Frame);
    }
    Dark::GpuScope silent(nullptr, "Frame", Dark::ProfileColor::Frame);
    Dark::CpuScope empty(nullptr, Dark::ProfileColor::EcsUpdate);
    Dark::profileCpuBegin("ECS Update", Dark::ProfileColor::EcsUpdate);
    Dark::profileCpuEnd();
}

TEST(Profile, GpuScopeRecordsOnCommandList)
{
    ComPtr<ID3D12Device> device = tryCreateDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";

    ComPtr<ID3D12CommandAllocator> alloc;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))));
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd))));

    {
        Dark::GpuScope frame(cmd.Get(), "Frame", Dark::ProfileColor::Frame);
        Dark::GpuScope gbuffer(cmd.Get(), "GBuffer", Dark::ProfileColor::GBuffer);
    }
    EXPECT_TRUE(SUCCEEDED(cmd->Close()));
}
