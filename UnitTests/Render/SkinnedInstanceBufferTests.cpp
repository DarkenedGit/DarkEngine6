#include <gtest/gtest.h>

#include "Animation/Pose.h"
#include "Render/Mesh.h"
#include "Render/ShaderCompile.h"
#include "Render/SkinnedInstanceBuffer.h"

#include "Math/Matrix4f.h"
#include "Math/Vector4f.h"

#include <d3dcompiler.h>
#include <dxgi1_4.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace Dark;
using namespace Dark::Math;

namespace
{

ComPtr<ID3D12Device> tryCreateWarpDevice()
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

SkinnedPartSubmit partOf(const Mesh* mesh, const AnimPose* pose, AssetID material, StaticPopulation population, bool castShadow, bool alphaMask)
{
    SkinnedPartSubmit part;
    part.mesh       = mesh;
    part.pose       = pose;
    part.material   = material;
    part.population = population;
    part.castShadow = castShadow;
    part.alphaMask  = alphaMask;
    return part;
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

bool variableOffset(ID3DBlob* blob, const char* cbName, const char* varName, UINT& outOffset)
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
        if (!cb || FAILED(cb->GetDesc(&cbDesc)) || !cbDesc.Name || std::strcmp(cbDesc.Name, cbName) != 0)
            continue;
        ID3D11ShaderReflectionVariable* var = cb->GetVariableByName(varName);
        D3D11_SHADER_VARIABLE_DESC      varDesc{};
        if (!var || FAILED(var->GetDesc(&varDesc)))
            return false;
        outOffset = varDesc.StartOffset;
        return true;
    }
    return false;
}

bool bindPoint(ID3DBlob* blob, const char* name, UINT& outPoint)
{
    ComPtr<ID3D11ShaderReflection> reflect;
    if (!blob || FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&reflect))))
        return false;
    D3D11_SHADER_INPUT_BIND_DESC desc{};
    if (FAILED(reflect->GetResourceBindingDescByName(name, &desc)))
        return false;
    outPoint = desc.BindPoint;
    return true;
}

bool disassemblyHas(ID3DBlob* blob, const char* needle)
{
    ComPtr<ID3DBlob> dump;
    if (!blob || !needle || FAILED(D3DDisassemble(blob->GetBufferPointer(), blob->GetBufferSize(), 0, nullptr, &dump)) || !dump)
        return false;
    const std::string text(static_cast<const char*>(dump->GetBufferPointer()));
    return text.find(needle) != std::string::npos;
}

} // namespace

TEST(SkinnedInstance, NullDeviceIsInvalid)
{
    SkinnedInstanceBuffer buffer;
    EXPECT_FALSE(buffer.create(nullptr));
    EXPECT_FALSE(buffer.isValid());
}

