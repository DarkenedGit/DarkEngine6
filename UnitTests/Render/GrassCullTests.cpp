#include <gtest/gtest.h>

#include "Render/Camera3D.h"
#include "Render/DepthState.h"
#include "Render/GrassPipeline.h"
#include "Render/ShaderCompile.h"

#include <d3d11shader.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

using namespace Dark;
using namespace Dark::Math;
using namespace Dark::Terrain;

namespace
{

Frustum3f cameraFrustum()
{
    Camera3D camera;
    camera.SetLens(1.0f, 1.0f, 0.1f, 40.0f);
    camera.LookAt(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));
    return Frustum3f(camera.GetCullViewProj());
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

static_assert(sizeof(IndexedArgs) == sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), "grass arg size");

} // namespace

TEST(GrassCull, ShadersCompile)
{
    ComPtr<ID3DBlob> ps;
    ASSERT_TRUE(compileShaderFromContent("shaders/GrassGBuffer.hlsl", "PSMain", "ps_5_0", ps));

    const char* lods[] = { "0", "1", "2", "3" };
    for (const char* lod : lods)
    {
        D3D_SHADER_MACRO macros[2]{};
        macros[0].Name       = "GRASS_LOD";
        macros[0].Definition = lod;
        ComPtr<ID3DBlob> vs;
        ASSERT_TRUE(compileShaderFromContent("shaders/GrassGBuffer.hlsl", "VSMain", "vs_5_0", vs, macros));

        UINT drawBytes = 0;
        ASSERT_TRUE(cbufferBytes(vs.Get(), "GrassDrawConstants", drawBytes));
        EXPECT_EQ(drawBytes, GrassPipeline::kRootBaseDwords * static_cast<UINT>(sizeof(uint32_t)));

        ComPtr<ID3D11ShaderReflection> reflect;
        ASSERT_TRUE(SUCCEEDED(D3DReflect(vs->GetBufferPointer(), vs->GetBufferSize(), IID_PPV_ARGS(&reflect))));
        D3D11_SHADER_INPUT_BIND_DESC bind{};
        EXPECT_TRUE(SUCCEEDED(reflect->GetResourceBindingDescByName("gIndices", &bind)));
        EXPECT_EQ(bind.BindPoint, 2u);
    }

    ComPtr<ID3DBlob> compact;
    ComPtr<ID3DBlob> args;
    ASSERT_TRUE(compileShaderFromContent("shaders/GrassCull.hlsl", "CSCompact", "cs_5_0", compact));
    ASSERT_TRUE(compileShaderFromContent("shaders/GrassCull.hlsl", "CSArgs", "cs_5_0", args));
    UINT compactBytes = 0;
    UINT argBytes     = 0;
    ASSERT_TRUE(cbufferBytes(compact.Get(), "GrassCullConstants", compactBytes));
    ASSERT_TRUE(cbufferBytes(args.Get(), "GrassCullConstants", argBytes));
    EXPECT_EQ(compactBytes, 160u);
    EXPECT_EQ(argBytes, 160u);
}

