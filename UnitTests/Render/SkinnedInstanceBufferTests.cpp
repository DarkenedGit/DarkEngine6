#include <gtest/gtest.h>

#include "Animation/Pose.h"
#include "Render/Camera3D.h"
#include "Render/DrawList.h"
#include "Render/Frustum3f.h"
#include "Render/GpuModel.h"
#include "Render/GpuResourceCache.h"
#include "Render/InstanceCull.h"
#include "Render/Mesh.h"
#include "Render/ModelDraw.h"
#include "Render/ShaderCompile.h"
#include "Render/SkinnedInstanceBuffer.h"

#include "Math/AABox3f.h"
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
    EXPECT_TRUE(buffer.forwardReady());

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

TEST(SkinnedInstance, SubmitBakesWorldAndRollsBackTheCharacter)
{
    const Matrix4f localA     = Matrix4f::ScaleMatrixXYZ(2.0f, 1.0f, 1.0f);
    const Matrix4f localB     = Matrix4f::TranslationMatrix(1.0f, 2.0f, 3.0f);
    const Matrix4f entity     = Matrix4f::TranslationMatrix(10.0f, 0.0f, 0.0f);
    const Matrix4f prevEntity = Matrix4f::TranslationMatrix(4.0f, 0.0f, 0.0f);

    GpuModel::Part partA;
    partA.mesh        = Mesh::takeSortIdentity();
    partA.localToRoot = localA;
    partA.skinned     = true;
    GpuModel::Part partB;
    partB.mesh        = Mesh::takeSortIdentity();
    partB.localToRoot = localB;
    partB.skinned     = true;
    GpuModel::Part staticPart;
    staticPart.mesh    = Mesh::takeSortIdentity();
    staticPart.skinned = false;
    GpuModel::Part translucentFlag;
    translucentFlag.mesh        = Mesh::takeSortIdentity();
    translucentFlag.skinned     = true;
    translucentFlag.translucent = true;
    GpuModel::Part emptyPart;
    emptyPart.skinned = true;
    GpuModel::Part translucentOnly;
    translucentOnly.mesh    = Mesh::takeSortIdentity();
    translucentOnly.skinned = true;

    std::vector<GpuModel::Part> opaqueParts;
    opaqueParts.push_back(std::move(partA));
    opaqueParts.push_back(std::move(partB));
    opaqueParts.push_back(std::move(staticPart));
    opaqueParts.push_back(std::move(translucentFlag));
    opaqueParts.push_back(std::move(emptyPart));
    std::vector<GpuModel::Part> translucentParts;
    translucentParts.push_back(std::move(translucentOnly));
    const GpuModel model(std::move(opaqueParts), std::move(translucentParts));

    AnimPose         pose{};
    pose.boneCount = 1;
    GpuResourceCache gpu(nullptr);
    SkinnedInstanceBuffer buffer;
    ASSERT_TRUE(submitSkinnedModelOpaque(buffer, gpu, model, &pose, entity, prevEntity, StaticPopulation::OpaqueModels, true));
    EXPECT_EQ(buffer.partCount(), 2u);
    EXPECT_EQ(buffer.paletteCount(), 1u);

    const Matrix4f expectA     = localA * entity;
    const Matrix4f expectPrevA = localA * prevEntity;
    const Matrix4f expectB     = localB * entity;
    const Matrix4f expectPrevB = localB * prevEntity;
    const SkinnedInstanceGpu* instances = buffer.instances();
    ASSERT_NE(instances, nullptr);
    EXPECT_EQ(instances[0].paletteIndex, instances[1].paletteIndex);
    EXPECT_FLOAT_EQ(instances[0].color[0], 1.0f);
    EXPECT_FLOAT_EQ(instances[0].color[3], 0.0f);
    for (int i = 0; i < 16; ++i)
    {
        EXPECT_FLOAT_EQ(instances[0].world[i], expectA.m_afEntry[i]);
        EXPECT_FLOAT_EQ(instances[0].prevWorld[i], expectPrevA.m_afEntry[i]);
        EXPECT_FLOAT_EQ(instances[1].world[i], expectB.m_afEntry[i]);
        EXPECT_FLOAT_EQ(instances[1].prevWorld[i], expectPrevB.m_afEntry[i]);
    }

    uint32_t depthItems = 0;
    for (uint32_t i = 0; i < buffer.depthBatchCount(); ++i)
        depthItems += buffer.depthBatch(i).itemCount;
    EXPECT_EQ(depthItems, 2u);

    EXPECT_FALSE(submitSkinnedModelOpaque(buffer, gpu, model, nullptr, entity, prevEntity, StaticPopulation::OpaqueModels, true));
    EXPECT_EQ(buffer.partCount(), 2u);

    Mesh                  filler = Mesh::takeSortIdentity();
    AnimPose              shared{};
    SkinnedInstanceBuffer capped;
    for (uint32_t i = 0; i < kMaxSkinnedParts - 1u; ++i)
        ASSERT_TRUE(capped.submit(partOf(&filler, &shared, 1, StaticPopulation::OpaqueModels, true, false)));
    const uint32_t before = capped.partCount();
    EXPECT_FALSE(submitSkinnedModelOpaque(capped, gpu, model, &pose, entity, prevEntity, StaticPopulation::OpaqueModels, true));
    EXPECT_EQ(capped.partCount(), before);
    EXPECT_EQ(capped.paletteCount(), 1u);
}