TEST(SkinnedInstance, OnePoseUploadsOnceAndDepthSpanMergesMaterials)
{
    Mesh meshLow  = Mesh::takeSortIdentity();
    Mesh meshHigh = Mesh::takeSortIdentity();
    ASSERT_LT(meshLow.sortKey(), meshHigh.sortKey());

    AnimPose poseA{};
    AnimPose poseB{};
    poseA.boneCount = 1;
    poseB.boneCount = 1;
    poseA.palette[0] = Matrix4f::TranslationMatrix(4.0f, 5.0f, 6.0f);

    SkinnedInstanceBuffer buffer;
    ASSERT_TRUE(buffer.submit(partOf(&meshHigh, &poseA, 1, StaticPopulation::OpaqueModels, true, false)));
    ASSERT_TRUE(buffer.submit(partOf(&meshLow, &poseA, 2, StaticPopulation::OpaqueModels, true, false)));
    ASSERT_TRUE(buffer.submit(partOf(&meshLow, &poseB, 1, StaticPopulation::OpaqueModels, false, false)));
    ASSERT_TRUE(buffer.submit(partOf(&meshLow, &poseB, 1, StaticPopulation::OpaqueModels, true, true)));
    ASSERT_TRUE(buffer.submit(partOf(&meshLow, &poseA, 1, StaticPopulation::OpaqueMeshes, true, false)));

    EXPECT_EQ(buffer.partCount(), 5u);
    EXPECT_EQ(buffer.paletteCount(), 2u);
    EXPECT_EQ(buffer.gbufferBatchCount(), 5u);

    const uint32_t* gIndices = buffer.gbufferIndices();
    ASSERT_NE(gIndices, nullptr);
    EXPECT_EQ(gIndices[0], 4u);
    EXPECT_EQ(gIndices[1], 2u);
    EXPECT_EQ(gIndices[2], 1u);
    EXPECT_EQ(gIndices[3], 3u);
    EXPECT_EQ(gIndices[4], 0u);

    EXPECT_EQ(buffer.gbufferBatch(0).itemBegin, 0u);
    EXPECT_EQ(buffer.gbufferBatch(0).key.population, static_cast<uint32_t>(StaticPopulation::OpaqueMeshes));
    EXPECT_EQ(buffer.gbufferBatch(1).itemBegin, 1u);
    EXPECT_EQ(buffer.gbufferBatch(1).key.alphaMask, 0u);
    EXPECT_EQ(buffer.gbufferBatch(3).itemBegin, 3u);
    EXPECT_EQ(buffer.gbufferBatch(3).key.alphaMask, 1u);
    EXPECT_EQ(buffer.gbufferBatch(3).key.material, 1u);

    EXPECT_EQ(buffer.depthBatchCount(), 3u);
    EXPECT_EQ(buffer.depthBatch(0).population, static_cast<uint32_t>(StaticPopulation::OpaqueMeshes));
    EXPECT_EQ(buffer.depthBatch(0).itemCount, 1u);
    EXPECT_EQ(buffer.depthBatch(1).population, static_cast<uint32_t>(StaticPopulation::OpaqueModels));
    EXPECT_EQ(buffer.depthBatch(1).meshKey, meshLow.sortKey());
    EXPECT_EQ(buffer.depthBatch(1).itemBegin, 1u);
    EXPECT_EQ(buffer.depthBatch(1).itemCount, 2u);
    EXPECT_EQ(buffer.depthBatch(2).meshKey, meshHigh.sortKey());
    EXPECT_EQ(buffer.depthBatch(2).itemBegin, 3u);

    const uint32_t* dIndices = buffer.depthIndices();
    ASSERT_NE(dIndices, nullptr);
    EXPECT_EQ(dIndices[0], 4u);
    EXPECT_EQ(dIndices[1], 1u);
    EXPECT_EQ(dIndices[2], 3u);
    EXPECT_EQ(dIndices[3], 0u);

    const SkinnedInstanceGpu* instances = buffer.instances();
    ASSERT_NE(instances, nullptr);
    EXPECT_EQ(instances[0].paletteIndex, 0u);
    EXPECT_EQ(instances[1].paletteIndex, 0u);
    EXPECT_EQ(instances[2].paletteIndex, 1u);
    EXPECT_EQ(instances[3].paletteIndex, 1u);
    EXPECT_EQ(instances[4].paletteIndex, 0u);
    EXPECT_EQ(instances[0].pad[0], 0u);
}

TEST(SkinnedInstance, CapsRejectTheExtraPartAndRollbackDropsTheCharacter)
{
    Mesh     mesh = Mesh::takeSortIdentity();
    AnimPose shared{};
    SkinnedInstanceBuffer parts;
    for (uint32_t i = 0; i < kMaxSkinnedParts; ++i)
    {
        SkinnedPartSubmit part = partOf(&mesh, &shared, 1, StaticPopulation::OpaqueModels, true, false);
        part.color[0]          = static_cast<float>(i);
        ASSERT_TRUE(parts.submit(part));
    }
    EXPECT_FALSE(parts.submit(partOf(&mesh, &shared, 1, StaticPopulation::OpaqueModels, true, false)));
    EXPECT_EQ(parts.partCount(), kMaxSkinnedParts);
    EXPECT_EQ(parts.paletteCount(), 1u);

    SkinnedPartSubmit missing;
    missing.pose = &shared;
    EXPECT_FALSE(parts.submit(missing));

    std::vector<AnimPose> poses(kMaxSkinnedPalettes + 1u);
    SkinnedInstanceBuffer palettes;
    for (uint32_t i = 0; i < kMaxSkinnedPalettes; ++i)
        ASSERT_TRUE(palettes.submit(partOf(&mesh, &poses[i], 1, StaticPopulation::OpaqueModels, true, false)));
    const uint32_t full = palettes.mark();
    EXPECT_FALSE(palettes.submit(partOf(&mesh, &poses[kMaxSkinnedPalettes], 1, StaticPopulation::OpaqueModels, true, false)));
    EXPECT_EQ(palettes.partCount(), full);

    palettes.rollback(full - 1u);
    const uint32_t character = palettes.mark();
    ASSERT_TRUE(palettes.submit(partOf(&mesh, &poses[kMaxSkinnedPalettes], 1, StaticPopulation::OpaqueModels, true, false)));
    ASSERT_TRUE(palettes.submit(partOf(&mesh, &poses[0], 1, StaticPopulation::OpaqueModels, true, false)));
    AnimPose overflow{};
    EXPECT_FALSE(palettes.submit(partOf(&mesh, &overflow, 1, StaticPopulation::OpaqueModels, true, false)));
    palettes.rollback(character);
    EXPECT_EQ(palettes.partCount(), character);
    EXPECT_EQ(palettes.paletteCount(), character);

    palettes.beginFrame(2);
    EXPECT_EQ(palettes.partCount(), 0u);
    ASSERT_TRUE(palettes.submit(partOf(&mesh, &overflow, 1, StaticPopulation::OpaqueModels, true, false)));
    EXPECT_EQ(palettes.paletteCount(), 1u);
}

