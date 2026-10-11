#include <gtest/gtest.h>

#include "Math/AABox3f.h"
#include "Math/Matrix4f.h"
#include "Math/Sphere3f.h"
#include "Render/Camera3D.h"
#include "Render/FoliagePipeline.h"
#include "Render/ShaderCompile.h"

#include <d3d11shader.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

using namespace Dark;
using namespace Dark::Math;
using namespace Dark::Terrain;

namespace
{

Frustum3f cameraFrustum(const Vector3f& target)
{
    Camera3D camera;
    camera.SetLens(1.0f, 1.0f, 0.1f, 40.0f);
    camera.LookAt(Vector3f(0.0f, 0.0f, 0.0f), target, Vector3f(0.0f, 1.0f, 0.0f));
    return Frustum3f(camera.GetCullViewProj());
}

void copyPlanes(const Frustum3f& frustum, float planes[6][4])
{
    for (int plane = 0; plane < 6; ++plane)
    {
        const Plane4f& p = frustum.GetPlane(plane);
        planes[plane][0] = p.x;
        planes[plane][1] = p.y;
        planes[plane][2] = p.z;
        planes[plane][3] = p.w;
    }
}

ComPtr<ID3D12Device> tryWarpDevice()
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
    return {};
}

bool cbufferBytes(ID3DBlob* blob, const char* name, UINT& outBytes)
{
    ComPtr<ID3D11ShaderReflection> reflect;
    if (!blob || FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&reflect))))
        return false;
    D3D11_SHADER_DESC desc{};
    if (FAILED(reflect->GetDesc(&desc)))
        return false;
    for (UINT i = 0; i < desc.ConstantBuffers; ++i)
    {
        ID3D11ShaderReflectionConstantBuffer* cb = reflect->GetConstantBufferByIndex(i);
        D3D11_SHADER_BUFFER_DESC              cbDesc{};
        if (!cb || FAILED(cb->GetDesc(&cbDesc)) || !cbDesc.Name)
            return false;
        if (std::strcmp(cbDesc.Name, name) == 0)
        {
            outBytes = cbDesc.Size;
            return true;
        }
    }
    return false;
}

ComPtr<ID3D12Resource> uploadBuffer(ID3D12Device* device, const void* bytes, UINT64 size)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width            = size;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.SampleDesc       = { 1, 0 };
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> res;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res))))
        return nullptr;
    void* mapped = nullptr;
    if (FAILED(res->Map(0, nullptr, &mapped)))
        return nullptr;
    std::memcpy(mapped, bytes, static_cast<size_t>(size));
    res->Unmap(0, nullptr);
    return res;
}

ComPtr<ID3D12Resource> readbackBuffer(ID3D12Device* device, UINT64 size)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width            = size;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.SampleDesc       = { 1, 0 };
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> res;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&res))))
        return nullptr;
    return res;
}

struct IndexedArgs
{
    uint32_t indexCountPerInstance;
    uint32_t instanceCount;
    uint32_t startIndexLocation;
    int32_t  baseVertexLocation;
    uint32_t startInstanceLocation;
};

static_assert(sizeof(IndexedArgs) == sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), "foliage arg size");
static_assert(sizeof(Matrix4f) == 64u, "foliage world rows");

const IndexedArgs* argAt(const uint8_t* bytes, uint32_t view, uint32_t kind, uint32_t part)
{
    return reinterpret_cast<const IndexedArgs*>(bytes + FoliagePipeline::cullArgumentOffset(view, kind, part));
}

void expectArg(const IndexedArgs& arg, uint32_t indexCount, uint32_t instanceCount)
{
    EXPECT_EQ(arg.indexCountPerInstance, indexCount);
    EXPECT_EQ(arg.instanceCount, instanceCount);
    EXPECT_EQ(arg.startIndexLocation, 0u);
    EXPECT_EQ(arg.baseVertexLocation, 0);
    EXPECT_EQ(arg.startInstanceLocation, 0u);
    EXPECT_NE(arg.startInstanceLocation, kMaxFoliageDraw);
}