TEST(SkinnedInstance, ForwardSignatureIs45)
{
    EXPECT_EQ(36u + 1u + 2u + 2u + 2u + 2u, 45u);
    EXPECT_EQ(sizeof(MeshForwardBatchConstants), 144u);
    EXPECT_EQ(SkinnedInstanceBuffer::kForwardConstants, 0u);
    EXPECT_EQ(SkinnedInstanceBuffer::kForwardMaterial, 1u);
    EXPECT_EQ(SkinnedInstanceBuffer::kForwardShadow, 2u);
    EXPECT_EQ(SkinnedInstanceBuffer::kForwardInstances, 3u);
    EXPECT_EQ(SkinnedInstanceBuffer::kForwardIndices, 4u);
    EXPECT_EQ(SkinnedInstanceBuffer::kForwardPalettes, 5u);

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    ASSERT_TRUE(compileShaderFromContent("shaders/SkinnedMeshForwardInstanced.hlsl", "VSMain", "vs_5_0", vs));
    ASSERT_TRUE(compileShaderFromContent("shaders/SkinnedMeshForwardInstanced.hlsl", "PSMain", "ps_5_0", ps));

    UINT vsBytes = 0;
    UINT psBytes = 0;
    ASSERT_TRUE(cbufferBytes(vs.Get(), "MeshForwardBatchConstants", vsBytes));
    ASSERT_TRUE(cbufferBytes(ps.Get(), "MeshForwardBatchConstants", psBytes));
    EXPECT_EQ(vsBytes, 144u);
    EXPECT_EQ(psBytes, 144u);

    UINT base = 0;
    ASSERT_TRUE(variableOffset(vs.Get(), "MeshForwardBatchConstants", "baseIndex", base));
    EXPECT_EQ(base, 128u);

    UINT instances = 0;
    UINT indices   = 0;
    UINT palettes  = 0;
    UINT albedo    = 0;
    UINT shadow    = 0;
    ASSERT_TRUE(bindPoint(vs.Get(), "gInstances", instances));
    ASSERT_TRUE(bindPoint(vs.Get(), "gIndices", indices));
    ASSERT_TRUE(bindPoint(vs.Get(), "gPalettes", palettes));
    ASSERT_TRUE(bindPoint(ps.Get(), "gAlbedo", albedo));
    ASSERT_TRUE(bindPoint(ps.Get(), "gShadowMap", shadow));
    EXPECT_EQ(instances, 5u);
    EXPECT_EQ(indices, 6u);
    EXPECT_EQ(palettes, 7u);
    EXPECT_EQ(albedo, 0u);
    EXPECT_EQ(shadow, 4u);

    EXPECT_TRUE(disassemblyHas(vs.Get(), "dcl_resource_structured t5, 160"));
    EXPECT_TRUE(disassemblyHas(vs.Get(), "dcl_resource_structured t7, 2048"));
}