TEST(SkinnedInstance, WarpKeepsTheOtherFrameSlot)
{
    ComPtr<ID3D12Device> device = tryCreateWarpDevice();
    if (!device)
        GTEST_SKIP() << "no WARP device at feature level 11_0";

    SkinnedInstanceBuffer buffer;
    ASSERT_TRUE(buffer.create(device.Get()));
    EXPECT_TRUE(buffer.isValid());

    Mesh     mesh = Mesh::takeSortIdentity();
    AnimPose poseA{};
    poseA.boneCount    = 1;
    poseA.palette[0]   = Matrix4f::TranslationMatrix(4.0f, 5.0f, 6.0f);
    poseA.prevPalette[0] = Matrix4f::TranslationMatrix(7.0f, 8.0f, 9.0f);

    buffer.beginFrame(0);
    SkinnedPartSubmit part = partOf(&mesh, &poseA, 1, StaticPopulation::OpaqueModels, true, false);
    part.world             = Matrix4f::TranslationMatrix(10.0f, 0.0f, 0.0f);
    part.color[0]          = 1.0f;
    part.color[3]          = 4.0f;
    ASSERT_TRUE(buffer.submit(part));

    const BonePaletteCB* slot0 = buffer.uploadedPalettes(0);
    ASSERT_NE(slot0, nullptr);
    EXPECT_FLOAT_EQ(slot0[0].curr[0][12], 4.0f);
    EXPECT_FLOAT_EQ(slot0[0].curr[0][13], 5.0f);
    EXPECT_FLOAT_EQ(slot0[0].prev[0][12], 7.0f);
    EXPECT_FLOAT_EQ(slot0[0].curr[63][0], 1.0f);
    EXPECT_FLOAT_EQ(slot0[0].curr[63][1], 0.0f);
    EXPECT_FLOAT_EQ(slot0[0].curr[63][15], 1.0f);

    const SkinnedInstanceGpu* inst0 = buffer.uploadedInstances(0);
    ASSERT_NE(inst0, nullptr);
    EXPECT_EQ(inst0[0].paletteIndex, 0u);
    EXPECT_FLOAT_EQ(inst0[0].world[12], 10.0f);
    EXPECT_FLOAT_EQ(inst0[0].color[3], 4.0f);
    EXPECT_EQ(inst0[0].pad[2], 0u);

    AnimPose poseB{};
    poseB.boneCount  = 1;
    poseB.palette[0] = Matrix4f::TranslationMatrix(1.0f, 0.0f, 0.0f);
    buffer.beginFrame(1);
    part.pose = &poseB;
    ASSERT_TRUE(buffer.submit(part));
    const BonePaletteCB* slot1 = buffer.uploadedPalettes(1);
    ASSERT_NE(slot1, nullptr);
    EXPECT_FLOAT_EQ(slot1[0].curr[0][12], 1.0f);
    const BonePaletteCB* still0 = buffer.uploadedPalettes(0);
    ASSERT_NE(still0, nullptr);
    EXPECT_FLOAT_EQ(still0[0].curr[0][12], 4.0f);
    EXPECT_FLOAT_EQ(still0[0].prev[0][14], 9.0f);
}