TEST(GrassCull, GraphicsPsoAcceptsBaseConstant)
{
    ComPtr<ID3D12Device> device = tryWarpDevice();
    if (!device)
        GTEST_SKIP() << "no WARP device at feature level 11_0";

    D3D12_ROOT_PARAMETER params[5]{};
    params[GrassPipeline::kRootCb].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[GrassPipeline::kRootCb].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    params[GrassPipeline::kRootCb].Descriptor.ShaderRegister = 0;

    params[GrassPipeline::kRootBlades].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[GrassPipeline::kRootBlades].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    params[GrassPipeline::kRootBlades].Descriptor.ShaderRegister = 0;

    params[GrassPipeline::kRootWind].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[GrassPipeline::kRootWind].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    params[GrassPipeline::kRootWind].Descriptor.ShaderRegister = 1;

    params[GrassPipeline::kRootBase].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[GrassPipeline::kRootBase].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;
    params[GrassPipeline::kRootBase].Constants.ShaderRegister = 1;
    params[GrassPipeline::kRootBase].Constants.Num32BitValues = GrassPipeline::kRootBaseDwords;

    params[GrassPipeline::kRootIndices].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[GrassPipeline::kRootIndices].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    params[GrassPipeline::kRootIndices].Descriptor.ShaderRegister = 2;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 5;
    rootDesc.pParameters   = params;
    rootDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob;
    ComPtr<ID3DBlob> rsErr;
    ASSERT_TRUE(SUCCEEDED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr)));
    ComPtr<ID3D12RootSignature> root;
    ASSERT_TRUE(SUCCEEDED(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&root))));

    D3D_SHADER_MACRO macros[2]{};
    macros[0].Name       = "GRASS_LOD";
    macros[0].Definition = "0";
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    ASSERT_TRUE(compileShaderFromContent("shaders/GrassGBuffer.hlsl", "VSMain", "vs_5_0", vs, macros));
    ASSERT_TRUE(compileShaderFromContent("shaders/GrassGBuffer.hlsl", "PSMain", "ps_5_0", ps));

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature                                   = root.Get();
    psoDesc.VS                                               = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS                                               = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[2].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[3].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask                                       = UINT_MAX;
    psoDesc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode                         = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise            = TRUE;
    psoDesc.RasterizerState.DepthClipEnable                  = TRUE;
    psoDesc.DepthStencilState.DepthEnable                    = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc                      = sceneDepthFunc();
    psoDesc.InputLayout                                      = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets                                 = 4;
    psoDesc.RTVFormats[0]                                    = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.RTVFormats[1]                                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.RTVFormats[2]                                    = DXGI_FORMAT_R16G16_FLOAT;
    psoDesc.RTVFormats[3]                                    = DXGI_FORMAT_R8_UNORM;
    psoDesc.DSVFormat                                        = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc                                       = { 1, 0 };

    ComPtr<ID3D12PipelineState> pso;
    const HRESULT               hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    EXPECT_EQ(hr, S_OK);
}