TEST(SkinnedInstance, CullAabbUsesOneWorldBoxAndSkipsInvalidCenters)
{
    Mesh meshA = Mesh::takeSortIdentity();
    Mesh meshB = Mesh::takeSortIdentity();
    AnimPose pose{};
    pose.boneCount = 1;

    const AABox3f asset(Vector3f(-1.0f, -2.0f, -3.0f), Vector3f(1.0f, 2.0f, 3.0f));
    const Matrix4f entity = Matrix4f::TranslationMatrix(10.0f, 0.0f, 0.0f);
    const AABox3f world = asset.Transformed(entity);
    ASSERT_TRUE(world.IsValid());
    const Vector3f center = world.Center();
    const Vector3f extent = world.Extents();

    GpuModel::Part partA;
    partA.mesh        = Mesh::takeSortIdentity();
    partA.skinned     = true;
    GpuModel::Part partB;
    partB.mesh        = Mesh::takeSortIdentity();
    partB.skinned     = true;
    std::vector<GpuModel::Part> opaque;
    opaque.push_back(std::move(partA));
    opaque.push_back(std::move(partB));
    const GpuModel model(std::move(opaque), std::vector<GpuModel::Part>{});
    GpuResourceCache gpu(nullptr);
    SkinnedInstanceBuffer baked;
    ASSERT_TRUE(submitSkinnedModelOpaque(baked, gpu, model, &pose, entity, entity, StaticPopulation::OpaqueModels, true, asset));
    const MeshCullAabb* bakedAabbs = baked.cullAabbs();
    ASSERT_EQ(baked.cullAabbCount(), 2u);
    ASSERT_NE(bakedAabbs, nullptr);
    for (uint32_t i = 0; i < 2u; ++i)
    {
        EXPECT_FLOAT_EQ(bakedAabbs[i].center[0], center.x);
        EXPECT_FLOAT_EQ(bakedAabbs[i].center[1], center.y);
        EXPECT_FLOAT_EQ(bakedAabbs[i].center[2], center.z);
        EXPECT_FLOAT_EQ(bakedAabbs[i].extent[0], extent.x);
        EXPECT_FLOAT_EQ(bakedAabbs[i].extent[1], extent.y);
        EXPECT_FLOAT_EQ(bakedAabbs[i].extent[2], extent.z);
        EXPECT_EQ(bakedAabbs[i].flags & kCullAlwaysVisible, 0u);
        EXPECT_EQ(bakedAabbs[i].flags & kCullCastShadow, kCullCastShadow);
    }

    SkinnedInstanceBuffer buffer;
    SkinnedPartSubmit inside = partOf(&meshA, &pose, 1, StaticPopulation::OpaqueModels, true, false);
    inside.worldAabb = AABox3f(Vector3f(-0.25f, -0.25f, 7.5f), Vector3f(0.25f, 0.25f, 8.5f));
    SkinnedPartSubmit always = partOf(&meshA, &pose, 1, StaticPopulation::OpaqueModels, false, false);
    SkinnedPartSubmit farPart = partOf(&meshB, &pose, 1, StaticPopulation::OpaqueModels, true, false);
    farPart.worldAabb = AABox3f(Vector3f(-0.25f, -0.25f, 79.5f), Vector3f(0.25f, 0.25f, 80.5f));
    ASSERT_TRUE(buffer.submit(inside));
    ASSERT_TRUE(buffer.submit(always));
    ASSERT_TRUE(buffer.submit(farPart));

    const MeshCullAabb* aabbs = buffer.cullAabbs();
    ASSERT_EQ(buffer.cullAabbCount(), 3u);
    ASSERT_NE(aabbs, nullptr);
    EXPECT_EQ(aabbs[1].flags & kCullAlwaysVisible, kCullAlwaysVisible);
    EXPECT_EQ(aabbs[1].flags & kCullCastShadow, 0u);
    EXPECT_FLOAT_EQ(aabbs[1].center[0], 0.0f);
    EXPECT_FLOAT_EQ(aabbs[1].center[1], 0.0f);
    EXPECT_FLOAT_EQ(aabbs[1].center[2], 0.0f);
    EXPECT_FLOAT_EQ(aabbs[1].extent[0], 0.0f);
    EXPECT_EQ(aabbs[1].depthBatch, SkinnedInstanceBuffer::kNoDepthBatch);
    EXPECT_EQ(aabbs[0].gbufferBatch, aabbs[1].gbufferBatch);
    EXPECT_NE(aabbs[0].depthBatch, SkinnedInstanceBuffer::kNoDepthBatch);
    EXPECT_NE(aabbs[2].depthBatch, SkinnedInstanceBuffer::kNoDepthBatch);

    Camera3D camera;
    camera.SetLens(1.0f, 1.0f, 0.1f, 40.0f);
    camera.LookAt(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));
    const Frustum3f frustum(camera.GetCullViewProj());
    EXPECT_TRUE(frustum.Intersects(inside.worldAabb));
    EXPECT_FALSE(frustum.Intersects(farPart.worldAabb));
    EXPECT_EQ(cullAabbVisible(aabbs[0], frustum.Planes.data()), frustum.Intersects(inside.worldAabb));
    EXPECT_EQ(cullAabbVisible(aabbs[2], frustum.Planes.data()), frustum.Intersects(farPart.worldAabb));
    EXPECT_TRUE(cullAabbVisible(aabbs[1], frustum.Planes.data()));
    EXPECT_EQ(buffer.depthBatchCount(), 2u);
}

