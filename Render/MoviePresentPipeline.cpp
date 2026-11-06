#include "Render/MoviePresentPipeline.h"
#include "Render/Profile.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"

#include <d3dcompiler.h>

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

    bool MoviePresentPipeline::create(ID3D12Device* device)
    {
        m_rootSignature.Reset();
        m_pso.Reset();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "MoviePresentPipeline::create: null device");
            return false;
        }

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = 1;
        srvRange.BaseShaderRegister                = 0;
        srvRange.RegisterSpace                     = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[2]{};
        params[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootConstants].Constants.ShaderRegister = 0;
        params[kRootConstants].Constants.RegisterSpace  = 0;
        params[kRootConstants].Constants.Num32BitValues = kConstantCount;

        params[kRootFrameSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootFrameSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootFrameSrv].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootFrameSrv].DescriptorTable.pDescriptorRanges   = &srvRange;

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
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
        rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (movie present)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Movie present root signature error: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(
                device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
                "CreateRootSignature (movie present)"))
        {
            return false;
        }

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent("shaders/MoviePresent.hlsl", "VSMain", "vs_5_0", vs)
            || !compileShaderFromContent("shaders/MoviePresent.hlsl", "PSMain", "ps_5_0", ps))
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
        pso.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ZERO;
        pso.DepthStencilState.StencilEnable                  = FALSE;
        pso.InputLayout                                      = { nullptr, 0 };
        pso.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets                                 = 1;
        pso.RTVFormats[0]                                    = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso.DSVFormat                                        = DXGI_FORMAT_UNKNOWN;
        pso.SampleDesc                                       = { 1, 0 };

        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso)), "CreateGraphicsPipelineState (movie present)"))
        {
            m_rootSignature.Reset();
            return false;
        }

        m_rootSignature->SetName(L"DE.MoviePresentRoot");
        m_pso->SetName(L"DE.MoviePresentPso");
        DE_LOG_INFO(LogCategory::Render, "MoviePresentPipeline: ready");
        return true;
    }

    void MoviePresentPipeline::draw(ID3D12GraphicsCommandList* cmd, ID3D12DescriptorHeap* shaderHeap, D3D12_GPU_DESCRIPTOR_HANDLE frameSrv, const float rect[4], float opacity) const
    {
        if (!cmd || !m_pso || !shaderHeap || !rect || frameSrv.ptr == 0)
            return;

        MoviePresentConstants constants{};
        constants.rect[0] = rect[0];
        constants.rect[1] = rect[1];
        constants.rect[2] = rect[2];
        constants.rect[3] = rect[3];
        constants.opacity = opacity;

        const GpuScope scope(cmd, "Movie", ProfileColor::Movie);
        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(m_pso.Get());
        ID3D12DescriptorHeap* heaps[] = { shaderHeap };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, kConstantCount, &constants, 0);
        cmd->SetGraphicsRootDescriptorTable(kRootFrameSrv, frameSrv);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

} // namespace Dark
