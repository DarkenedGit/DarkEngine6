#include "Render/SkinnedInstanceBuffer.h"

#include "Animation/Pose.h"
#include "Assets/Material.h"
#include "Core/Log.h"
#include "Render/DepthState.h"
#include "Render/GpuMaterial.h"
#include "Render/GpuResourceCache.h"
#include "Render/MaterialSurface.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"

#include <algorithm>
#include <cstring>
#include <d3d12.h>
#include <numeric>

namespace Dark
{

    namespace
    {

        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        void logOnce(bool& logged, const char* message)
        {
            if (logged)
                return;
            DE_LOG_WARN(LogCategory::Render, "{}", message);
            logged = true;
        }

        ComPtr<ID3D12Resource> CreateUpload(ID3D12Device* device, UINT64 size, const char* what)
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
            if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)), what))
                return nullptr;
            return res;
        }

        bool serializeRoot(const D3D12_ROOT_SIGNATURE_DESC& desc, const char* label, ComPtr<ID3DBlob>& blob)
        {
            ComPtr<ID3DBlob> err;
            if (FailedHr(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err), label))
            {
                if (err)
                    DE_LOG_ERROR(LogCategory::Render, "{}: {}", label, static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }
            return true;
        }

        void fillIdentity(float m[16])
        {
            std::memset(m, 0, 16 * sizeof(float));
            m[0] = m[5] = m[10] = m[15] = 1.0f;
        }

        BonePaletteCB copyPalette(const AnimPose& pose)
        {
            BonePaletteCB cb{};
            const uint32_t n = pose.boneCount > AnimPose::kMaxBones ? AnimPose::kMaxBones : pose.boneCount;
            for (uint32_t i = 0; i < AnimPose::kMaxBones; ++i)
            {
                if (i < n)
                {
                    std::memcpy(cb.curr[i], pose.palette[i].m_afEntry, sizeof(cb.curr[i]));
                    std::memcpy(cb.prev[i], pose.prevPalette[i].m_afEntry, sizeof(cb.prev[i]));
                }
                else
                {
                    fillIdentity(cb.curr[i]);
                    fillIdentity(cb.prev[i]);
                }
            }
            return cb;
        }

        void fillBatchScalars(GpuResourceCache& gpu, AssetID material, MeshBatchConstants& cb)
        {
            MeshGBufferConstants surface{};
            if (const AssetRef<Material> mat = gpu.cpuMaterial(material))
                applyMaterialSurface(*mat, surface);
            else
            {
                surface.roughness     = 1.0f;
                surface.metallic      = 0.0f;
                surface.ao            = 1.0f;
                surface.normalScale   = 1.0f;
                surface.alphaCutoff   = 0.5f;
                surface.alphaModeMask = 0.0f;
            }
            cb.roughness     = surface.roughness;
            cb.metallic      = surface.metallic;
            cb.ao            = surface.ao;
            cb.normalScale   = surface.normalScale;
            cb.alphaCutoff   = surface.alphaCutoff;
            cb.alphaModeMask = surface.alphaModeMask;
        }

    } // namespace

    static_assert(sizeof(SkinnedInstanceGpu) == 160u, "skinned instance");
    static_assert(sizeof(BonePaletteCB) == 8192u, "palette");
    static_assert(sizeof(BonePaletteCB) == 4u * 2048u, "palette chunks");
    static_assert(40u + 1u + 2u + 2u + 2u == 47u, "skinned gbuffer root");
    static_assert(20u + 2u + 2u + 2u == 26u, "skinned depth root");
    static_assert(SkinnedInstanceBuffer::kFrameSlots == Renderer::kFrameCount, "skinned slots");

    struct SkinnedInstanceBuffer::Gpu
    {
        ComPtr<ID3D12Resource> instances;
        ComPtr<ID3D12Resource> palettes;
        ComPtr<ID3D12Resource> indices;
        uint8_t*               instancesMapped = nullptr;
        uint8_t*               palettesMapped  = nullptr;
        uint8_t*               indicesMapped   = nullptr;

        ComPtr<ID3D12RootSignature> gbRoot;
        ComPtr<ID3D12PipelineState> gbPso;
        ComPtr<ID3D12RootSignature> depthRoot;
        ComPtr<ID3D12PipelineState> depthPso;

        bool createPipelines(ID3D12Device* device);
        void bindGBuffer(ID3D12GraphicsCommandList* cmd) const;
        void setGBufferConstants(ID3D12GraphicsCommandList* cmd, const MeshBatchConstants& constants) const;
        void setGBufferBuffers(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS instanceVa, D3D12_GPU_VIRTUAL_ADDRESS indexVa, D3D12_GPU_VIRTUAL_ADDRESS paletteVa) const;
        void bindDepth(ID3D12GraphicsCommandList* cmd) const;
        void setDepthConstants(ID3D12GraphicsCommandList* cmd, const MeshDepthBatchConstants& constants) const;
        void setDepthBuffers(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS instanceVa, D3D12_GPU_VIRTUAL_ADDRESS indexVa, D3D12_GPU_VIRTUAL_ADDRESS paletteVa) const;
    };

    bool SkinnedInstanceBuffer::Gpu::createPipelines(ID3D12Device* device)
    {
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = GpuMaterial::kMapSrvCount;
        srvRange.BaseShaderRegister                = 0;
        srvRange.RegisterSpace                     = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER gbParams[5]{};
        gbParams[SkinnedInstanceBuffer::kGbConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        gbParams[SkinnedInstanceBuffer::kGbConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        gbParams[SkinnedInstanceBuffer::kGbConstants].Constants.ShaderRegister = 0;
        gbParams[SkinnedInstanceBuffer::kGbConstants].Constants.RegisterSpace  = 0;
        gbParams[SkinnedInstanceBuffer::kGbConstants].Constants.Num32BitValues = static_cast<UINT>(sizeof(MeshBatchConstants) / 4);

        gbParams[SkinnedInstanceBuffer::kGbMaterial].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        gbParams[SkinnedInstanceBuffer::kGbMaterial].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        gbParams[SkinnedInstanceBuffer::kGbMaterial].DescriptorTable.NumDescriptorRanges = 1;
        gbParams[SkinnedInstanceBuffer::kGbMaterial].DescriptorTable.pDescriptorRanges   = &srvRange;

        gbParams[SkinnedInstanceBuffer::kGbInstances].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        gbParams[SkinnedInstanceBuffer::kGbInstances].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        gbParams[SkinnedInstanceBuffer::kGbInstances].Descriptor.ShaderRegister = 4;
        gbParams[SkinnedInstanceBuffer::kGbInstances].Descriptor.RegisterSpace  = 0;

        gbParams[SkinnedInstanceBuffer::kGbIndices].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        gbParams[SkinnedInstanceBuffer::kGbIndices].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        gbParams[SkinnedInstanceBuffer::kGbIndices].Descriptor.ShaderRegister = 5;
        gbParams[SkinnedInstanceBuffer::kGbIndices].Descriptor.RegisterSpace  = 0;

        gbParams[SkinnedInstanceBuffer::kGbPalettes].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        gbParams[SkinnedInstanceBuffer::kGbPalettes].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        gbParams[SkinnedInstanceBuffer::kGbPalettes].Descriptor.ShaderRegister = 6;
        gbParams[SkinnedInstanceBuffer::kGbPalettes].Descriptor.RegisterSpace  = 0;

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.MaxLOD           = D3D12_FLOAT32_MAX;
        samp.ShaderRegister   = 0;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC gbDesc{};
        gbDesc.NumParameters     = 5;
        gbDesc.pParameters       = gbParams;
        gbDesc.NumStaticSamplers = 1;
        gbDesc.pStaticSamplers   = &samp;
        gbDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rsBlob;
        if (!serializeRoot(gbDesc, "D3D12SerializeRootSignature (skinned gbuffer)", rsBlob))
            return false;
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&gbRoot)), "CreateRootSignature (skinned gbuffer)"))
            return false;

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent("shaders/SkinnedMeshGBufferInstanced.hlsl", "VSMain", "vs_5_0", vs)
            || !compileShaderFromContent("shaders/SkinnedMeshGBufferInstanced.hlsl", "PSMain", "ps_5_0", ps))
            return false;

        D3D12_INPUT_ELEMENT_DESC colorLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC gbPsoDesc{};
        gbPsoDesc.pRootSignature                                   = gbRoot.Get();
        gbPsoDesc.VS                                               = { vs->GetBufferPointer(), vs->GetBufferSize() };
        gbPsoDesc.PS                                               = { ps->GetBufferPointer(), ps->GetBufferSize() };
        gbPsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        gbPsoDesc.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        gbPsoDesc.BlendState.RenderTarget[2].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        gbPsoDesc.BlendState.RenderTarget[3].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        gbPsoDesc.SampleMask                                       = UINT_MAX;
        gbPsoDesc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
        gbPsoDesc.RasterizerState.CullMode                         = D3D12_CULL_MODE_BACK;
        gbPsoDesc.RasterizerState.FrontCounterClockwise            = TRUE;
        gbPsoDesc.RasterizerState.DepthClipEnable                  = TRUE;
        gbPsoDesc.DepthStencilState.DepthEnable                    = TRUE;
        gbPsoDesc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;
        gbPsoDesc.DepthStencilState.DepthFunc                      = sceneDepthFunc();
        gbPsoDesc.DepthStencilState.StencilEnable                  = FALSE;
        gbPsoDesc.InputLayout                                      = { colorLayout, _countof(colorLayout) };
        gbPsoDesc.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        gbPsoDesc.NumRenderTargets                                 = 4;
        gbPsoDesc.RTVFormats[0]                                    = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        gbPsoDesc.RTVFormats[1]                                    = DXGI_FORMAT_R8G8B8A8_UNORM;
        gbPsoDesc.RTVFormats[2]                                    = DXGI_FORMAT_R16G16_FLOAT;
        gbPsoDesc.RTVFormats[3]                                    = DXGI_FORMAT_R8_UNORM;
        gbPsoDesc.DSVFormat                                        = DXGI_FORMAT_D32_FLOAT;
        gbPsoDesc.SampleDesc                                       = { 1, 0 };

        if (FailedHr(device->CreateGraphicsPipelineState(&gbPsoDesc, IID_PPV_ARGS(&gbPso)), "CreateGraphicsPipelineState (skinned gbuffer)"))
            return false;

        D3D12_ROOT_PARAMETER depthParams[4]{};
        depthParams[SkinnedInstanceBuffer::kDepthConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        depthParams[SkinnedInstanceBuffer::kDepthConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;
        depthParams[SkinnedInstanceBuffer::kDepthConstants].Constants.ShaderRegister = 0;
        depthParams[SkinnedInstanceBuffer::kDepthConstants].Constants.RegisterSpace  = 0;
        depthParams[SkinnedInstanceBuffer::kDepthConstants].Constants.Num32BitValues = static_cast<UINT>(sizeof(MeshDepthBatchConstants) / 4);

        depthParams[SkinnedInstanceBuffer::kDepthInstances].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        depthParams[SkinnedInstanceBuffer::kDepthInstances].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        depthParams[SkinnedInstanceBuffer::kDepthInstances].Descriptor.ShaderRegister = 4;
        depthParams[SkinnedInstanceBuffer::kDepthInstances].Descriptor.RegisterSpace  = 0;

        depthParams[SkinnedInstanceBuffer::kDepthIndices].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        depthParams[SkinnedInstanceBuffer::kDepthIndices].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        depthParams[SkinnedInstanceBuffer::kDepthIndices].Descriptor.ShaderRegister = 5;
        depthParams[SkinnedInstanceBuffer::kDepthIndices].Descriptor.RegisterSpace  = 0;

        depthParams[SkinnedInstanceBuffer::kDepthPalettes].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        depthParams[SkinnedInstanceBuffer::kDepthPalettes].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        depthParams[SkinnedInstanceBuffer::kDepthPalettes].Descriptor.ShaderRegister = 6;
        depthParams[SkinnedInstanceBuffer::kDepthPalettes].Descriptor.RegisterSpace  = 0;

        D3D12_ROOT_SIGNATURE_DESC depthDesc{};
        depthDesc.NumParameters = 4;
        depthDesc.pParameters   = depthParams;
        depthDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        rsBlob.Reset();
        if (!serializeRoot(depthDesc, "D3D12SerializeRootSignature (skinned depth)", rsBlob))
            return false;
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&depthRoot)), "CreateRootSignature (skinned depth)"))
            return false;

        ComPtr<ID3DBlob> depthVs;
        if (!compileShaderFromContent("shaders/SkinnedShadowDepthInstanced.hlsl", "VSMain", "vs_5_0", depthVs))
            return false;

        D3D12_INPUT_ELEMENT_DESC shadowLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC depthPsoDesc{};
        depthPsoDesc.pRootSignature                        = depthRoot.Get();
        depthPsoDesc.VS                                    = { depthVs->GetBufferPointer(), depthVs->GetBufferSize() };
        depthPsoDesc.PS                                    = { nullptr, 0 };
        depthPsoDesc.SampleMask                            = UINT_MAX;
        depthPsoDesc.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
        depthPsoDesc.RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
        depthPsoDesc.RasterizerState.FrontCounterClockwise = TRUE;
        depthPsoDesc.RasterizerState.DepthClipEnable       = TRUE;
        depthPsoDesc.RasterizerState.DepthBias             = -4000;
        depthPsoDesc.RasterizerState.SlopeScaledDepthBias  = -2.5f;
        depthPsoDesc.RasterizerState.DepthBiasClamp        = 0.0f;
        depthPsoDesc.DepthStencilState.DepthEnable         = TRUE;
        depthPsoDesc.DepthStencilState.DepthWriteMask      = D3D12_DEPTH_WRITE_MASK_ALL;
        depthPsoDesc.DepthStencilState.DepthFunc           = shadowDepthFunc();
        depthPsoDesc.DepthStencilState.StencilEnable       = FALSE;
        depthPsoDesc.InputLayout                           = { shadowLayout, _countof(shadowLayout) };
        depthPsoDesc.PrimitiveTopologyType                 = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        depthPsoDesc.NumRenderTargets                      = 0;
        depthPsoDesc.DSVFormat                             = DXGI_FORMAT_D32_FLOAT;
        depthPsoDesc.SampleDesc                            = { 1, 0 };

        if (FailedHr(device->CreateGraphicsPipelineState(&depthPsoDesc, IID_PPV_ARGS(&depthPso)), "CreateGraphicsPipelineState (skinned depth)"))
            return false;
        return true;
    }

    void SkinnedInstanceBuffer::Gpu::bindGBuffer(ID3D12GraphicsCommandList* cmd) const
    {
        if (!cmd || !gbPso)
            return;
        cmd->SetGraphicsRootSignature(gbRoot.Get());
        cmd->SetPipelineState(gbPso.Get());
    }

    void SkinnedInstanceBuffer::Gpu::setGBufferConstants(ID3D12GraphicsCommandList* cmd, const MeshBatchConstants& constants) const
    {
        if (!cmd || !gbRoot)
            return;
        cmd->SetGraphicsRoot32BitConstants(SkinnedInstanceBuffer::kGbConstants, static_cast<UINT>(sizeof(MeshBatchConstants) / 4), &constants, 0);
    }

    void SkinnedInstanceBuffer::Gpu::setGBufferBuffers(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS instanceVa, D3D12_GPU_VIRTUAL_ADDRESS indexVa,
                                                       D3D12_GPU_VIRTUAL_ADDRESS paletteVa) const
    {
        if (!cmd || !gbRoot)
            return;
        cmd->SetGraphicsRootShaderResourceView(SkinnedInstanceBuffer::kGbInstances, instanceVa);
        cmd->SetGraphicsRootShaderResourceView(SkinnedInstanceBuffer::kGbIndices, indexVa);
        cmd->SetGraphicsRootShaderResourceView(SkinnedInstanceBuffer::kGbPalettes, paletteVa);
    }

    void SkinnedInstanceBuffer::Gpu::bindDepth(ID3D12GraphicsCommandList* cmd) const
    {
        if (!cmd || !depthPso)
            return;
        cmd->SetGraphicsRootSignature(depthRoot.Get());
        cmd->SetPipelineState(depthPso.Get());
    }

    void SkinnedInstanceBuffer::Gpu::setDepthConstants(ID3D12GraphicsCommandList* cmd, const MeshDepthBatchConstants& constants) const
    {
        if (!cmd || !depthRoot)
            return;
        cmd->SetGraphicsRoot32BitConstants(SkinnedInstanceBuffer::kDepthConstants, static_cast<UINT>(sizeof(MeshDepthBatchConstants) / 4), &constants, 0);
    }

    void SkinnedInstanceBuffer::Gpu::setDepthBuffers(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS instanceVa, D3D12_GPU_VIRTUAL_ADDRESS indexVa,
                                                     D3D12_GPU_VIRTUAL_ADDRESS paletteVa) const
    {
        if (!cmd || !depthRoot)
            return;
        cmd->SetGraphicsRootShaderResourceView(SkinnedInstanceBuffer::kDepthInstances, instanceVa);
        cmd->SetGraphicsRootShaderResourceView(SkinnedInstanceBuffer::kDepthIndices, indexVa);
        cmd->SetGraphicsRootShaderResourceView(SkinnedInstanceBuffer::kDepthPalettes, paletteVa);
    }

    SkinnedInstanceBuffer::SkinnedInstanceBuffer() = default;

    SkinnedInstanceBuffer::~SkinnedInstanceBuffer()
    {
        destroy();
    }

    void SkinnedInstanceBuffer::destroy()
    {
        if (!m_gpu)
            return;
        if (m_gpu->instances && m_gpu->instancesMapped)
            m_gpu->instances->Unmap(0, nullptr);
        if (m_gpu->palettes && m_gpu->palettesMapped)
            m_gpu->palettes->Unmap(0, nullptr);
        if (m_gpu->indices && m_gpu->indicesMapped)
            m_gpu->indices->Unmap(0, nullptr);
        m_gpu->instancesMapped = nullptr;
        m_gpu->palettesMapped  = nullptr;
        m_gpu->indicesMapped   = nullptr;
        m_gpu.reset();
    }

    bool SkinnedInstanceBuffer::create(ID3D12Device* device)
    {
        destroy();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "SkinnedInstanceBuffer::create: null device");
            return false;
        }

        m_gpu = std::make_unique<Gpu>();
        const UINT64 instanceBytes = static_cast<UINT64>(kFrameSlots) * kMaxSkinnedParts * sizeof(SkinnedInstanceGpu);
        const UINT64 paletteBytes  = static_cast<UINT64>(kFrameSlots) * kMaxSkinnedPalettes * sizeof(BonePaletteCB);
        const UINT64 indexBytes    = static_cast<UINT64>(kFrameSlots) * kMaxSkinnedParts * 2u * sizeof(uint32_t);
        m_gpu->instances = CreateUpload(device, instanceBytes, "CreateCommittedResource skinned instances");
        m_gpu->palettes  = CreateUpload(device, paletteBytes, "CreateCommittedResource skinned palettes");
        m_gpu->indices   = CreateUpload(device, indexBytes, "CreateCommittedResource skinned indices");
        if (!m_gpu->instances || !m_gpu->palettes || !m_gpu->indices)
        {
            destroy();
            DE_LOG_ERROR(LogCategory::Render, "SkinnedInstanceBuffer: skinned instancing unavailable");
            return false;
        }

        void* mapped = nullptr;
        if (FailedHr(m_gpu->instances->Map(0, nullptr, &mapped), "Map skinned instances"))
        {
            destroy();
            DE_LOG_ERROR(LogCategory::Render, "SkinnedInstanceBuffer: skinned instancing unavailable");
            return false;
        }
        m_gpu->instancesMapped = static_cast<uint8_t*>(mapped);
        mapped                 = nullptr;
        if (FailedHr(m_gpu->palettes->Map(0, nullptr, &mapped), "Map skinned palettes"))
        {
            destroy();
            DE_LOG_ERROR(LogCategory::Render, "SkinnedInstanceBuffer: skinned instancing unavailable");
            return false;
        }
        m_gpu->palettesMapped = static_cast<uint8_t*>(mapped);
        mapped                = nullptr;
        if (FailedHr(m_gpu->indices->Map(0, nullptr, &mapped), "Map skinned indices"))
        {
            destroy();
            DE_LOG_ERROR(LogCategory::Render, "SkinnedInstanceBuffer: skinned instancing unavailable");
            return false;
        }
        m_gpu->indicesMapped = static_cast<uint8_t*>(mapped);

        std::memset(m_gpu->instancesMapped, 0, static_cast<size_t>(instanceBytes));
        std::memset(m_gpu->palettesMapped, 0, static_cast<size_t>(paletteBytes));
        std::memset(m_gpu->indicesMapped, 0, static_cast<size_t>(indexBytes));
        m_gpu->instances->SetName(L"DE.Skinned.Instances");
        m_gpu->palettes->SetName(L"DE.Skinned.Palettes");
        m_gpu->indices->SetName(L"DE.Skinned.Indices");

        if (!m_gpu->createPipelines(device))
        {
            destroy();
            DE_LOG_ERROR(LogCategory::Render, "SkinnedInstanceBuffer: skinned instancing unavailable");
            return false;
        }

        DE_LOG_INFO(LogCategory::Render, "SkinnedInstanceBuffer: ready (gbuffer 47 DWORDs, depth 26 DWORDs, 256 palettes, 1024 parts)");
        return true;
    }

    bool SkinnedInstanceBuffer::isValid() const
    {
        return m_gpu && m_gpu->gbPso && m_gpu->depthPso && m_gpu->instancesMapped && m_gpu->palettesMapped && m_gpu->indicesMapped;
    }

    void SkinnedInstanceBuffer::beginFrame(uint32_t frameIndex)
    {
        m_frameIndex = frameIndex;
        m_parts.clear();
        m_poseKeys.clear();
        m_compiled = false;
        m_instances.clear();
        m_palettes.clear();
        m_gbufferIndices.clear();
        m_depthIndices.clear();
        m_gbuffer.clear();
        m_depth.clear();
    }

    uint32_t SkinnedInstanceBuffer::mark() const
    {
        return static_cast<uint32_t>(m_parts.size());
    }

    void SkinnedInstanceBuffer::rollback(uint32_t mark)
    {
        if (mark > m_parts.size())
            mark = static_cast<uint32_t>(m_parts.size());
        m_parts.resize(mark);
        rebuildPoseKeys();
        m_compiled = false;
    }

    void SkinnedInstanceBuffer::rebuildPoseKeys()
    {
        m_poseKeys.clear();
        for (const Part& part : m_parts)
        {
            bool known = false;
            for (const AnimPose* pose : m_poseKeys)
            {
                if (pose == part.pose)
                {
                    known = true;
                    break;
                }
            }
            if (!known)
                m_poseKeys.push_back(part.pose);
        }
    }

    bool SkinnedInstanceBuffer::submit(const SkinnedPartSubmit& part)
    {
        static bool loggedMissing = false;
        static bool loggedParts   = false;
        static bool loggedPalette = false;
        if (!part.mesh || !part.pose)
        {
            logOnce(loggedMissing, "SkinnedInstanceBuffer: part missing mesh or pose");
            return false;
        }
        if (m_parts.size() >= kMaxSkinnedParts)
        {
            logOnce(loggedParts, "SkinnedInstanceBuffer: skinned part cap reached");
            return false;
        }

        bool known = false;
        for (const AnimPose* pose : m_poseKeys)
        {
            if (pose == part.pose)
            {
                known = true;
                break;
            }
        }
        if (!known && m_poseKeys.size() >= kMaxSkinnedPalettes)
        {
            logOnce(loggedPalette, "SkinnedInstanceBuffer: skinned palette cap reached");
            return false;
        }

        Part stored;
        stored.mesh       = part.mesh;
        stored.material   = part.material;
        stored.world      = part.world;
        stored.prevWorld  = part.prevWorld;
        stored.population = part.population;
        stored.alphaMask  = part.alphaMask;
        stored.castShadow = part.castShadow;
        stored.pose       = part.pose;
        std::memcpy(stored.color, part.color, sizeof(stored.color));
        m_parts.push_back(stored);
        if (!known)
            m_poseKeys.push_back(part.pose);
        m_compiled = false;
        return true;
    }

    void SkinnedInstanceBuffer::compile() const
    {
        if (m_compiled)
            return;

        const auto partKey = [](const Part& part) {
            BatchKey key;
            key.population = static_cast<uint32_t>(part.population);
            key.meshKey    = part.mesh ? part.mesh->sortKey() : 0u;
            key.alphaMask  = part.alphaMask ? 1u : 0u;
            key.material   = part.material;
            return key;
        };

        m_instances.resize(m_parts.size());
        m_palettes.clear();
        std::vector<const AnimPose*> poses;
        poses.reserve(m_poseKeys.size());
        std::vector<uint32_t> paletteOf(m_parts.size(), 0u);
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_parts.size()); ++i)
        {
            const AnimPose* pose = m_parts[i].pose;
            uint32_t        slot = static_cast<uint32_t>(poses.size());
            for (uint32_t p = 0; p < static_cast<uint32_t>(poses.size()); ++p)
            {
                if (poses[p] == pose)
                {
                    slot = p;
                    break;
                }
            }
            if (slot == poses.size())
            {
                poses.push_back(pose);
                m_palettes.push_back(copyPalette(*pose));
            }
            paletteOf[i] = slot;

            SkinnedInstanceGpu& gpu = m_instances[i];
            std::memcpy(gpu.world, m_parts[i].world.m_afEntry, sizeof(gpu.world));
            std::memcpy(gpu.prevWorld, m_parts[i].prevWorld.m_afEntry, sizeof(gpu.prevWorld));
            std::memcpy(gpu.color, m_parts[i].color, sizeof(gpu.color));
            gpu.paletteIndex = slot;
            gpu.pad[0] = gpu.pad[1] = gpu.pad[2] = 0u;
        }

        std::vector<uint32_t> order(m_parts.size());
        std::iota(order.begin(), order.end(), 0u);
        std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
            return batchKeyLess(partKey(m_parts[a]), partKey(m_parts[b]));
        });

        m_gbuffer.clear();
        m_gbufferIndices.resize(order.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(order.size()); ++i)
        {
            const uint32_t submitIndex = order[i];
            m_gbufferIndices[i]        = submitIndex;
            const BatchKey key         = partKey(m_parts[submitIndex]);
            if (m_gbuffer.empty() || !batchKeyEqual(m_gbuffer.back().key, key))
            {
                GBufferBatch batch;
                batch.key       = key;
                batch.itemBegin = i;
                batch.itemCount = 1;
                batch.mesh      = m_parts[submitIndex].mesh;
                m_gbuffer.push_back(batch);
            }
            else
                m_gbuffer.back().itemCount += 1;
        }

        m_depth.clear();
        m_depthIndices.clear();
        for (uint32_t submitIndex : order)
        {
            const Part& part = m_parts[submitIndex];
            if (!part.castShadow)
                continue;
            const BatchKey key     = partKey(part);
            const bool     newSpan = m_depth.empty() || m_depth.back().population != key.population || m_depth.back().meshKey != key.meshKey;
            if (newSpan)
            {
                DepthBatch batch;
                batch.population = key.population;
                batch.meshKey    = key.meshKey;
                batch.itemBegin  = static_cast<uint32_t>(m_depthIndices.size());
                batch.itemCount  = 1;
                batch.mesh       = part.mesh;
                m_depth.push_back(batch);
            }
            else
                m_depth.back().itemCount += 1;
            m_depthIndices.push_back(submitIndex);
        }

        if (m_gpu && m_gpu->instancesMapped && m_gpu->palettesMapped && m_gpu->indicesMapped)
        {
            const uint32_t slot = m_frameIndex % kFrameSlots;
            if (!m_instances.empty())
            {
                auto* dst = reinterpret_cast<SkinnedInstanceGpu*>(m_gpu->instancesMapped) + static_cast<size_t>(slot) * kMaxSkinnedParts;
                std::memcpy(dst, m_instances.data(), m_instances.size() * sizeof(SkinnedInstanceGpu));
            }
            if (!m_palettes.empty())
            {
                auto* dst = reinterpret_cast<BonePaletteCB*>(m_gpu->palettesMapped) + static_cast<size_t>(slot) * kMaxSkinnedPalettes;
                std::memcpy(dst, m_palettes.data(), m_palettes.size() * sizeof(BonePaletteCB));
            }
            auto* indices = reinterpret_cast<uint32_t*>(m_gpu->indicesMapped) + static_cast<size_t>(slot) * kMaxSkinnedParts * 2u;
            if (!m_gbufferIndices.empty())
                std::memcpy(indices, m_gbufferIndices.data(), m_gbufferIndices.size() * sizeof(uint32_t));
            if (!m_depthIndices.empty())
                std::memcpy(indices + kMaxSkinnedParts, m_depthIndices.data(), m_depthIndices.size() * sizeof(uint32_t));
        }

        m_compiled = true;
    }

    uint32_t SkinnedInstanceBuffer::paletteCount() const
    {
        compile();
        return static_cast<uint32_t>(m_palettes.size());
    }

    uint32_t SkinnedInstanceBuffer::gbufferBatchCount() const
    {
        compile();
        return static_cast<uint32_t>(m_gbuffer.size());
    }

    uint32_t SkinnedInstanceBuffer::depthBatchCount() const
    {
        compile();
        return static_cast<uint32_t>(m_depth.size());
    }

    const GBufferBatch& SkinnedInstanceBuffer::gbufferBatch(uint32_t index) const
    {
        compile();
        static const GBufferBatch kEmpty{};
        if (index >= m_gbuffer.size())
            return kEmpty;
        return m_gbuffer[index];
    }

    const DepthBatch& SkinnedInstanceBuffer::depthBatch(uint32_t index) const
    {
        compile();
        static const DepthBatch kEmpty{};
        if (index >= m_depth.size())
            return kEmpty;
        return m_depth[index];
    }

    const SkinnedInstanceGpu* SkinnedInstanceBuffer::instances() const
    {
        compile();
        if (m_instances.empty())
            return nullptr;
        return m_instances.data();
    }

    const uint32_t* SkinnedInstanceBuffer::gbufferIndices() const
    {
        compile();
        if (m_gbufferIndices.empty())
            return nullptr;
        return m_gbufferIndices.data();
    }

    const uint32_t* SkinnedInstanceBuffer::depthIndices() const
    {
        compile();
        if (m_depthIndices.empty())
            return nullptr;
        return m_depthIndices.data();
    }

    const BonePaletteCB* SkinnedInstanceBuffer::uploadedPalettes(uint32_t frameSlot) const
    {
        compile();
        if (!m_gpu || !m_gpu->palettesMapped || frameSlot >= kFrameSlots)
            return nullptr;
        return reinterpret_cast<const BonePaletteCB*>(m_gpu->palettesMapped) + static_cast<size_t>(frameSlot) * kMaxSkinnedPalettes;
    }

    const SkinnedInstanceGpu* SkinnedInstanceBuffer::uploadedInstances(uint32_t frameSlot) const
    {
        compile();
        if (!m_gpu || !m_gpu->instancesMapped || frameSlot >= kFrameSlots)
            return nullptr;
        return reinterpret_cast<const SkinnedInstanceGpu*>(m_gpu->instancesMapped) + static_cast<size_t>(frameSlot) * kMaxSkinnedParts;
    }

    void SkinnedInstanceBuffer::recordGBuffer(ID3D12GraphicsCommandList* cmd, GpuResourceCache& gpu, StaticPopulation population, const float viewProj[16], const float prevViewProj[16]) const
    {
        if (!cmd || !isValid() || !viewProj || !prevViewProj)
            return;
        compile();
        const uint32_t populationId = static_cast<uint32_t>(population);
        bool           any          = false;
        for (const GBufferBatch& batch : m_gbuffer)
        {
            if (batch.key.population == populationId && batch.mesh && batch.mesh->valid() && batch.itemCount > 0)
            {
                any = true;
                break;
            }
        }
        if (!any)
            return;

        const uint32_t                slot       = m_frameIndex % kFrameSlots;
        const D3D12_GPU_VIRTUAL_ADDRESS instances = m_gpu->instances->GetGPUVirtualAddress() + static_cast<UINT64>(slot) * kMaxSkinnedParts * sizeof(SkinnedInstanceGpu);
        const D3D12_GPU_VIRTUAL_ADDRESS indices   = m_gpu->indices->GetGPUVirtualAddress() + static_cast<UINT64>(slot) * kMaxSkinnedParts * 2u * sizeof(uint32_t);
        const D3D12_GPU_VIRTUAL_ADDRESS palettes  = m_gpu->palettes->GetGPUVirtualAddress() + static_cast<UINT64>(slot) * kMaxSkinnedPalettes * sizeof(BonePaletteCB);
        m_gpu->bindGBuffer(cmd);
        m_gpu->setGBufferBuffers(cmd, instances, indices, palettes);
        for (const GBufferBatch& batch : m_gbuffer)
        {
            if (batch.key.population != populationId || !batch.mesh || !batch.mesh->valid() || batch.itemCount == 0)
                continue;
            MeshBatchConstants cb{};
            std::memcpy(cb.viewProj, viewProj, sizeof(cb.viewProj));
            std::memcpy(cb.prevViewProj, prevViewProj, sizeof(cb.prevViewProj));
            fillBatchScalars(gpu, batch.key.material, cb);
            cb.baseIndex = batch.itemBegin;
            cb.pad       = 0u;
            m_gpu->setGBufferConstants(cmd, cb);
            gpu.bindMaterial(cmd, batch.key.material, SkinnedInstanceBuffer::kGbMaterial);
            batch.mesh->drawInstanced(cmd, batch.itemCount);
        }
    }

    void SkinnedInstanceBuffer::recordDepth(ID3D12GraphicsCommandList* cmd, const float lightViewProj[16]) const
    {
        if (!cmd || !isValid() || !lightViewProj)
            return;
        compile();
        bool any = false;
        for (const DepthBatch& batch : m_depth)
        {
            if (batch.mesh && batch.mesh->valid() && batch.itemCount > 0)
            {
                any = true;
                break;
            }
        }
        if (!any)
            return;

        const uint32_t                  slot       = m_frameIndex % kFrameSlots;
        const D3D12_GPU_VIRTUAL_ADDRESS instances  = m_gpu->instances->GetGPUVirtualAddress() + static_cast<UINT64>(slot) * kMaxSkinnedParts * sizeof(SkinnedInstanceGpu);
        const D3D12_GPU_VIRTUAL_ADDRESS indices    = m_gpu->indices->GetGPUVirtualAddress() + (static_cast<UINT64>(slot) * kMaxSkinnedParts * 2u + kMaxSkinnedParts) * sizeof(uint32_t);
        const D3D12_GPU_VIRTUAL_ADDRESS palettes   = m_gpu->palettes->GetGPUVirtualAddress() + static_cast<UINT64>(slot) * kMaxSkinnedPalettes * sizeof(BonePaletteCB);
        m_gpu->bindDepth(cmd);
        m_gpu->setDepthBuffers(cmd, instances, indices, palettes);
        for (const DepthBatch& batch : m_depth)
        {
            if (!batch.mesh || !batch.mesh->valid() || batch.itemCount == 0)
                continue;
            MeshDepthBatchConstants cb{};
            std::memcpy(cb.lightViewProj, lightViewProj, sizeof(cb.lightViewProj));
            cb.baseIndex = batch.itemBegin;
            cb.pad[0] = cb.pad[1] = cb.pad[2] = 0u;
            m_gpu->setDepthConstants(cmd, cb);
            batch.mesh->drawInstanced(cmd, batch.itemCount);
        }
    }

} // namespace Dark