TEST(SkinnedInstance, CullStaysDirectWithoutADevice)
{
    SkinnedInstanceBuffer buffer;
    EXPECT_FALSE(buffer.cullReady());
    EXPECT_FALSE(buffer.dispatchCull(nullptr, nullptr, 0));
}

TEST(SkinnedInstance, CullShaderKeepsTheStaticLayout)
{
    EXPECT_EQ(sizeof(MeshCullAabb), 48u);
    EXPECT_EQ(sizeof(GpuBatchRecord), 16u);
    EXPECT_EQ(sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), 20u);
    EXPECT_EQ(SkinnedInstanceCull::kStride, kMaxSkinnedParts);
    EXPECT_NE(SkinnedInstanceCull::kStride, kMaxStaticItems);

    ComPtr<ID3DBlob> compact;
    ComPtr<ID3DBlob> args;
    ASSERT_TRUE(compileShaderFromContent("shaders/InstanceCull.hlsl", "CSCompact", "cs_5_0", compact));
    ASSERT_TRUE(compileShaderFromContent("shaders/InstanceCull.hlsl", "CSArgs", "cs_5_0", args));
    UINT bytes = 0;
    ASSERT_TRUE(cbufferBytes(compact.Get(), "CullConstants", bytes));
    EXPECT_EQ(bytes, 128u);
    UINT aabbs = 0;
    UINT batches = 0;
    UINT indices = 0;
    UINT counters = 0;
    UINT argReg = 0;
    ASSERT_TRUE(bindPoint(compact.Get(), "gAabbs", aabbs));
    ASSERT_TRUE(bindPoint(compact.Get(), "gBatches", batches));
    ASSERT_TRUE(bindPoint(compact.Get(), "gIndices", indices));
    ASSERT_TRUE(bindPoint(compact.Get(), "gCounters", counters));
    // CSCompact never writes arguments, so FXC drops gArgs from that entry point.
    ASSERT_TRUE(bindPoint(args.Get(), "gArgs", argReg));
    EXPECT_EQ(aabbs, 0u);
    EXPECT_EQ(batches, 1u);
    EXPECT_EQ(indices, 0u);
    EXPECT_EQ(counters, 1u);
    EXPECT_EQ(argReg, 2u);
    EXPECT_TRUE(disassemblyHas(compact.Get(), "dcl_resource_structured t0, 48"));
    EXPECT_TRUE(disassemblyHas(args.Get(), "dcl_uav_structured u2, 4"));
}

