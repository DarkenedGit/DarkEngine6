#include <gtest/gtest.h>

#include "Render/TerrainErosionPipeline.h"
#include "Render/Texture2D.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

using Dark::TerrainErosionPipeline;
using Dark::Texture2D;
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

TEST(TerrainErosion, Create_NoDevice_False)
{
    TerrainErosionPipeline pipe;
    EXPECT_FALSE(pipe.create(nullptr));
    EXPECT_FALSE(pipe.isValid());
    EXPECT_EQ(pipe.commandList(), nullptr);
    EXPECT_EQ(pipe.psoThermal(), nullptr);
    EXPECT_EQ(pipe.psoPipe(), nullptr);
}

TEST(TerrainErosion, CreateUavR32Float_NoDevice_False)
{
    Texture2D tex;
    EXPECT_FALSE(tex.createUavR32Float(nullptr, 8, 8));
    EXPECT_FALSE(tex.valid());
    EXPECT_EQ(tex.cpuHandleUav().ptr, 0u);
    EXPECT_EQ(tex.resource(), nullptr);
}

TEST(TerrainErosion, CreateUavR32Float_ZeroSize_False)
{
    Texture2D tex;
    EXPECT_FALSE(tex.createUavR32Float(nullptr, 0, 8));
    EXPECT_FALSE(tex.createUavR32Float(nullptr, 8, 0));
    EXPECT_FALSE(tex.valid());
}

TEST(TerrainErosion, Create_WithDevice)
{
    ComPtr<ID3D12Device> device = TryCreateDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";

    TerrainErosionPipeline pipe;
    ASSERT_TRUE(pipe.create(device.Get()));
    EXPECT_TRUE(pipe.isValid());
    EXPECT_NE(pipe.psoThermal(), nullptr);
    EXPECT_NE(pipe.psoPipe(), nullptr);
    EXPECT_NE(pipe.rootSignature(), nullptr);
    EXPECT_NE(pipe.heap(), nullptr);
    EXPECT_NE(pipe.cpuHandle(0).ptr, 0u);
    EXPECT_NE(pipe.gpuHandle(1).ptr, 0u);

    ASSERT_TRUE(pipe.resetCommands(device.Get()));
    EXPECT_NE(pipe.commandList(), nullptr);
    EXPECT_NE(pipe.allocator(), nullptr);
}

TEST(TerrainErosion, ResetCommands_NoDevice_False)
{
    TerrainErosionPipeline pipe;
    EXPECT_FALSE(pipe.resetCommands(nullptr));
    EXPECT_EQ(pipe.commandList(), nullptr);
}

TEST(TerrainErosion, CreateUavR32Float_WithDevice)
{
    ComPtr<ID3D12Device> device = TryCreateDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";

    Texture2D tex;
    EXPECT_FALSE(tex.createUavR32Float(device.Get(), 0, 16));
    EXPECT_FALSE(tex.createUavR32Float(device.Get(), 16, 0));
    EXPECT_FALSE(tex.valid());

    ASSERT_TRUE(tex.createUavR32Float(device.Get(), 16, 32));
    EXPECT_TRUE(tex.valid());
    EXPECT_EQ(tex.width(), 16u);
    EXPECT_EQ(tex.height(), 32u);
    ASSERT_NE(tex.resource(), nullptr);
    EXPECT_NE(tex.cpuHandle().ptr, 0u);
    EXPECT_NE(tex.cpuHandleUav().ptr, 0u);
    EXPECT_NE(tex.cpuHandle().ptr, tex.cpuHandleUav().ptr);

    const D3D12_RESOURCE_DESC desc = tex.resource()->GetDesc();
    EXPECT_EQ(desc.Format, DXGI_FORMAT_R32_FLOAT);
    EXPECT_EQ(desc.Width, 16u);
    EXPECT_EQ(desc.Height, 32u);
    EXPECT_NE(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 0u);
    EXPECT_EQ(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 0u);
}

TEST(TerrainErosion, CreateUavRgba32Float_WithDevice)
{
    ComPtr<ID3D12Device> device = TryCreateDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";

    Texture2D tex;
    ASSERT_TRUE(tex.createUavRgba32Float(device.Get(), 8, 8));
    const D3D12_RESOURCE_DESC desc = tex.resource()->GetDesc();
    EXPECT_EQ(desc.Format, DXGI_FORMAT_R32G32B32A32_FLOAT);
    EXPECT_NE(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 0u);
}

TEST(TerrainErosion, Dispatch_NoCmd_False)
{
    TerrainErosionPipeline    pipe;
    Dark::ErosionGpuConstants constants{};
    constants.width  = 8;
    constants.height = 8;
    Texture2D a;
    Texture2D b;
    EXPECT_FALSE(pipe.dispatchThermal(nullptr, nullptr, a, b, constants, 0));
    EXPECT_FALSE(pipe.dispatchPipe(nullptr, nullptr, a, b, a, b, constants, 0));
}