bool boxVisible(const Matrix4f& world, const AABox3f& local, const Frustum3f& frustum, const Sphere3f* sphere)
{
    const AABox3f box = local.Transformed(world);
    if (sphere)
        return box.Intersects(*sphere);
    return frustum.Intersects(box);
}

} // namespace

TEST(FoliageCull, ShadersCompile)
{
    ComPtr<ID3DBlob> compact;
    ComPtr<ID3DBlob> args;
    ASSERT_TRUE(compileShaderFromContent("shaders/FoliageCull.hlsl", "CSCompact", "cs_5_0", compact));
    ASSERT_TRUE(compileShaderFromContent("shaders/FoliageCull.hlsl", "CSArgs", "cs_5_0", args));
    UINT compactBytes = 0;
    UINT argBytes     = 0;
    ASSERT_TRUE(cbufferBytes(compact.Get(), "FoliageCullConstants", compactBytes));
    ASSERT_TRUE(cbufferBytes(args.Get(), "FoliageCullConstants", argBytes));
    EXPECT_EQ(compactBytes, FoliagePipeline::kCullConstantBytes);
    EXPECT_EQ(argBytes, FoliagePipeline::kCullConstantBytes);
}

TEST(FoliageCull, GrassEmitsNoShadowIndices)
{
    ComPtr<ID3D12Device> device = tryWarpDevice();
    if (!device)
        GTEST_SKIP() << "no WARP device at feature level 11_0";

    const Frustum3f ahead = cameraFrustum(Vector3f(0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(ahead.Contains(Vector3f(0.0f, 0.0f, 8.0f)));
    EXPECT_FALSE(ahead.Contains(Vector3f(0.0f, 0.0f, 80.0f)));

    const AABox3f local(Vector3f(-0.5f, -0.5f, -0.5f), Vector3f(0.5f, 0.5f, 0.5f));
    Matrix4f worlds[3]{};
    worlds[0] = Matrix4f::TranslationMatrix(0.0f, 0.0f, 8.0f);
    worlds[1] = Matrix4f::TranslationMatrix(0.0f, 0.0f, 80.0f);
    worlds[2] = Matrix4f::TranslationMatrix(0.0f, 0.0f, 8.0f);
    const uint32_t kinds[3] = {
        static_cast<uint32_t>(FoliageKind::Tree),
        static_cast<uint32_t>(FoliageKind::Tree),
        static_cast<uint32_t>(FoliageKind::Grass),
    };

    ComPtr<ID3D12Resource> worldUpload = uploadBuffer(device.Get(), worlds, sizeof(worlds));
    ComPtr<ID3D12Resource> kindUpload  = uploadBuffer(device.Get(), kinds, sizeof(kinds));
    ASSERT_TRUE(worldUpload);
    ASSERT_TRUE(kindUpload);

    FoliagePipeline pipeline;
    ASSERT_TRUE(pipeline.createCull(device.Get()));
    ASSERT_TRUE(pipeline.cullReady());
    EXPECT_EQ(pipeline.cullArgumentResource(2), nullptr);
    EXPECT_EQ(pipeline.cullIndexResource(2), nullptr);

    FoliageCullDesc desc{};
    desc.worlds            = worldUpload->GetGPUVirtualAddress();
    desc.kinds             = kindUpload->GetGPUVirtualAddress();
    desc.instanceCount     = 3;
    desc.kindResident[0]   = 2;
    desc.kindResident[3]   = 1;
    desc.kindValid[0]      = 1;
    desc.kindValid[3]      = 1;
    desc.boundsMin[0][0]   = local.Min.x;
    desc.boundsMin[0][1]   = local.Min.y;
    desc.boundsMin[0][2]   = local.Min.z;
    desc.boundsMax[0][0]   = local.Max.x;
    desc.boundsMax[0][1]   = local.Max.y;
    desc.boundsMax[0][2]   = local.Max.z;
    desc.boundsMin[3][0]   = local.Min.x;
    desc.boundsMin[3][1]   = local.Min.y;
    desc.boundsMin[3][2]   = local.Min.z;
    desc.boundsMax[3][0]   = local.Max.x;
    desc.boundsMax[3][1]   = local.Max.y;
    desc.boundsMax[3][2]   = local.Max.z;
    desc.partCount[0]      = 2;
    desc.indexCount[0][0]  = 24;
    desc.indexCount[0][1]  = 12;
    desc.partCount[3]      = 1;
    desc.indexCount[3][0]  = 6;
    copyPlanes(ahead, desc.planes);

    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC   queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))));
    ComPtr<ID3D12CommandAllocator>    allocator;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmd))));

    FoliageCullDesc empty = desc;
    empty.instanceCount   = 0;
    EXPECT_FALSE(pipeline.recordCull(cmd.Get(), 0, empty));
    EXPECT_FALSE(pipeline.recordCull(cmd.Get(), 2, desc));
    desc.viewIndex = FoliagePipeline::kCullViewsPerFrame;
    EXPECT_FALSE(pipeline.recordCull(cmd.Get(), 0, desc));

    desc.viewIndex  = 0;
    desc.shadowView = 1;
    ASSERT_TRUE(pipeline.recordCull(cmd.Get(), 0, desc));
    desc.viewIndex  = 1;
    desc.shadowView = 0;
    ASSERT_TRUE(pipeline.recordCull(cmd.Get(), 0, desc));

    ID3D12Resource* args    = pipeline.cullArgumentResource(0);
    ID3D12Resource* indices = pipeline.cullIndexResource(0);
    ASSERT_NE(args, nullptr);
    ASSERT_NE(indices, nullptr);

    ComPtr<ID3D12Resource> argRead   = readbackBuffer(device.Get(), args->GetDesc().Width);
    ComPtr<ID3D12Resource> shadowRead = readbackBuffer(device.Get(), 8u * sizeof(uint32_t));
    ComPtr<ID3D12Resource> cameraTree = readbackBuffer(device.Get(), 8u * sizeof(uint32_t));
    ComPtr<ID3D12Resource> cameraGrass = readbackBuffer(device.Get(), sizeof(uint32_t));
    ASSERT_TRUE(argRead);
    ASSERT_TRUE(shadowRead);
    ASSERT_TRUE(cameraTree);
    ASSERT_TRUE(cameraGrass);

    auto transition = [](ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter  = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barrier);
    };
    transition(cmd.Get(), args, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(cmd.Get(), indices, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd->CopyBufferRegion(argRead.Get(), 0, args, 0, args->GetDesc().Width);
    cmd->CopyBufferRegion(shadowRead.Get(), 0, indices, FoliagePipeline::cullIndexByteOffset(0, 0), 8u * sizeof(uint32_t));
    cmd->CopyBufferRegion(cameraTree.Get(), 0, indices, FoliagePipeline::cullIndexByteOffset(1, 0), 8u * sizeof(uint32_t));
    cmd->CopyBufferRegion(cameraGrass.Get(), 0, indices, FoliagePipeline::cullIndexByteOffset(1, FoliagePipeline::kGrassKindIndex), sizeof(uint32_t));
    ASSERT_TRUE(SUCCEEDED(cmd->Close()));

    ComPtr<ID3D12Fence> fence;
    ASSERT_TRUE(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
    ID3D12CommandList* lists[] = { cmd.Get() };
    queue->ExecuteCommandLists(1, lists);
    ASSERT_TRUE(SUCCEEDED(queue->Signal(fence.Get(), 1)));
    if (fence->GetCompletedValue() < 1)
    {
        const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        ASSERT_NE(event, nullptr);
        ASSERT_TRUE(SUCCEEDED(fence->SetEventOnCompletion(1, event)));
        ASSERT_EQ(WaitForSingleObject(event, 10000), WAIT_OBJECT_0);
        CloseHandle(event);
    }

    std::vector<uint8_t> argBytes(static_cast<size_t>(args->GetDesc().Width));
    uint32_t shadowTree[8]{};
    uint32_t cameraTrees[8]{};
    uint32_t cameraGrassIndex = 0;
    void* argMap = nullptr;
    void* shadowMap = nullptr;
    void* treeMap = nullptr;
    void* grassMap = nullptr;
    ASSERT_TRUE(SUCCEEDED(argRead->Map(0, nullptr, &argMap)));
    ASSERT_TRUE(SUCCEEDED(shadowRead->Map(0, nullptr, &shadowMap)));
    ASSERT_TRUE(SUCCEEDED(cameraTree->Map(0, nullptr, &treeMap)));
    ASSERT_TRUE(SUCCEEDED(cameraGrass->Map(0, nullptr, &grassMap)));
    std::memcpy(argBytes.data(), argMap, argBytes.size());
    std::memcpy(shadowTree, shadowMap, sizeof(shadowTree));
    std::memcpy(cameraTrees, treeMap, sizeof(cameraTrees));
    std::memcpy(&cameraGrassIndex, grassMap, sizeof(cameraGrassIndex));
    argRead->Unmap(0, nullptr);
    shadowRead->Unmap(0, nullptr);
    cameraTree->Unmap(0, nullptr);
    cameraGrass->Unmap(0, nullptr);
    pipeline.destroy();

    const IndexedArgs& shadowTreeArg = *argAt(argBytes.data(), 0, 0, 0);
    const IndexedArgs& shadowLeafArg = *argAt(argBytes.data(), 0, 0, 1);
    const IndexedArgs& shadowGrassArg = *argAt(argBytes.data(), 0, FoliagePipeline::kGrassKindIndex, 0);
    expectArg(shadowTreeArg, 24u, 1u);
    expectArg(shadowLeafArg, 12u, 1u);
    expectArg(shadowGrassArg, 6u, 0u);
    EXPECT_EQ(shadowTree[0], 0u);

    const IndexedArgs& cameraTreeArg = *argAt(argBytes.data(), 1, 0, 0);
    const IndexedArgs& cameraGrassArg = *argAt(argBytes.data(), 1, FoliagePipeline::kGrassKindIndex, 0);
    expectArg(cameraTreeArg, 24u, 1u);
    expectArg(cameraGrassArg, 6u, 1u);
    EXPECT_EQ(cameraTrees[0], 0u);
    EXPECT_EQ(cameraGrassIndex, 2u);
}

TEST(FoliageCull, PointLightSphereSharedAcrossFaces)
{
    ComPtr<ID3D12Device> device = tryWarpDevice();
    if (!device)
        GTEST_SKIP() << "no WARP device at feature level 11_0";

    const Frustum3f ahead = cameraFrustum(Vector3f(0.0f, 0.0f, 1.0f));
    const Frustum3f back  = cameraFrustum(Vector3f(0.0f, 0.0f, -1.0f));
    const AABox3f local(Vector3f(-0.5f, -0.5f, -0.5f), Vector3f(0.5f, 0.5f, 0.5f));
    const Sphere3f pointSphere(Vector3f(0.0f, 0.0f, -5.0f), 2.0f);

    Matrix4f worlds[5]{};
    worlds[0] = Matrix4f::TranslationMatrix(0.0f, 0.0f, -5.0f);
    worlds[1] = Matrix4f::TranslationMatrix(0.0f, 0.0f, 8.0f);
    worlds[2] = Matrix4f::TranslationMatrix(0.0f, 30.0f, -5.0f);
    worlds[3] = Matrix4f::TranslationMatrix(0.0f, 0.0f, -7.2f);
    worlds[4] = Matrix4f::TranslationMatrix(0.0f, 0.0f, -5.0f);
    const uint32_t kinds[5] = {
        static_cast<uint32_t>(FoliageKind::Tree),
        static_cast<uint32_t>(FoliageKind::Tree),
        static_cast<uint32_t>(FoliageKind::Tree),
        static_cast<uint32_t>(FoliageKind::Tree),
        static_cast<uint32_t>(FoliageKind::Grass),
    };

    EXPECT_TRUE(pointSphere.Center.y == 0.0f);
    EXPECT_TRUE(local.Transformed(worlds[0]).Intersects(pointSphere));
    EXPECT_FALSE(local.Transformed(worlds[1]).Intersects(pointSphere));
    EXPECT_FALSE(local.Transformed(worlds[2]).Intersects(pointSphere));
    EXPECT_TRUE(local.Transformed(worlds[3]).Intersects(pointSphere));
    EXPECT_TRUE(ahead.Intersects(local.Transformed(worlds[1])));
    EXPECT_FALSE(ahead.Intersects(local.Transformed(worlds[0])));
    EXPECT_TRUE(back.Intersects(local.Transformed(worlds[0])));
    EXPECT_FALSE(back.Intersects(local.Transformed(worlds[1])));

    std::vector<uint32_t> expected;
    for (uint32_t i = 0; i < 5; ++i)
    {
        if (kinds[i] == static_cast<uint32_t>(FoliageKind::Grass))
            continue;
        if (boxVisible(worlds[i], local, ahead, &pointSphere))
            expected.push_back(i);
    }
    std::sort(expected.begin(), expected.end());
    ASSERT_EQ(expected.size(), 2u);
    EXPECT_EQ(expected[0], 0u);
    EXPECT_EQ(expected[1], 3u);

    ComPtr<ID3D12Resource> worldUpload = uploadBuffer(device.Get(), worlds, sizeof(worlds));
    ComPtr<ID3D12Resource> kindUpload  = uploadBuffer(device.Get(), kinds, sizeof(kinds));
    ASSERT_TRUE(worldUpload);
    ASSERT_TRUE(kindUpload);

    FoliagePipeline pipeline;
    ASSERT_TRUE(pipeline.createCull(device.Get()));

    FoliageCullDesc desc{};
    desc.worlds           = worldUpload->GetGPUVirtualAddress();
    desc.kinds            = kindUpload->GetGPUVirtualAddress();
    desc.instanceCount    = 5;
    desc.kindResident[0]  = 4;
    desc.kindResident[3]  = 1;
    desc.kindValid[0]     = 1;
    desc.kindValid[3]     = 1;
    desc.useSphere        = 1;
    desc.shadowView       = 1;
    desc.sphere[0]        = pointSphere.Center.x;
    desc.sphere[1]        = pointSphere.Center.y;
    desc.sphere[2]        = pointSphere.Center.z;
    desc.sphere[3]        = pointSphere.Radius;
    desc.partCount[0]     = 1;
    desc.indexCount[0][0] = 36;
    desc.partCount[3]     = 1;
    desc.indexCount[3][0] = 6;
    for (int kind = 0; kind < 4; ++kind)
    {
        if (desc.kindValid[kind] == 0)
            continue;
        desc.boundsMin[kind][0] = local.Min.x;
        desc.boundsMin[kind][1] = local.Min.y;
        desc.boundsMin[kind][2] = local.Min.z;
        desc.boundsMax[kind][0] = local.Max.x;
        desc.boundsMax[kind][1] = local.Max.y;
        desc.boundsMax[kind][2] = local.Max.z;
    }

    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC   queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))));
    ComPtr<ID3D12CommandAllocator>    allocator;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmd))));

    desc.viewIndex = 0;
    copyPlanes(ahead, desc.planes);
    ASSERT_TRUE(pipeline.recordCull(cmd.Get(), 0, desc));
    desc.viewIndex = 1;
    copyPlanes(back, desc.planes);
    ASSERT_TRUE(pipeline.recordCull(cmd.Get(), 0, desc));

    ID3D12Resource* args    = pipeline.cullArgumentResource(0);
    ID3D12Resource* indices = pipeline.cullIndexResource(0);
    ASSERT_NE(args, nullptr);
    ASSERT_NE(indices, nullptr);

    ComPtr<ID3D12Resource> argRead  = readbackBuffer(device.Get(), args->GetDesc().Width);
    ComPtr<ID3D12Resource> faceRead = readbackBuffer(device.Get(), 8u * sizeof(uint32_t));
    ComPtr<ID3D12Resource> backRead = readbackBuffer(device.Get(), 8u * sizeof(uint32_t));
    ASSERT_TRUE(argRead);
    ASSERT_TRUE(faceRead);
    ASSERT_TRUE(backRead);

    auto transition = [](ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter  = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barrier);
    };
    transition(cmd.Get(), args, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(cmd.Get(), indices, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd->CopyBufferRegion(argRead.Get(), 0, args, 0, args->GetDesc().Width);
    cmd->CopyBufferRegion(faceRead.Get(), 0, indices, FoliagePipeline::cullIndexByteOffset(0, 0), 8u * sizeof(uint32_t));
    cmd->CopyBufferRegion(backRead.Get(), 0, indices, FoliagePipeline::cullIndexByteOffset(1, 0), 8u * sizeof(uint32_t));
    ASSERT_TRUE(SUCCEEDED(cmd->Close()));

    ComPtr<ID3D12Fence> fence;
    ASSERT_TRUE(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
    ID3D12CommandList* lists[] = { cmd.Get() };
    queue->ExecuteCommandLists(1, lists);
    ASSERT_TRUE(SUCCEEDED(queue->Signal(fence.Get(), 1)));
    if (fence->GetCompletedValue() < 1)
    {
        const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        ASSERT_NE(event, nullptr);
        ASSERT_TRUE(SUCCEEDED(fence->SetEventOnCompletion(1, event)));
        ASSERT_EQ(WaitForSingleObject(event, 10000), WAIT_OBJECT_0);
        CloseHandle(event);
    }

    std::vector<uint8_t> argBytes(static_cast<size_t>(args->GetDesc().Width));
    uint32_t faceIndex[8]{};
    uint32_t backIndex[8]{};
    void* argMap = nullptr;
    void* faceMap = nullptr;
    void* backMap = nullptr;
    ASSERT_TRUE(SUCCEEDED(argRead->Map(0, nullptr, &argMap)));
    ASSERT_TRUE(SUCCEEDED(faceRead->Map(0, nullptr, &faceMap)));
    ASSERT_TRUE(SUCCEEDED(backRead->Map(0, nullptr, &backMap)));
    std::memcpy(argBytes.data(), argMap, argBytes.size());
    std::memcpy(faceIndex, faceMap, sizeof(faceIndex));
    std::memcpy(backIndex, backMap, sizeof(backIndex));
    argRead->Unmap(0, nullptr);
    faceRead->Unmap(0, nullptr);
    backRead->Unmap(0, nullptr);
    pipeline.destroy();

    const IndexedArgs& faceTree = *argAt(argBytes.data(), 0, 0, 0);
    const IndexedArgs& backTree = *argAt(argBytes.data(), 1, 0, 0);
    const IndexedArgs& faceGrass = *argAt(argBytes.data(), 0, FoliagePipeline::kGrassKindIndex, 0);
    const IndexedArgs& backGrass = *argAt(argBytes.data(), 1, FoliagePipeline::kGrassKindIndex, 0);
    expectArg(faceTree, 36u, static_cast<uint32_t>(expected.size()));
    expectArg(backTree, 36u, static_cast<uint32_t>(expected.size()));
    expectArg(faceGrass, 6u, 0u);
    expectArg(backGrass, 6u, 0u);

    std::vector<uint32_t> faceGot(faceIndex, faceIndex + expected.size());
    std::vector<uint32_t> backGot(backIndex, backIndex + expected.size());
    std::sort(faceGot.begin(), faceGot.end());
    std::sort(backGot.begin(), backGot.end());
    EXPECT_EQ(faceGot, expected);
    EXPECT_EQ(backGot, expected);
}