TEST(GrassCull, ComputeCullMatchesVisibleBlades)
{
    ComPtr<ID3D12Device> device = tryWarpDevice();
    if (!device)
        GTEST_SKIP() << "no WARP device at feature level 11_0";

    const Frustum3f frustum = cameraFrustum();
    EXPECT_TRUE(frustum.Contains(Vector3f(0.0f, 0.0f, 8.0f)));
    EXPECT_FALSE(frustum.Contains(Vector3f(0.0f, 0.0f, 80.0f)));

    GrassBlade blades[6]{};
    blades[0].z        = 8.0f;
    blades[0].tileSlot = 0;
    blades[1].z        = 8.0f;
    blades[1].tileSlot = 1;
    blades[2].z        = 80.0f;
    blades[2].tileSlot = 2;
    blades[3].z        = 8.0f;
    blades[3].tileSlot = 3;
    blades[4].z        = 8.0f;
    blades[4].tileSlot = 1;
    blades[5].z        = 8.0f;
    blades[5].tileSlot = 0;

    GrassTileWind wind[4]{};
    wind[0].fade = 1.0f;
    wind[1].fade = 0.0f;
    wind[2].fade = 1.0f;
    wind[3].fade = 0.25f;

    ComPtr<ID3D12Resource> bladeUpload = uploadBuffer(device.Get(), blades, sizeof(blades));
    ComPtr<ID3D12Resource> windUpload  = uploadBuffer(device.Get(), wind, sizeof(wind));
    ASSERT_TRUE(bladeUpload);
    ASSERT_TRUE(windUpload);

    GrassPipeline pipeline;
    ASSERT_TRUE(pipeline.createCull(device.Get()));
    ASSERT_TRUE(pipeline.cullReady());
    EXPECT_EQ(pipeline.cullArgumentResource(2), nullptr);
    EXPECT_EQ(pipeline.cullIndexResource(2), nullptr);

    D3D12_GPU_VIRTUAL_ADDRESS bladeVa[4]{};
    bladeVa[0] = bladeUpload->GetGPUVirtualAddress();
    bladeVa[1] = bladeVa[0] + 4u * sizeof(GrassBlade);

    GrassLodSpan lods[4]{};
    lods[0].count = 4;
    lods[1].count = 2;

    float planes[6][4]{};
    for (int plane = 0; plane < 6; ++plane)
    {
        const Plane4f& p = frustum.GetPlane(plane);
        planes[plane][0] = p.x;
        planes[plane][1] = p.y;
        planes[plane][2] = p.z;
        planes[plane][3] = p.w;
    }

    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC   queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))));

    ComPtr<ID3D12CommandAllocator>    allocator;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmd))));

    GrassLodSpan              none[4]{};
    D3D12_GPU_VIRTUAL_ADDRESS noneVa[4]{};
    EXPECT_FALSE(pipeline.recordCull(cmd.Get(), 0, none, noneVa, windUpload->GetGPUVirtualAddress(), 4, planes));
    EXPECT_FALSE(pipeline.recordCull(cmd.Get(), 2, lods, bladeVa, windUpload->GetGPUVirtualAddress(), 4, planes));
    ASSERT_TRUE(pipeline.recordCull(cmd.Get(), 0, lods, bladeVa, windUpload->GetGPUVirtualAddress(), 4, planes));

    ID3D12Resource* args    = pipeline.cullArgumentResource(0);
    ID3D12Resource* indices = pipeline.cullIndexResource(0);
    ASSERT_NE(args, nullptr);
    ASSERT_NE(indices, nullptr);
    EXPECT_EQ(args->GetDesc().Width, 4u * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));

    const UINT64 lod0Bytes = 16;
    const UINT64 lod1Off   = static_cast<UINT64>(kGrassLod0Slots) * sizeof(uint32_t);
    ComPtr<ID3D12Resource> argRead  = readbackBuffer(device.Get(), args->GetDesc().Width);
    ComPtr<ID3D12Resource> lod0Read = readbackBuffer(device.Get(), lod0Bytes);
    ComPtr<ID3D12Resource> lod1Read = readbackBuffer(device.Get(), sizeof(uint32_t));
    ASSERT_TRUE(argRead);
    ASSERT_TRUE(lod0Read);
    ASSERT_TRUE(lod1Read);

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
    cmd->CopyBufferRegion(lod0Read.Get(), 0, indices, 0, lod0Bytes);
    cmd->CopyBufferRegion(lod1Read.Get(), 0, indices, lod1Off, sizeof(uint32_t));
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

    IndexedArgs argCopy[4]{};
    uint32_t    lod0Index[4]{};
    uint32_t    lod1Index = 0;
    void*       argMap    = nullptr;
    void*       lod0Map   = nullptr;
    void*       lod1Map   = nullptr;
    ASSERT_TRUE(SUCCEEDED(argRead->Map(0, nullptr, &argMap)));
    ASSERT_TRUE(SUCCEEDED(lod0Read->Map(0, nullptr, &lod0Map)));
    ASSERT_TRUE(SUCCEEDED(lod1Read->Map(0, nullptr, &lod1Map)));
    std::memcpy(argCopy, argMap, sizeof(argCopy));
    std::memcpy(lod0Index, lod0Map, sizeof(lod0Index));
    std::memcpy(&lod1Index, lod1Map, sizeof(lod1Index));
    argRead->Unmap(0, nullptr);
    lod0Read->Unmap(0, nullptr);
    lod1Read->Unmap(0, nullptr);
    pipeline.destroy();

    EXPECT_EQ(argCopy[0].indexCountPerInstance, 24u);
    EXPECT_EQ(argCopy[0].instanceCount, 2u);
    EXPECT_EQ(argCopy[0].startIndexLocation, 0u);
    EXPECT_EQ(argCopy[0].baseVertexLocation, 0);
    EXPECT_EQ(argCopy[0].startInstanceLocation, 0u);
    EXPECT_TRUE((lod0Index[0] == 0u && lod0Index[1] == 3u) || (lod0Index[0] == 3u && lod0Index[1] == 0u));

    EXPECT_EQ(argCopy[1].indexCountPerInstance, 12u);
    EXPECT_EQ(argCopy[1].instanceCount, 1u);
    EXPECT_EQ(argCopy[1].startInstanceLocation, 0u);
    EXPECT_NE(argCopy[1].startInstanceLocation, kGrassLod0Slots);
    EXPECT_EQ(lod1Index, 1u);

    EXPECT_EQ(argCopy[2].indexCountPerInstance, 6u);
    EXPECT_EQ(argCopy[2].instanceCount, 0u);
    EXPECT_EQ(argCopy[2].startInstanceLocation, 0u);
    EXPECT_EQ(argCopy[3].indexCountPerInstance, 6u);
    EXPECT_EQ(argCopy[3].instanceCount, 0u);
    EXPECT_EQ(argCopy[3].startInstanceLocation, 0u);
}
