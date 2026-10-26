#include "Render/UnderwaterPipeline.h"

#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"

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

    bool UnderwaterPipeline::create(ID3D12Device* device)
    {
        m_rootSignature.Reset();
        m_pso.Reset();
        m_srvHeap.Reset();
        m_copySrvHeap.Reset();
        m_copy.Reset();
        m_copyW     = 0;
        m_copyH     = 0;
        m_copyState = D3D12_RESOURCE_STATE_COMMON;
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "UnderwaterPipeline::create: null device");
            return false;
        }

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = kSrvPerFrame;
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
        if (!compileShaderFromContent("shaders/Underwater.hlsl", "VSMain", "vs_5_0", vs)
            || !compileShaderFromContent("shaders/Underwater.hlsl", "PSMain", "ps_5_0", ps))
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

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = kBufferedFrames * kSrvPerFrame;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap underwater SRV"))
        {
            m_pso.Reset();
            m_rootSignature.Reset();
            return false;
        }
        m_cpu     = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        m_gpu     = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_DESCRIPTOR_HEAP_DESC copyHeap{};
        copyHeap.NumDescriptors = 1;
        copyHeap.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        copyHeap.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&copyHeap, IID_PPV_ARGS(&m_copySrvHeap)), "CreateDescriptorHeap underwater copy SRV"))
        {
            m_pso.Reset();
            m_rootSignature.Reset();
            m_srvHeap.Reset();
            return false;
        }
        m_copyCpu = m_copySrvHeap->GetCPUDescriptorHandleForHeapStart();

        DE_LOG_INFO(LogCategory::Render, "UnderwaterPipeline: ready");
        return true;
    }

    bool UnderwaterPipeline::ensureCopy(Renderer& renderer)
    {
        ID3D12Device* device = renderer.device();
        const uint32_t w     = renderer.width();
        const uint32_t h     = renderer.height();
        if (!device || w == 0 || h == 0 || m_copyCpu.ptr == 0)
            return false;
        if (m_copy && m_copyW == w && m_copyH == h)
            return true;

        if (m_copy)
            renderer.waitForGpu();
        m_copy.Reset();
        m_copyW     = 0;
        m_copyH     = 0;
        m_copyState = D3D12_RESOURCE_STATE_COMMON;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = w;
        rd.Height           = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rd.SampleDesc       = { 1, 0 };
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_copy)),
                     "CreateCommittedResource underwater HDR copy"))
            return false;
        m_copy->SetName(L"DE.Underwater.HdrCopy");

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format                  = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels     = 1;
        device->CreateShaderResourceView(m_copy.Get(), &srv, m_copyCpu);

        m_copyW     = w;
        m_copyH     = h;
        m_copyState = D3D12_RESOURCE_STATE_COMMON;
        return true;
    }

    void UnderwaterPipeline::transitionCopy(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        if (!cmd || !m_copy || m_copyState == after)
            return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = m_copy.Get();
        b.Transition.StateBefore = m_copyState;
        b.Transition.StateAfter  = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
        m_copyState = after;
    }

    void UnderwaterPipeline::draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const UnderwaterFrame& frame)
    {
        if (!cmd || !m_pso || !m_srvHeap || !(frame.amount > 0.0f))
            return;
        ID3D12Device* device = renderer.device();
        ID3D12Resource* hdr  = renderer.hdr();
        const D3D12_CPU_DESCRIPTOR_HANDLE depth = renderer.depthSrvCpu();
        if (!device || !hdr || depth.ptr == 0 || !ensureCopy(renderer))
            return;

        const GpuScope scope(cmd, "Underwater", ProfileColor::Underwater);
        cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        renderer.transitionHdr(cmd, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transitionCopy(cmd, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(m_copy.Get(), hdr);
        transitionCopy(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        renderer.bindHdrColorTarget();
        // bindHdrColorTarget also transitions depth...
        const UINT slot = renderer.frameIndex() % kBufferedFrames;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_cpu;
        cpu.ptr += static_cast<SIZE_T>(slot * kSrvPerFrame) * m_srvIncr;
        device->CopyDescriptorsSimple(1, cpu, m_copyCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE depthCpu = cpu;
        depthCpu.ptr += m_srvIncr;
        device->CopyDescriptorsSimple(1, depthCpu, depth, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_gpu;
        gpu.ptr += static_cast<SIZE_T>(slot * kSrvPerFrame) * m_srvIncr;

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
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, kConstantCount, constants, 0);
        cmd->SetGraphicsRootDescriptorTable(kRootSrv, gpu);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

} // namespace Dark