TEST(SkinnedInstance, ShadersKeepPaletteAndBatchLayout)
{
    EXPECT_EQ(sizeof(SkinnedInstanceGpu), 160u);
    EXPECT_EQ(sizeof(BonePaletteCB), 8192u);
    EXPECT_EQ(sizeof(MeshBatchConstants), 160u);
    EXPECT_EQ(sizeof(MeshDepthBatchConstants), 80u);

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    ComPtr<ID3DBlob> depth;
    ASSERT_TRUE(compileShaderFromContent("shaders/SkinnedMeshGBufferInstanced.hlsl", "VSMain", "vs_5_0", vs));
    ASSERT_TRUE(compileShaderFromContent("shaders/SkinnedMeshGBufferInstanced.hlsl", "PSMain", "ps_5_0", ps));
    ASSERT_TRUE(compileShaderFromContent("shaders/SkinnedShadowDepthInstanced.hlsl", "VSMain", "vs_5_0", depth));

    UINT vsBytes = 0;
    UINT psBytes = 0;
    UINT depthBytes = 0;
    ASSERT_TRUE(cbufferBytes(vs.Get(), "MeshBatchConstants", vsBytes));
    ASSERT_TRUE(cbufferBytes(ps.Get(), "MeshBatchConstants", psBytes));
    ASSERT_TRUE(cbufferBytes(depth.Get(), "MeshDepthBatchConstants", depthBytes));
    EXPECT_EQ(vsBytes, 160u);
    EXPECT_EQ(psBytes, 160u);
    EXPECT_EQ(depthBytes, 80u);

    UINT vsBase = 0;
    UINT psBase = 0;
    UINT depthBase = 0;
    UINT rough = 0;
    ASSERT_TRUE(variableOffset(vs.Get(), "MeshBatchConstants", "baseIndex", vsBase));
    ASSERT_TRUE(variableOffset(ps.Get(), "MeshBatchConstants", "baseIndex", psBase));
    ASSERT_TRUE(variableOffset(vs.Get(), "MeshBatchConstants", "roughness", rough));
    ASSERT_TRUE(variableOffset(depth.Get(), "MeshDepthBatchConstants", "baseIndex", depthBase));
    EXPECT_EQ(vsBase, 152u);
    EXPECT_EQ(psBase, 152u);
    EXPECT_EQ(rough, 128u);
    EXPECT_EQ(depthBase, 64u);

    UINT instances = 0;
    UINT indices   = 0;
    UINT palettes  = 0;
    ASSERT_TRUE(bindPoint(vs.Get(), "gInstances", instances));
    ASSERT_TRUE(bindPoint(vs.Get(), "gIndices", indices));
    ASSERT_TRUE(bindPoint(vs.Get(), "gPalettes", palettes));
    EXPECT_EQ(instances, 4u);
    EXPECT_EQ(indices, 5u);
    EXPECT_EQ(palettes, 6u);

    UINT depthInstances = 0;
    UINT depthIndices   = 0;
    UINT depthPalettes  = 0;
    ASSERT_TRUE(bindPoint(depth.Get(), "gInstances", depthInstances));
    ASSERT_TRUE(bindPoint(depth.Get(), "gIndices", depthIndices));
    ASSERT_TRUE(bindPoint(depth.Get(), "gPalettes", depthPalettes));
    EXPECT_EQ(depthInstances, 4u);
    EXPECT_EQ(depthIndices, 5u);
    EXPECT_EQ(depthPalettes, 6u);

    UINT albedo = 0;
    ASSERT_TRUE(bindPoint(ps.Get(), "gAlbedo", albedo));
    EXPECT_EQ(albedo, 0u);

    EXPECT_TRUE(disassemblyHas(vs.Get(), "dcl_resource_structured t4, 160"));
    EXPECT_TRUE(disassemblyHas(vs.Get(), "dcl_resource_structured t6, 2048"));
    EXPECT_TRUE(disassemblyHas(depth.Get(), "dcl_resource_structured t4, 160"));
    EXPECT_TRUE(disassemblyHas(depth.Get(), "dcl_resource_structured t6, 2048"));
}

TEST(SkinnedInstance, BoneThenPartWorld)
{
    const Matrix4f bone     = Matrix4f::ScaleMatrixXYZ(2.0f, 3.0f, 4.0f);
    const Matrix4f world    = Matrix4f::TranslationMatrix(10.0f, 0.0f, 0.0f);
    const Matrix4f viewProj = Matrix4f::TranslationMatrix(0.0f, 1.0f, 0.0f);
    const Vector4f point(1.0f, 1.0f, 1.0f, 1.0f);

    const Vector4f clip    = viewProj * (world * (bone * point));
    const Vector4f swapped = viewProj * (bone * (world * point));
    EXPECT_NEAR(clip.x, 12.0f, 1.0e-4f);
    EXPECT_NEAR(clip.y, 4.0f, 1.0e-4f);
    EXPECT_NEAR(clip.z, 4.0f, 1.0e-4f);
    EXPECT_NEAR(clip.w, 1.0f, 1.0e-4f);
    EXPECT_GT(std::fabs(swapped.x - clip.x), 1.0f);

    const Vector4f normal = world * (bone * Vector4f(0.0f, 1.0f, 0.0f, 0.0f));
    EXPECT_NEAR(normal.x, 0.0f, 1.0e-4f);
    EXPECT_NEAR(normal.y, 3.0f, 1.0e-4f);
    EXPECT_NEAR(normal.z, 0.0f, 1.0e-4f);
    EXPECT_NEAR(normal.w, 0.0f, 1.0e-4f);
}
