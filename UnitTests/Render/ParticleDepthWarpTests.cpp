#include <gtest/gtest.h>

#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"
#include "Particles/GpuParticleSystem.h"
#include "Particles/ParticleDepthCollision.h"
#include "Render/Camera3D.h"
#include "Render/ParticleGpuPipeline.h"
#include "Render/ShaderCompile.h"

#include <d3d11shader.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

using namespace Dark;
using namespace Dark::Math;

namespace
{

ComPtr<ID3D12Device> tryWarpDevice()
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

ComPtr<ID3D12Resource> makeBuffer(ID3D12Device* device, UINT64 bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_HEAP_PROPERTIES props{};
    props.Type = heap;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width            = bytes;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.SampleDesc       = { 1, 0 };
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags            = flags;
    ComPtr<ID3D12Resource> res;
    if (FAILED(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&res))))
        return nullptr;
    return res;
}

void transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    if (!cmd || !resource || before == after)
        return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter  = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &barrier);
}

void ndcFromPixel(int x, int y, float width, float height, float& ndcX, float& ndcY)
{
    const float u = (static_cast<float>(x) + 0.5f) / width;
    const float v = (static_cast<float>(y) + 0.5f) / height;
    ndcX          = u * 2.0f - 1.0f;
    ndcY          = 1.0f - v * 2.0f;
}

bool pixelInRange(int x, int y, int width, int height)
{
    return x >= 0 && y >= 0 && x < width && y < height;
}

void expectVec(const Vector3f& got, const Vector3f& expected)
{
    EXPECT_NEAR(got.x, expected.x, 2.0e-3f);
    EXPECT_NEAR(got.y, expected.y, 2.0e-3f);
    EXPECT_NEAR(got.z, expected.z, 2.0e-3f);
}

Vector3f particlePos(const GpuParticle& particle)
{
    return Vector3f(particle.position[0], particle.position[1], particle.position[2]);
}

Vector3f particleVel(const GpuParticle& particle)
{
    return Vector3f(particle.velocity[0], particle.velocity[1], particle.velocity[2]);
}

} // namespace