TEST(SkinnedInstance, GpuCullMatchesTheFrustumOnItsOwnBuffers)
{
    ComPtr<ID3D12Device> device = tryCreateWarpDevice();
    if (!device)
        GTEST_SKIP() << "no WARP device at feature level 11_0";

    SkinnedInstanceBuffer buffer;
    ASSERT_TRUE(buffer.create(device.Get()));
    ASSERT_TRUE(buffer.isValid());
    ASSERT_TRUE(buffer.cullReady());

    DrawList list;
    ASSERT_TRUE(list.create(device.Get()));
    ASSERT_TRUE(list.cullReady());
    EXPECT_NE(buffer.cullArgumentResource(0), list.argumentResource(0));
    EXPECT_NE(buffer.cullIndexResource(0), list.cullIndexResource(0));
    ASSERT_NE(buffer.cullIndexResource(0), nullptr);
    EXPECT_EQ(buffer.cullIndexResource(0)->GetDesc().Width, static_cast<UINT64>(kMaxStaticViews) * kMaxSkinnedParts * sizeof(uint32_t));
    EXPECT_EQ(buffer.cullArgumentResource(0)->GetDesc().Width, static_cast<UINT64>(kMaxStaticViews) * SkinnedInstanceCull::kStride * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));

    Mesh meshA = Mesh::takeSortIdentity();
    Mesh meshB = Mesh::takeSortIdentity();
    AnimPose pose{};
    pose.boneCount = 1;
    buffer.beginFrame(0);
    SkinnedPartSubmit inside = partOf(&meshA, &pose, 1, StaticPopulation::OpaqueModels, true, false);
    inside.worldAabb = AABox3f(Vector3f(-0.25f, -0.25f, 7.5f), Vector3f(0.25f, 0.25f, 8.5f));
    SkinnedPartSubmit always = partOf(&meshA, &pose, 1, StaticPopulation::OpaqueModels, false, false);
    SkinnedPartSubmit farPart = partOf(&meshB, &pose, 1, StaticPopulation::OpaqueModels, true, false);
    farPart.worldAabb = AABox3f(Vector3f(-0.25f, -0.25f, 79.5f), Vector3f(0.25f, 0.25f, 80.5f));
    ASSERT_TRUE(buffer.submit(inside));
    ASSERT_TRUE(buffer.submit(always));
    ASSERT_TRUE(buffer.submit(farPart));

    Camera3D camera;
    camera.SetLens(1.0f, 1.0f, 0.1f, 40.0f);
    camera.LookAt(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));
    const Frustum3f frustum(camera.GetCullViewProj());
    CullViewDesc views[2]{};
    views[0].viewIndex = 0;
    views[1].viewIndex = 1;
    views[1].useDepthBatch = 1;
    for (int plane = 0; plane < Frustum3f::Count; ++plane)
    {
        const Plane4f& p = frustum.Planes[static_cast<size_t>(plane)];
        views[0].planes[plane][0] = p.x;
        views[0].planes[plane][1] = p.y;
        views[0].planes[plane][2] = p.z;
        views[0].planes[plane][3] = p.w;
        views[1].planes[plane][0] = p.x;
        views[1].planes[plane][1] = p.y;
        views[1].planes[plane][2] = p.z;
        views[1].planes[plane][3] = p.w;
    }

    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))))
        GTEST_SKIP() << "CreateCommandQueue failed";
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmd))));
    ASSERT_TRUE(buffer.dispatchCull(cmd.Get(), views, 2));

    ID3D12Resource* args = buffer.cullArgumentResource(0);
    ID3D12Resource* indices = buffer.cullIndexResource(0);
    ASSERT_NE(args, nullptr);
    ASSERT_NE(indices, nullptr);
    const UINT64 argBytes = args->GetDesc().Width;
    const UINT64 indexBytes = indices->GetDesc().Width;

    auto makeReadback = [&](UINT64 bytes) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width            = bytes;
        desc.Height           = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.SampleDesc       = { 1, 0 };
        desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> res;
        EXPECT_TRUE(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&res))));
        return res;
    };
    ComPtr<ID3D12Resource> argRead = makeReadback(argBytes);
    ComPtr<ID3D12Resource> indexRead = makeReadback(indexBytes);
    ASSERT_TRUE(argRead);
    ASSERT_TRUE(indexRead);

    auto transition = [](ID3D12GraphicsCommandList* listCmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter  = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        listCmd->ResourceBarrier(1, &barrier);
    };
    transition(cmd.Get(), args, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(cmd.Get(), indices, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd->CopyBufferRegion(argRead.Get(), 0, args, 0, argBytes);
    cmd->CopyBufferRegion(indexRead.Get(), 0, indices, 0, indexBytes);
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

    std::vector<uint8_t> argCopy(static_cast<size_t>(argBytes));
    std::vector<uint8_t> indexCopy(static_cast<size_t>(indexBytes));
    void* argMap = nullptr;
    void* indexMap = nullptr;
    ASSERT_TRUE(SUCCEEDED(argRead->Map(0, nullptr, &argMap)));
    ASSERT_TRUE(SUCCEEDED(indexRead->Map(0, nullptr, &indexMap)));
    std::memcpy(argCopy.data(), argMap, argCopy.size());
    std::memcpy(indexCopy.data(), indexMap, indexCopy.size());
    argRead->Unmap(0, nullptr);
    indexRead->Unmap(0, nullptr);

    auto readArg = [&](uint32_t view, uint32_t batch) {
        DrawIndexedArgs out{};
        const size_t offset = (static_cast<size_t>(view) * SkinnedInstanceCull::kStride + batch) * sizeof(DrawIndexedArgs);
        std::memcpy(&out, argCopy.data() + offset, sizeof(out));
        return out;
    };
    auto readIndex = [&](uint32_t view, uint32_t element) {
        uint32_t out = 0;
        const size_t offset = (static_cast<size_t>(view) * SkinnedInstanceCull::kStride + element) * sizeof(uint32_t);
        std::memcpy(&out, indexCopy.data() + offset, sizeof(out));
        return out;
    };

    const GBufferBatch* batchA = nullptr;
    const GBufferBatch* batchB = nullptr;
    uint32_t indexA = 0;
    uint32_t indexB = 0;
    for (uint32_t i = 0; i < buffer.gbufferBatchCount(); ++i)
    {
        if (buffer.gbufferBatch(i).mesh == &meshA)
        {
            batchA = &buffer.gbufferBatch(i);
            indexA = i;
        }
        if (buffer.gbufferBatch(i).mesh == &meshB)
        {
            batchB = &buffer.gbufferBatch(i);
            indexB = i;
        }
    }
    ASSERT_NE(batchA, nullptr);
    ASSERT_NE(batchB, nullptr);
    const DrawIndexedArgs gbA = readArg(0, indexA);
    const DrawIndexedArgs gbB = readArg(0, indexB);
    EXPECT_EQ(gbA.instanceCount, 2u);
    EXPECT_EQ(gbA.startInstanceLocation, 0u);
    EXPECT_EQ(gbA.startIndexLocation, 0u);
    EXPECT_EQ(gbA.baseVertexLocation, 0);
    EXPECT_EQ(gbA.indexCountPerInstance, meshA.indexCount());
    EXPECT_EQ(gbB.instanceCount, 0u);
    EXPECT_EQ(gbB.startInstanceLocation, 0u);
    EXPECT_EQ(gbB.indexCountPerInstance, meshB.indexCount());
    if (batchB->itemBegin != 0)
        EXPECT_NE(gbB.startInstanceLocation, batchB->itemBegin);
    const uint32_t i0 = readIndex(0, batchA->itemBegin);
    const uint32_t i1 = readIndex(0, batchA->itemBegin + 1u);
    EXPECT_TRUE((i0 == 0u && i1 == 1u) || (i0 == 1u && i1 == 0u));

    const DepthBatch* depthA = nullptr;
    const DepthBatch* depthB = nullptr;
    uint32_t depthIndexA = 0;
    uint32_t depthIndexB = 0;
    for (uint32_t i = 0; i < buffer.depthBatchCount(); ++i)
    {
        if (buffer.depthBatch(i).mesh == &meshA)
        {
            depthA = &buffer.depthBatch(i);
            depthIndexA = i;
        }
        if (buffer.depthBatch(i).mesh == &meshB)
        {
            depthB = &buffer.depthBatch(i);
            depthIndexB = i;
        }
    }
    ASSERT_NE(depthA, nullptr);
    ASSERT_NE(depthB, nullptr);
    const DrawIndexedArgs dA = readArg(1, depthIndexA);
    const DrawIndexedArgs dB = readArg(1, depthIndexB);
    EXPECT_EQ(dA.instanceCount, 1u);
    EXPECT_EQ(dA.startInstanceLocation, 0u);
    EXPECT_EQ(dA.indexCountPerInstance, meshA.indexCount());
    EXPECT_EQ(readIndex(1, depthA->itemBegin), 0u);
    EXPECT_EQ(dB.instanceCount, 0u);
    EXPECT_EQ(dB.startInstanceLocation, 0u);
    EXPECT_EQ(dB.indexCountPerInstance, meshB.indexCount());
}
