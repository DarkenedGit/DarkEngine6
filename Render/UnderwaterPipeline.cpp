#include "Render/UnderwaterPipeline.h"

#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Render/GraphTargets.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"


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
    } // namespace

    bool UnderwaterPipeline::create(ID3D12Device* device)
    {
        release();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "UnderwaterPipeline::create: null device");
            return false;
        }

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = kSrvCount;
        srvRange.BaseShaderRegister                = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[2]{};
        params[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootConstants].Constants.ShaderRegister = 0;
        params[kRootConstants].Constants.Num32BitValues = kConstantCount;

        params[kRootSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootSrv].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootSrv].DescriptorTable.pDescriptorRanges   = &srvRange;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters = 2;
        rsDesc.pParameters   = params;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (underwater)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Underwater RS: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
                     "CreateRootSignature (underwater)"))
            return false;

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent("shaders/Underwater.hlsl", "VSMain", ShaderStage::Vertex, vs)
            || !compileShaderFromContent("shaders/Underwater.hlsl", "PSMain", ShaderStage::Pixel, ps))
        {
            m_rootSignature.Reset();
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature                                   = m_rootSignature.Get();
        pso.VS                                               = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pso.PS                                               = { ps->GetBufferPointer(), ps->GetBufferSize() };
        pso.SampleMask                                       = UINT_MAX;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode                         = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable                  = TRUE;
        pso.DepthStencilState.DepthEnable                    = FALSE;
        pso.DepthStencilState.StencilEnable                  = FALSE;
        pso.InputLayout                                      = { nullptr, 0 };
        pso.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets                                 = 1;
        pso.RTVFormats[0]                                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        pso.DSVFormat                                        = DXGI_FORMAT_UNKNOWN;
        pso.SampleDesc                                       = { 1, 0 };
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso)), "CreateGraphicsPipelineState (underwater)"))
        {
            m_rootSignature.Reset();
            return false;
        }

        DE_LOG_INFO(LogCategory::Render, "UnderwaterPipeline: ready");
        return true;
    }

    UnderwaterPipeline::~UnderwaterPipeline()
    {
        release();
    }

    void UnderwaterPipeline::release()
    {
        if (m_targets)
            m_targets->unregisterTarget(m_copy);
        m_copy    = {};
        m_targets = nullptr;
        m_pso.Reset();
        m_rootSignature.Reset();
    }

    bool UnderwaterPipeline::ensureCopy(Renderer& renderer)
    {
        // Registered on first use (only underwater frames pay for it); Renderer::resize keeps it at
        // the output size, so there is no lazy resize and no GPU wait here.
        if (m_copy.id.valid())
            return m_targets->resource(m_copy.id) != nullptr;
        GraphTargets* targets = renderer.targets();
        if (!targets || !targets->valid())
            return false;
        m_targets = targets;
        m_copy    = targets->registerTarget("DE.Underwater.HdrCopy", copyTargetDesc(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_STATE_COMMON));
        return m_copy.id.valid();
    }

    ID3D12Resource* UnderwaterPipeline::copyResource() const
    {
        return m_targets && m_copy.id.valid() ? m_targets->resource(m_copy.id) : nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE UnderwaterPipeline::copySrv() const
    {
        return m_targets && m_copy.id.valid() ? m_targets->srv(m_copy.id) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    void UnderwaterPipeline::draw(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE table, const UnderwaterFrame& frame) const
    {
        if (!cmd || !m_pso || table.ptr == 0 || !(frame.amount > 0.0f))
            return;
        const GpuScope scope(cmd, "Underwater", ProfileColor::Underwater);
        float constants[kConstantCount]{};
        for (int i = 0; i < 16; ++i)
            constants[i] = frame.invViewProj[i];
        constants[16] = frame.cameraPos[0];
        constants[17] = frame.cameraPos[1];
        constants[18] = frame.cameraPos[2];
        constants[19] = frame.waterLevel;
        constants[20] = frame.amount;

        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(m_pso.Get());
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, kConstantCount, constants, 0);
        cmd->SetGraphicsRootDescriptorTable(kRootSrv, table);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

} // namespace Dark
