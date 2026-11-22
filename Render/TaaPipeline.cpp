#include "Render/TaaPipeline.h"
#include "Render/Profile.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"

#include <cstring>

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

    bool TaaPipeline::create(ID3D12Device* device)
    {
        m_rootSignature.Reset();
        m_pso.Reset();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "TaaPipeline::create: null device");
            return false;
        }

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = kSrvCount;
        srvRange.BaseShaderRegister                = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[2]{};
        params[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootConstants].Constants.ShaderRegister = 0;
        params[kRootConstants].Constants.Num32BitValues = kConstantCount;

        params[kRootSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootSrv].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootSrv].DescriptorTable.pDescriptorRanges   = &srvRange;

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.MaxLOD           = D3D12_FLOAT32_MAX;
        samp.ShaderRegister   = 0;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters     = 2;
        rsDesc.pParameters       = params;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers   = &samp;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (TAA)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "TAA RS: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)), "CreateRootSignature (TAA)"))
            return false;

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent("shaders/Taa.hlsl", "VSMain", ShaderStage::Vertex, vs) || !compileShaderFromContent("shaders/Taa.hlsl", "PSMain", ShaderStage::Pixel, ps))
            return false;

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
        pso.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.NumRenderTargets                                 = 2; // post + history (no Post→History copy)
        pso.RTVFormats[0]                                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        pso.RTVFormats[1]                                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        pso.DSVFormat                                        = DXGI_FORMAT_UNKNOWN;
        pso.SampleDesc                                       = { 1, 0 };
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso)), "CreateGraphicsPipelineState (TAA)"))
        {
            m_rootSignature.Reset();
            return false;
        }

        DE_LOG_INFO(LogCategory::Render, "TaaPipeline: ready");
        return true;
    }

    void TaaPipeline::draw(ID3D12GraphicsCommandList* cmd, const TaaSettings& settings, const TaaTargets& targets) const
    {
        if (!cmd || !m_pso || targets.srvTable.ptr == 0 || targets.postRtv.ptr == 0 || targets.historyRtv.ptr == 0 || targets.width == 0 || targets.height == 0)
            return;

        const GpuScope taa(cmd, "TAA", ProfileColor::Taa);
        // MRT: post HDR + the graph's TAA history (current slot), replacing the old Post->History copy.
        const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { targets.postRtv, targets.historyRtv };
        cmd->OMSetRenderTargets(2, rtvs, FALSE, nullptr);
        D3D12_VIEWPORT vp{};
        vp.Width    = static_cast<float>(targets.width);
        vp.Height   = static_cast<float>(targets.height);
        vp.MaxDepth = 1.0f;
        const D3D12_RECT sc{ 0, 0, static_cast<LONG>(targets.width), static_cast<LONG>(targets.height) };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);

        const float w = static_cast<float>(Math::Max(targets.width, 1u));
        const float h = static_cast<float>(Math::Max(targets.height, 1u));
        float constants[kConstantCount]{};
        std::memcpy(constants, settings.invViewProj, sizeof(float) * 16);
        std::memcpy(constants + 16, settings.prevViewProj, sizeof(float) * 16);
        constants[32] = settings.blend;
        constants[33] = settings.reset ? 1.0f : 0.0f;
        constants[34] = 1.0f / w;
        constants[35] = 1.0f / h;

        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(m_pso.Get());
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, kConstantCount, constants, 0);
        cmd->SetGraphicsRootDescriptorTable(kRootSrv, targets.srvTable);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

} // namespace Dark
