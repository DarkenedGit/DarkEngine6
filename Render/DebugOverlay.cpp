#include "Render/DebugOverlay.h"
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

        bool createPso(
            ID3D12Device* device,
            ID3D12RootSignature* root,
            ID3DBlob* vs,
            ID3DBlob* ps,
            ComPtr<ID3D12PipelineState>& outPso,
            const char* what)
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
            pso.pRootSignature                                   = root;
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
            pso.RTVFormats[0]                                    = DXGI_FORMAT_R8G8B8A8_UNORM;
            pso.DSVFormat                                        = DXGI_FORMAT_UNKNOWN;
            pso.SampleDesc                                       = { 1, 0 };
            return !FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&outPso)), what);
        }

    } // namespace

    bool DebugOverlay::create(ID3D12Device* device)
    {
        m_rootSignature.Reset();
        m_pso2D.Reset();
        m_psoArray.Reset();
        m_srvHeap.Reset();
        if (!device)
            return false;

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = 1;
        srvRange.BaseShaderRegister                = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[2]{};
        params[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootConstants].Constants.ShaderRegister = 0;
        params[kRootConstants].Constants.Num32BitValues = 4;

        params[kRootSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootSrv].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootSrv].DescriptorTable.pDescriptorRanges   = &srvRange;

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
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
        if (FailedHr(
                D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr),
                "D3D12SerializeRootSignature (debug overlay)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Debug overlay RS: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(
                device->CreateRootSignature(
                    0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
                "CreateRootSignature (debug overlay)"))
        {
            return false;
        }

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps2d;
        ComPtr<ID3DBlob> psArr;
        if (!compileShaderFromContent("shaders/DebugOverlay2D.hlsl", "VSMain", "vs_5_0", vs)
            || !compileShaderFromContent("shaders/DebugOverlay2D.hlsl", "PSMain", "ps_5_0", ps2d)
            || !compileShaderFromContent("shaders/DebugOverlayArray.hlsl", "PSMain", "ps_5_0", psArr))
        {
            return false;
        }

        if (!createPso(device, m_rootSignature.Get(), vs.Get(), ps2d.Get(), m_pso2D, "PSO debug overlay 2D")
            || !createPso(device, m_rootSignature.Get(), vs.Get(), psArr.Get(), m_psoArray, "PSO debug overlay array"))
        {
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = kHeapSize;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap debug overlay SRV"))
            return false;
        m_cpu     = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        m_gpu     = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_frame   = 0;
        m_draw    = 0;

        DE_LOG_INFO(LogCategory::Render, "DebugOverlay: ready");
        return true;
    }

    void DebugOverlay::beginFrame(uint32_t frameIndex)
    {
        m_frame = frameIndex % kBufferedFrames;
        m_draw  = 0;
    }

    void DebugOverlay::draw2D(
        ID3D12GraphicsCommandList* cmd,
        ID3D12Device* device,
        D3D12_CPU_DESCRIPTOR_HANDLE srcSrv,
        LONG x,
        LONG y,
        LONG w,
        LONG h,
        float contrast,
        bool invert)
    {
        DebugOverlayConstants c{ 0.0f, contrast, invert ? 1.0f : 0.0f, 0.0f };
        draw(cmd, device, m_pso2D.Get(), srcSrv, x, y, w, h, c);
    }

    void DebugOverlay::drawArray(
        ID3D12GraphicsCommandList* cmd,
        ID3D12Device* device,
        D3D12_CPU_DESCRIPTOR_HANDLE srcSrv,
        LONG x,
        LONG y,
        LONG w,
        LONG h,
        float slice,
        float contrast,
        bool invert)
    {
        DebugOverlayConstants c{ slice, contrast, invert ? 1.0f : 0.0f, 0.0f };
        draw(cmd, device, m_psoArray.Get(), srcSrv, x, y, w, h, c);
    }

    void DebugOverlay::draw(
        ID3D12GraphicsCommandList* cmd,
        ID3D12Device* device,
        ID3D12PipelineState* pso,
        D3D12_CPU_DESCRIPTOR_HANDLE srcSrv,
        LONG x,
        LONG y,
        LONG w,
        LONG h,
        const DebugOverlayConstants& constants)
    {
        if (!cmd || !device || !pso || !m_srvHeap || w <= 0 || h <= 0)
            return;
        if (srcSrv.ptr == 0)
            return;
        if (m_draw >= kDrawsPerFrame)
            return;

        const UINT slot = m_frame * kDrawsPerFrame + m_draw;
        ++m_draw;

        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_cpu;
        cpu.ptr += static_cast<SIZE_T>(slot) * m_srvIncr;
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_gpu;
        gpu.ptr += static_cast<SIZE_T>(slot) * m_srvIncr;
        device->CopyDescriptorsSimple(1, cpu, srcSrv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_VIEWPORT vp{};
        vp.TopLeftX = static_cast<float>(x);
        vp.TopLeftY = static_cast<float>(y);
        vp.Width    = static_cast<float>(w);
        vp.Height   = static_cast<float>(h);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        D3D12_RECT sc{ x, y, x + w, y + h };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);

        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(pso);
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, 4, &constants, 0);
        cmd->SetGraphicsRootDescriptorTable(kRootSrv, gpu);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

} // namespace Dark
