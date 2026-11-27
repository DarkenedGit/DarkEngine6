#include "Render/InstancedMeshPipeline.h"
#include "Render/DepthState.h"
#include "Render/GpuMaterial.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"

#include <cstddef>

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

    } // namespace

    void InstancedMeshPipeline::reset()
    {
        m_gbRoot.Reset();
        m_gbPso.Reset();
        m_gbThermal.Reset();
        m_gbUv.Reset();
        m_gbSurfaceTag.Reset();
        m_thermal = false;
        m_ultraviolet = false;
        m_writeSurfaceTag = false;
        m_depthRoot.Reset();
        m_depthPso.Reset();
    }

    bool InstancedMeshPipeline::create(ID3D12Device* device)
    {
        reset();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "InstancedMeshPipeline::create: null device");
            return false;
        }

        // UV is t0 space1 so instances stay t4. The table is still one root DWORD.
        D3D12_DESCRIPTOR_RANGE mapRanges[2]{};
        fillGBufferMapRanges(mapRanges);

        D3D12_ROOT_PARAMETER gbParams[4]{};
        gbParams[kGbConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        gbParams[kGbConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        gbParams[kGbConstants].Constants.ShaderRegister = 0;
        gbParams[kGbConstants].Constants.RegisterSpace  = 0;
        gbParams[kGbConstants].Constants.Num32BitValues = static_cast<UINT>(sizeof(MeshBatchConstants) / 4);

        gbParams[kGbMaterial].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        gbParams[kGbMaterial].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        gbParams[kGbMaterial].DescriptorTable.NumDescriptorRanges = 2;
        gbParams[kGbMaterial].DescriptorTable.pDescriptorRanges   = mapRanges;

        gbParams[kGbInstances].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        gbParams[kGbInstances].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        gbParams[kGbInstances].Descriptor.ShaderRegister = 4;
        gbParams[kGbInstances].Descriptor.RegisterSpace  = 0;

        gbParams[kGbIndices].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        gbParams[kGbIndices].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        gbParams[kGbIndices].Descriptor.ShaderRegister = 5;
        gbParams[kGbIndices].Descriptor.RegisterSpace  = 0;

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.MaxLOD           = D3D12_FLOAT32_MAX;
        samp.ShaderRegister   = 0;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC gbDesc{};
        gbDesc.NumParameters     = 4;
        gbDesc.pParameters       = gbParams;
        gbDesc.NumStaticSamplers = 1;
        gbDesc.pStaticSamplers   = &samp;
        gbDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rsBlob;
        if (!serializeRoot(gbDesc, "D3D12SerializeRootSignature (instanced gbuffer)", rsBlob))
            return false;
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_gbRoot)),
                     "CreateRootSignature (instanced gbuffer)"))
        {
            reset();
            return false;
        }

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent("shaders/BasicMeshGBufferInstanced.hlsl", "VSMain", ShaderStage::Vertex, vs)
            || !compileShaderFromContent("shaders/BasicMeshGBufferInstanced.hlsl", "PSMain", ShaderStage::Pixel, ps))
        {
            reset();
            return false;
        }

        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC gbPso{};
        gbPso.pRootSignature                                    = m_gbRoot.Get();
        gbPso.VS                                                = { vs->GetBufferPointer(), vs->GetBufferSize() };
        gbPso.PS                                                = { ps->GetBufferPointer(), ps->GetBufferSize() };
        gbPso.BlendState.RenderTarget[0].RenderTargetWriteMask  = D3D12_COLOR_WRITE_ENABLE_ALL;
        gbPso.BlendState.RenderTarget[1].RenderTargetWriteMask  = D3D12_COLOR_WRITE_ENABLE_ALL;
        gbPso.BlendState.RenderTarget[2].RenderTargetWriteMask  = D3D12_COLOR_WRITE_ENABLE_ALL;
        gbPso.BlendState.RenderTarget[3].RenderTargetWriteMask  = D3D12_COLOR_WRITE_ENABLE_ALL;
        gbPso.SampleMask                                        = UINT_MAX;
        gbPso.RasterizerState.FillMode                          = D3D12_FILL_MODE_SOLID;
        gbPso.RasterizerState.CullMode                          = D3D12_CULL_MODE_BACK;
        gbPso.RasterizerState.FrontCounterClockwise             = TRUE;
        gbPso.RasterizerState.DepthClipEnable                   = TRUE;
        gbPso.DepthStencilState.DepthEnable                     = TRUE;
        gbPso.DepthStencilState.DepthWriteMask                  = D3D12_DEPTH_WRITE_MASK_ALL;
        gbPso.DepthStencilState.DepthFunc                       = sceneDepthFunc();
        gbPso.DepthStencilState.StencilEnable                   = FALSE;
        gbPso.InputLayout                                       = { inputLayout, _countof(inputLayout) };
        gbPso.PrimitiveTopologyType                             = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        gbPso.NumRenderTargets                                  = 4;
        gbPso.RTVFormats[0]                                     = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        gbPso.RTVFormats[1]                                     = DXGI_FORMAT_R8G8B8A8_UNORM;
        gbPso.RTVFormats[2]                                     = DXGI_FORMAT_R16G16_FLOAT;
        gbPso.RTVFormats[3]                                     = DXGI_FORMAT_R8_UNORM;
        gbPso.DSVFormat                                         = DXGI_FORMAT_D32_FLOAT;
        gbPso.SampleDesc                                        = { 1, 0 };

        if (FailedHr(device->CreateGraphicsPipelineState(&gbPso, IID_PPV_ARGS(&m_gbPso)), "CreateGraphicsPipelineState (instanced gbuffer)"))
        {
            reset();
            return false;
        }
        {
            D3D_SHADER_MACRO thermalMacros[3];
            makeThermalMacros("0.41", thermalMacros);
            ComPtr<ID3DBlob> thermalPs;
            if (!compileShaderFromContent("shaders/BasicMeshGBufferInstanced.hlsl", "PSMain", ShaderStage::Pixel, thermalPs, thermalMacros))
            {
                reset();
                return false;
            }
            D3D12_GRAPHICS_PIPELINE_STATE_DESC thermalDesc = gbPso;
            thermalDesc.PS = { thermalPs->GetBufferPointer(), thermalPs->GetBufferSize() };
            if (FailedHr(device->CreateGraphicsPipelineState(&thermalDesc, IID_PPV_ARGS(&m_gbThermal)), "CreateGraphicsPipelineState (instanced thermal)"))
            {
                reset();
                return false;
            }
            D3D_SHADER_MACRO uvMacros[3];
            makeUvMacros("0.00", uvMacros);
            ComPtr<ID3DBlob> uvPs;
            if (!compileShaderFromContent("shaders/BasicMeshGBufferInstanced.hlsl", "PSMain", ShaderStage::Pixel, uvPs, uvMacros))
            {
                reset();
                return false;
            }
            D3D12_GRAPHICS_PIPELINE_STATE_DESC uvDesc = gbPso;
            uvDesc.PS = { uvPs->GetBufferPointer(), uvPs->GetBufferSize() };
            if (FailedHr(device->CreateGraphicsPipelineState(&uvDesc, IID_PPV_ARGS(&m_gbUv)), "CreateGraphicsPipelineState (instanced ultraviolet)"))
            {
                reset();
                return false;
            }
            if (!createSurfaceTagPipelineState(device, gbPso, "shaders/BasicMeshGBufferInstanced.hlsl", "0", m_gbSurfaceTag,
                                               "CreateGraphicsPipelineState (instanced surface tag)"))
            {
                reset();
                return false;
            }
        }

        D3D12_ROOT_PARAMETER depthParams[3]{};
        depthParams[kDepthConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        depthParams[kDepthConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;
        depthParams[kDepthConstants].Constants.ShaderRegister = 0;
        depthParams[kDepthConstants].Constants.RegisterSpace  = 0;
        depthParams[kDepthConstants].Constants.Num32BitValues = static_cast<UINT>(sizeof(MeshDepthBatchConstants) / 4);

        depthParams[kDepthInstances].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        depthParams[kDepthInstances].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        depthParams[kDepthInstances].Descriptor.ShaderRegister = 4;
        depthParams[kDepthInstances].Descriptor.RegisterSpace  = 0;

        depthParams[kDepthIndices].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        depthParams[kDepthIndices].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        depthParams[kDepthIndices].Descriptor.ShaderRegister = 5;
        depthParams[kDepthIndices].Descriptor.RegisterSpace  = 0;

        D3D12_ROOT_SIGNATURE_DESC depthDesc{};
        depthDesc.NumParameters = 3;
        depthDesc.pParameters   = depthParams;
        depthDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        rsBlob.Reset();
        if (!serializeRoot(depthDesc, "D3D12SerializeRootSignature (instanced depth)", rsBlob))
        {
            reset();
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_depthRoot)),
                     "CreateRootSignature (instanced depth)"))
        {
            reset();
            return false;
        }

        ComPtr<ID3DBlob> depthVs;
        if (!compileShaderFromContent("shaders/BasicMeshDepthInstanced.hlsl", "VSMain", ShaderStage::Vertex, depthVs))
        {
            reset();
            return false;
        }

        D3D12_INPUT_ELEMENT_DESC depthLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC depthPso{};
        depthPso.pRootSignature                        = m_depthRoot.Get();
        depthPso.VS                                    = { depthVs->GetBufferPointer(), depthVs->GetBufferSize() };
        depthPso.PS                                    = { nullptr, 0 };
        depthPso.SampleMask                           = UINT_MAX;
        depthPso.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
        depthPso.RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
        depthPso.RasterizerState.FrontCounterClockwise = TRUE;
        depthPso.RasterizerState.DepthClipEnable       = TRUE;
        depthPso.RasterizerState.DepthBias             = -4000;
        depthPso.RasterizerState.SlopeScaledDepthBias  = -2.5f;
        depthPso.RasterizerState.DepthBiasClamp        = 0.0f;
        depthPso.DepthStencilState.DepthEnable         = TRUE;
        depthPso.DepthStencilState.DepthWriteMask      = D3D12_DEPTH_WRITE_MASK_ALL;
        depthPso.DepthStencilState.DepthFunc           = shadowDepthFunc();
        depthPso.DepthStencilState.StencilEnable       = FALSE;
        depthPso.InputLayout                           = { depthLayout, _countof(depthLayout) };
        depthPso.PrimitiveTopologyType                 = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        depthPso.NumRenderTargets                      = 0;
        depthPso.DSVFormat                             = DXGI_FORMAT_D32_FLOAT;
        depthPso.SampleDesc                            = { 1, 0 };

        if (FailedHr(device->CreateGraphicsPipelineState(&depthPso, IID_PPV_ARGS(&m_depthPso)), "CreateGraphicsPipelineState (instanced depth)"))
        {
            reset();
            return false;
        }

        DE_LOG_INFO(LogCategory::Render, "InstancedMeshPipeline: ready (gbuffer 45 DWORDs, depth 24 DWORDs)");
        return true;
    }

    void InstancedMeshPipeline::bindGBuffer(ID3D12GraphicsCommandList* cmd) const
    {
        if (!cmd || !m_gbPso)
            return;
        cmd->SetGraphicsRootSignature(m_gbRoot.Get());
        cmd->SetPipelineState((m_ultraviolet && m_gbUv) ? m_gbUv.Get() : (m_thermal && m_gbThermal) ? m_gbThermal.Get()
            : (m_writeSurfaceTag && m_gbSurfaceTag) ? m_gbSurfaceTag.Get() : m_gbPso.Get());
    }

    void InstancedMeshPipeline::setGBufferConstants(ID3D12GraphicsCommandList* cmd, const MeshBatchConstants& constants) const
    {
        if (!cmd || !m_gbRoot)
            return;
        cmd->SetGraphicsRoot32BitConstants(kGbConstants, static_cast<UINT>(sizeof(MeshBatchConstants) / 4), &constants, 0);
    }

    void InstancedMeshPipeline::setGBufferBuffers(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS instances, D3D12_GPU_VIRTUAL_ADDRESS indices) const
    {
        if (!cmd || !m_gbRoot)
            return;
        cmd->SetGraphicsRootShaderResourceView(kGbInstances, instances);
        cmd->SetGraphicsRootShaderResourceView(kGbIndices, indices);
    }

    void InstancedMeshPipeline::bindDepth(ID3D12GraphicsCommandList* cmd) const
    {
        if (!cmd || !m_depthPso)
            return;
        cmd->SetGraphicsRootSignature(m_depthRoot.Get());
        cmd->SetPipelineState(m_depthPso.Get());
    }

    void InstancedMeshPipeline::setDepthConstants(ID3D12GraphicsCommandList* cmd, const MeshDepthBatchConstants& constants) const
    {
        if (!cmd || !m_depthRoot)
            return;
        cmd->SetGraphicsRoot32BitConstants(kDepthConstants, static_cast<UINT>(sizeof(MeshDepthBatchConstants) / 4), &constants, 0);
    }

    void InstancedMeshPipeline::setDepthBuffers(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS instances, D3D12_GPU_VIRTUAL_ADDRESS indices) const
    {
        if (!cmd || !m_depthRoot)
            return;
        cmd->SetGraphicsRootShaderResourceView(kDepthInstances, instances);
        cmd->SetGraphicsRootShaderResourceView(kDepthIndices, indices);
    }

} // namespace Dark
