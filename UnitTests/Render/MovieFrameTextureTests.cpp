#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <gtest/gtest.h>

#include "Render/MovieFrameTexture.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{

    ComPtr<ID3D12Device> createTestDevice()
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

    HRESULT createBgra(ID3D12Device* device, D3D12_RESOURCE_FLAGS flags, ComPtr<ID3D12Resource>& out)
    {
        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width            = 64;
        texDesc.Height           = 64;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels        = 1;
        texDesc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        texDesc.SampleDesc       = { 1, 0 };
        texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags            = flags;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        return device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&out));
    }

} // namespace

TEST(MovieFrameTexture, SoftwareFramesDoNotRequestUav)
{
    EXPECT_EQ(Dark::movieFrameResourceFlags(false), D3D12_RESOURCE_FLAG_NONE);
    EXPECT_NE(Dark::movieFrameResourceFlags(true) & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 0u);
    EXPECT_FALSE(Dark::movieBgraTypedUavStoreSupported(nullptr));
}

TEST(MovieFrameTexture, UavFlagMatchesFormatSupport)
{
    ComPtr<ID3D12Device> device = createTestDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";

    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
    support.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    ASSERT_TRUE(SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))));
    const bool typed = (support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) != 0;
    const bool store = (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0;
    EXPECT_EQ(Dark::movieBgraTypedUavStoreSupported(device.Get()), typed && store);

    ComPtr<ID3D12Resource> software;
    ASSERT_TRUE(SUCCEEDED(createBgra(device.Get(), Dark::movieFrameResourceFlags(false), software)));
    EXPECT_EQ(software->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 0u);

    ComPtr<ID3D12Resource> hardware;
    const HRESULT          hw = createBgra(device.Get(), Dark::movieFrameResourceFlags(true), hardware);
    if (Dark::movieBgraTypedUavStoreSupported(device.Get()))
    {
        ASSERT_TRUE(SUCCEEDED(hw));
        EXPECT_NE(hardware->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 0u);
    }
    else
        EXPECT_TRUE(FAILED(hw));
}