TEST(ParticleDepthWarp, FiniteDifferenceMatchesCpu)
{
    ComPtr<ID3DBlob> cs;
    ASSERT_TRUE(compileShaderFromContent("shaders/ParticleSimulate.hlsl", "CSSimulate", "cs_5_0", cs));
    UINT cbBytes   = 0;
    UINT stepBytes = 0;
    ASSERT_TRUE(cbufferBytes(cs.Get(), "GpuParticleCb", cbBytes));
    ASSERT_TRUE(cbufferBytes(cs.Get(), "GpuParticleStep", stepBytes));
    EXPECT_EQ(cbBytes, 512u);
    EXPECT_EQ(stepBytes, 32u);

    ComPtr<ID3D12Device> device = tryWarpDevice();
    if (!device)
        GTEST_SKIP() << "no WARP device at feature level 11_0";

    ParticleGpuPipeline pipeline;
    ASSERT_TRUE(pipeline.create(device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM));

    constexpr int   kWidth     = 8;
    constexpr int   kHeight    = 8;
    constexpr float kNear      = 0.2f;
    constexpr float kSurfZ     = 5.0f;
    constexpr float kBias      = 0.02f;
    constexpr float kThickness = 0.20f;
    constexpr float kRestitution = 0.45f;
    constexpr float kFriction  = 0.20f;
    constexpr float kH         = 1.0f / 60.0f;
    const float     depthValue = kNear / kSurfZ;

    Camera3D camera;
    camera.SetLens(HalfPi, 1.0f, kNear, 200.0f);
    camera.LookAt(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));
    const Matrix4f viewProj    = camera.GetViewProj();
    const Matrix4f invViewProj = viewProj.Inverse();
    const Matrix4f view        = camera.GetView();
    const Vector3f look        = camera.GetLook();
    const Vector3f right       = camera.GetRight();

    const Vector3f hitVel  = look * 2.0f + right;
    const Vector3f hitPost(0.0f, 0.0f, kSurfZ + 0.05f);
    const Vector3f hitInitial = hitPost - hitVel * kH;
    const ParticleProjectUvResult hitUv = particleProjectUv(hitPost, viewProj);
    ASSERT_TRUE(hitUv.inside);
    const int hitPixX = static_cast<int>(hitUv.u * static_cast<float>(kWidth));
    const int hitPixY = static_cast<int>(hitUv.v * static_cast<float>(kHeight));
    ASSERT_TRUE(pixelInRange(hitPixX, hitPixY, kWidth, kHeight));

    const float hitViewZ = (view * Vector4f(hitPost, 1.0f)).z;
    const ParticleSurfaceViewZResult surface = particleSurfaceViewZ(depthValue, kNear);
    ASSERT_TRUE(surface.valid);
    const ParticleDepthHitResult hit = particleDepthHit(hitViewZ, surface.viewZ, kNear, kBias, kThickness, hitVel.Magnitude(), kH);
    ASSERT_TRUE(hit.hit);

    int nx = hitPixX;
    int ny = hitPixY;
    if (pixelInRange(hitPixX + 1, hitPixY, kWidth, kHeight))
        nx = hitPixX + 1;
    else if (pixelInRange(hitPixX - 1, hitPixY, kWidth, kHeight))
        nx = hitPixX - 1;
    else
        FAIL() << "no x neighbor";
    if (pixelInRange(hitPixX, hitPixY + 1, kWidth, kHeight))
        ny = hitPixY + 1;
    else if (pixelInRange(hitPixX, hitPixY - 1, kWidth, kHeight))
        ny = hitPixY - 1;
    else
        FAIL() << "no y neighbor";

    float cNdcX = 0.0f;
    float cNdcY = 0.0f;
    float xNdcX = 0.0f;
    float xNdcY = 0.0f;
    float yNdcX = 0.0f;
    float yNdcY = 0.0f;
    ndcFromPixel(hitPixX, hitPixY, static_cast<float>(kWidth), static_cast<float>(kHeight), cNdcX, cNdcY);
    ndcFromPixel(nx, hitPixY, static_cast<float>(kWidth), static_cast<float>(kHeight), xNdcX, xNdcY);
    ndcFromPixel(hitPixX, ny, static_cast<float>(kWidth), static_cast<float>(kHeight), yNdcX, yNdcY);
    const Vector3f center = particleReconstructWorld(cNdcX, cNdcY, depthValue, invViewProj);
    const Vector3f alongX = particleReconstructWorld(xNdcX, xNdcY, depthValue, invViewProj);
    const Vector3f alongY = particleReconstructWorld(yNdcX, yNdcY, depthValue, invViewProj);
    const Vector3f normal = particleDepthNormal(center, alongX, alongY, camera.GetPosition());
    ASSERT_GT(normal.MagnitudeSqrd(), 1.0e-8f);

    const float resolvedZ = particleResolveViewZ(surface.viewZ, kBias);
    const Vector3f hitExpectedPos = hitPost - look * (hitViewZ - resolvedZ);
    const ParticleReflectResult reflected = particleReflectVelocity(hitVel, normal, kRestitution, kFriction);
    ASSERT_GT(reflected.velocity.Magnitude(), 0.15f);

    const Vector3f missVel(0.2f, 0.05f, 0.0f);
    const Vector3f missPost(0.15f, -0.2f, 2.0f);
    const Vector3f missInitial = missPost - missVel * kH;
    const ParticleProjectUvResult missUv = particleProjectUv(missPost, viewProj);
    ASSERT_TRUE(missUv.inside);
    const float missViewZ = (view * Vector4f(missPost, 1.0f)).z;
    const ParticleDepthHitResult miss = particleDepthHit(missViewZ, surface.viewZ, kNear, kBias, kThickness, missVel.Magnitude(), kH);
    ASSERT_FALSE(miss.hit);

    GpuParticle seeds[2]{};
    seeds[0].position[0] = hitInitial.x;
    seeds[0].position[1] = hitInitial.y;
    seeds[0].position[2] = hitInitial.z;
    seeds[0].velocity[0] = hitVel.x;
    seeds[0].velocity[1] = hitVel.y;
    seeds[0].velocity[2] = hitVel.z;
    seeds[0].life        = 1.0f;
    seeds[0].maxLife     = 1.0f;
    seeds[0].flags       = 1u;
    seeds[1].position[0] = missInitial.x;
    seeds[1].position[1] = missInitial.y;
    seeds[1].position[2] = missInitial.z;
    seeds[1].velocity[0] = missVel.x;
    seeds[1].velocity[1] = missVel.y;
    seeds[1].velocity[2] = missVel.z;
    seeds[1].life        = 1.0f;
    seeds[1].maxLife     = 1.0f;
    seeds[1].flags       = 1u;

    GpuParticleConstants constants{};
    std::memcpy(constants.viewProj, viewProj.m_afEntry, sizeof(constants.viewProj));
    std::memcpy(constants.invViewProj, invViewProj.m_afEntry, sizeof(constants.invViewProj));
    std::memcpy(constants.view, view.m_afEntry, sizeof(constants.view));
    constants.cameraRight[0] = right.x;
    constants.cameraRight[1] = right.y;
    constants.cameraRight[2] = right.z;
    constants.cameraUp[0]    = camera.GetUp().x;
    constants.cameraUp[1]    = camera.GetUp().y;
    constants.cameraUp[2]    = camera.GetUp().z;
    constants.cameraPos[0]   = camera.GetPosition().x;
    constants.cameraPos[1]   = camera.GetPosition().y;
    constants.cameraPos[2]   = camera.GetPosition().z;
    constants.cameraLook[0]  = look.x;
    constants.cameraLook[1]  = look.y;
    constants.cameraLook[2]  = look.z;
    constants.sim0[0]        = 2u;
    constants.sim2[3]        = kH;
    constants.collide0[0]    = kNear;
    constants.collide0[1]    = static_cast<float>(kWidth);
    constants.collide0[2]    = static_cast<float>(kHeight);
    constants.collide0[3]    = kBias;
    constants.collide1[0]    = kThickness;
    constants.collide1[1]    = kRestitution;
    constants.collide1[2]    = kFriction;
    constants.collide1[3]    = 0.2f;
    constants.collide2[0]    = static_cast<uint32_t>(ParticleCollisionReaction::Bounce);
    constants.collide2[2]    = 1u;

    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width            = kWidth;
    texDesc.Height           = kHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels        = 1;
    texDesc.Format           = DXGI_FORMAT_R32_FLOAT;
    texDesc.SampleDesc       = { 1, 0 };
    texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 texBytes = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &texBytes);
    ASSERT_GT(texBytes, 0u);

    std::vector<uint8_t> texUpload(static_cast<size_t>(texBytes), 0);
    for (int y = 0; y < kHeight; ++y)
    {
        float* row = reinterpret_cast<float*>(texUpload.data() + static_cast<size_t>(y) * footprint.Footprint.RowPitch);
        for (int x = 0; x < kWidth; ++x)
            row[x] = depthValue;
    }

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> depthTex;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&depthTex))));
    D3D12_RESOURCE_DESC attribDesc = texDesc;
    attribDesc.Width               = 1;
    attribDesc.Height              = 1;
    attribDesc.Format              = DXGI_FORMAT_R8G8B8A8_UNORM;
    ComPtr<ID3D12Resource> attribTex;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &attribDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&attribTex))));

    ComPtr<ID3D12Resource> depthUpload = makeBuffer(device.Get(), texBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    ASSERT_TRUE(depthUpload);
    void* mapped = nullptr;
    ASSERT_TRUE(SUCCEEDED(depthUpload->Map(0, nullptr, &mapped)));
    std::memcpy(mapped, texUpload.data(), texUpload.size());
    depthUpload->Unmap(0, nullptr);

    ComPtr<ID3D12Resource> prev = makeBuffer(device.Get(), sizeof(seeds), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    ComPtr<ID3D12Resource> curr = makeBuffer(device.Get(), sizeof(seeds), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> seedUpload = makeBuffer(device.Get(), sizeof(seeds), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    ComPtr<ID3D12Resource> cb = makeBuffer(device.Get(), sizeof(GpuParticleConstants), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    ComPtr<ID3D12Resource> events = makeBuffer(device.Get(), 256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> counter = makeBuffer(device.Get(), 256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> childHead = makeBuffer(device.Get(), 256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> readback = makeBuffer(device.Get(), sizeof(seeds), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    ASSERT_TRUE(prev && curr && seedUpload && cb && events && counter && childHead && readback);
    ASSERT_TRUE(SUCCEEDED(seedUpload->Map(0, nullptr, &mapped)));
    std::memcpy(mapped, seeds, sizeof(seeds));
    seedUpload->Unmap(0, nullptr);
    ASSERT_TRUE(SUCCEEDED(cb->Map(0, nullptr, &mapped)));
    std::memcpy(mapped, &constants, sizeof(constants));
    cb->Unmap(0, nullptr);

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 2;
    heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;
    ASSERT_TRUE(SUCCEEDED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap))));
    const UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv{};
    depthSrv.Format                  = DXGI_FORMAT_R32_FLOAT;
    depthSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrv.Texture2D.MipLevels     = 1;
    device->CreateShaderResourceView(depthTex.Get(), &depthSrv, cpu);
    D3D12_SHADER_RESOURCE_VIEW_DESC attribSrv = depthSrv;
    attribSrv.Format                         = DXGI_FORMAT_R8G8B8A8_UNORM;
    cpu.ptr += inc;
    device->CreateShaderResourceView(attribTex.Get(), &attribSrv, cpu);

    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC   queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))));
    ComPtr<ID3D12CommandAllocator>    allocator;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmd))));

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource       = depthUpload.Get();
    src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource        = depthTex.Get();
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    cmd->CopyBufferRegion(prev.Get(), 0, seedUpload.Get(), 0, sizeof(seeds));
    transition(cmd.Get(), depthTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(cmd.Get(), prev.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    ID3D12DescriptorHeap* heaps[] = { heap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    pipeline.bindSimulate(cmd.Get(), false);
    cmd->SetComputeRootConstantBufferView(ParticleGpuPipeline::kSimCb, cb->GetGPUVirtualAddress());
    cmd->SetComputeRootShaderResourceView(ParticleGpuPipeline::kSimPrev, prev->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(ParticleGpuPipeline::kSimCurr, curr->GetGPUVirtualAddress());
    const uint32_t step[8] = { 0u, 0u, 2u, static_cast<uint32_t>(ParticleCollisionReaction::Bounce), 0u, 1u, 0u, 0u };
    cmd->SetComputeRoot32BitConstants(ParticleGpuPipeline::kSimStep, 8, step, 0);
    cmd->SetComputeRootDescriptorTable(ParticleGpuPipeline::kSimTextures, heap->GetGPUDescriptorHandleForHeapStart());
    cmd->SetComputeRootUnorderedAccessView(ParticleGpuPipeline::kSimEvents, events->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(ParticleGpuPipeline::kSimCounter, counter->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(ParticleGpuPipeline::kSimChildHead, childHead->GetGPUVirtualAddress());
    cmd->Dispatch(1, 1, 1);

    D3D12_RESOURCE_BARRIER done{};
    done.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    done.UAV.pResource = curr.Get();
    cmd->ResourceBarrier(1, &done);
    transition(cmd.Get(), curr.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd->CopyBufferRegion(readback.Get(), 0, curr.Get(), 0, sizeof(seeds));
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

    GpuParticle got[2]{};
    ASSERT_TRUE(SUCCEEDED(readback->Map(0, nullptr, &mapped)));
    std::memcpy(got, mapped, sizeof(got));
    readback->Unmap(0, nullptr);
    pipeline.destroy();

    expectVec(particlePos(got[0]), hitExpectedPos);
    expectVec(particleVel(got[0]), reflected.velocity);
    EXPECT_NEAR(got[0].life, 1.0f - kH, 2.0e-3f);
    EXPECT_EQ(got[0].flags, 1u);

    expectVec(particlePos(got[1]), missPost);
    expectVec(particleVel(got[1]), missVel);
    EXPECT_NEAR(got[1].life, 1.0f - kH, 2.0e-3f);
    EXPECT_EQ(got[1].flags, 1u);
}
