#include <gtest/gtest.h>

#include "Render/Profile.h"

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

#include <cstdio>
#include <cstring>

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

namespace
{

const Dark::ProfileRange* findRange(const Dark::ProfileSnapshot& snap, const char* name)
{
    for (uint32_t i = 0; i < snap.rangeCount; ++i)
    {
        if (std::strcmp(snap.ranges[i].name, name) == 0)
            return &snap.ranges[i];
    }
    return nullptr;
}

} // namespace

TEST(Profile, FrameRecordsNestedCpuRanges)
{
    Dark::profileFrameBegin();
    {
        const Dark::CpuScope outer("ECS Update", Dark::ProfileColor::EcsUpdate);
        const Dark::CpuScope inner("Frame", Dark::ProfileColor::Frame);
    }
    char cascade[32];
    std::snprintf(cascade, sizeof(cascade), "Shadow Cascade %d", 1);
    Dark::profileCpuBegin(cascade, Dark::ProfileColor::ShadowCascade);
    std::memset(cascade, 0x58, sizeof(cascade));
    Dark::profileCpuEnd();
    Dark::profileFrameEnd(1.0f / 60.0f);

    const Dark::ProfileSnapshot& snap = Dark::profileSnapshot();
    EXPECT_EQ(snap.rangeCount, 3u);
    EXPECT_NEAR(snap.fps, 60.0f, 0.01f);
    EXPECT_GE(snap.frameMs, 16.0f);
    EXPECT_FALSE(snap.gpuValid);
    EXPECT_GE(snap.cpuMs, 0.0f);

    const Dark::ProfileRange* ecs    = findRange(snap, "ECS Update");
    const Dark::ProfileRange* frame  = findRange(snap, "Frame");
    const Dark::ProfileRange* shadow = findRange(snap, "Shadow Cascade 1");
    ASSERT_NE(ecs, nullptr);
    ASSERT_NE(frame, nullptr);
    ASSERT_NE(shadow, nullptr);
    EXPECT_EQ(ecs->depth, 0);
    EXPECT_EQ(frame->depth, 1);
    EXPECT_EQ(shadow->depth, 0);
    EXPECT_GE(ecs->cpuMs, frame->cpuMs);
    EXPECT_LT(frame->gpuMs, 0.0f);
    EXPECT_LT(ecs->gpuMs, 0.0f);
}

TEST(Profile, GpuTimestampsResolveOnTheNextSlotReuse)
{
    ComPtr<ID3D12Device> device = tryCreateDevice();
    if (!device)
        GTEST_SKIP() << "no D3D12 device (WARP or hardware)";

    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))))
        GTEST_SKIP() << "CreateCommandQueue failed";

    struct Shutdown
    {
        ~Shutdown() { Dark::profileTimersShutdown(); }
    };
    if (!Dark::profileTimersInit(device.Get(), queue.Get()))
        GTEST_SKIP() << "timestamp queries unavailable";
    const Shutdown shutdown;

    ComPtr<ID3D12CommandAllocator> alloc;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))));
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd))));

    Dark::profileFrameBegin();
    Dark::profileTimersBeginFrame(0, cmd.Get());
    {
        const Dark::GpuScope frame(cmd.Get(), "Frame", Dark::ProfileColor::Frame);
        const Dark::GpuScope gbuffer(cmd.Get(), "GBuffer", Dark::ProfileColor::GBuffer);
    }
    Dark::profileTimersResolve(cmd.Get());
    ASSERT_TRUE(SUCCEEDED(cmd->Close()));

    ComPtr<ID3D12Fence> fence;
    ASSERT_TRUE(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));

    ID3D12CommandList* lists[] = { cmd.Get() };
    queue->ExecuteCommandLists(1, lists);
    ASSERT_TRUE(SUCCEEDED(queue->Signal(fence.Get(), 1)));
    bool gpuDone = fence->GetCompletedValue() >= 1;
    if (!gpuDone)
    {
        const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        ASSERT_NE(event, nullptr);
        EXPECT_TRUE(SUCCEEDED(fence->SetEventOnCompletion(1, event)));
        gpuDone = WaitForSingleObject(event, 5000) == WAIT_OBJECT_0;
        CloseHandle(event);
    }

    Dark::profileFrameEnd(0.016f);
    if (gpuDone)
        Dark::profileTimersBeginFrame(0, nullptr);
    ASSERT_TRUE(gpuDone);

    const Dark::ProfileSnapshot& snap = Dark::profileSnapshot();
    EXPECT_TRUE(snap.gpuValid);
    const Dark::ProfileRange* frame   = findRange(snap, "Frame");
    const Dark::ProfileRange* gbuffer = findRange(snap, "GBuffer");
    ASSERT_NE(frame, nullptr);
    ASSERT_NE(gbuffer, nullptr);
    EXPECT_EQ(frame->depth, 0);
    EXPECT_EQ(gbuffer->depth, 1);
    EXPECT_GE(frame->gpuMs, 0.0f);
    EXPECT_GE(gbuffer->gpuMs, 0.0f);
    EXPECT_GE(frame->gpuMs + 0.01f, gbuffer->gpuMs);
}
