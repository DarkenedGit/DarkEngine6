#include "Render/LinePipeline.h"
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

    bool LinePipeline::create(ID3D12Device* device, DXGI_FORMAT colorFormat)
    {
        m_rootSignature.Reset();
        m_pso.Reset();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "LinePipeline::create: null device");
            return false;
        }

        D3D12_ROOT_PARAMETER rootParam{};
        rootParam.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParam.ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        rootParam.Constants.ShaderRegister = 0;
        rootParam.Constants.RegisterSpace  = 0;
        rootParam.Constants.Num32BitValues = static_cast<UINT>(sizeof(LineFrameConstants) / 4);

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters = 1;
        rsDesc.pParameters   = &rootParam;
        rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "Line D3D12SerializeRootSignature"))
        {
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)), "Line CreateRootSignature"))
        {
            return false;
        }

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent("shaders/Line.hlsl", "VSMain", "vs_5_0", vs) || !compileShaderFromContent("shaders/Line.hlsl", "PSMain", "ps_5_0", ps))
            return false;

        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature                                   = m_rootSignature.Get();
        psoDesc.VS                                               = { vs->GetBufferPointer(), vs->GetBufferSize() };
        psoDesc.PS                                               = { ps->GetBufferPointer(), ps->GetBufferSize() };
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.SampleMask                                       = UINT_MAX;

        psoDesc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;

        psoDesc.DepthStencilState.DepthEnable    = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // grid doesn't occlude
        psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;

        psoDesc.InputLayout           = { inputLayout, _countof(inputLayout) };
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        psoDesc.NumRenderTargets      = 1;
        psoDesc.RTVFormats[0]         = colorFormat;
        psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc            = { 1, 0 };

        if (FailedHr(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)), "Line CreateGraphicsPipelineState"))
        {
            return false;
        }

        DE_LOG_INFO(LogCategory::Render, "LinePipeline: ready");
        return true;
    }

    void LinePipeline::bind(ID3D12GraphicsCommandList* cmd) const
    {
        if (!cmd || !m_pso)
            return;
        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(m_pso.Get());
    }

    void LinePipeline::setConstants(ID3D12GraphicsCommandList* cmd, const LineFrameConstants& constants) const
    {
        if (!cmd || !m_rootSignature)
            return;
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, static_cast<UINT>(sizeof(LineFrameConstants) / 4), &constants, 0);
    }

} // namespace Dark
