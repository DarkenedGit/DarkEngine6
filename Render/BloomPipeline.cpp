#include "Render/BloomPipeline.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"

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

        void fillBlendAdditive(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso)
        {
            D3D12_RENDER_TARGET_BLEND_DESC& rt = pso.BlendState.RenderTarget[0];
            rt.BlendEnable                     = TRUE;
            rt.RenderTargetWriteMask           = D3D12_COLOR_WRITE_ENABLE_ALL;
            rt.SrcBlend                        = D3D12_BLEND_ONE;
            rt.DestBlend                       = D3D12_BLEND_ONE;
            rt.BlendOp                         = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha                   = D3D12_BLEND_ZERO;
            rt.DestBlendAlpha                  = D3D12_BLEND_ONE;
            rt.BlendOpAlpha                    = D3D12_BLEND_OP_ADD;
        }

        const wchar_t* kMipNames[BloomPipeline::kMipCount] = {
            L"DE.Bloom.Extract", L"DE.Bloom.Down0", L"DE.Bloom.Down1", L"DE.Bloom.Down2", L"DE.Bloom.Down3", L"DE.Bloom.Down4",
        };
    } // namespace

    void BloomPipeline::resetTargets()
    {
        for (UINT i = 0; i < kMipCount; ++i)
        {
            m_mips[i].res.Reset();
            m_mips[i].w     = 0;
            m_mips[i].h     = 0;
            m_mips[i].state = D3D12_RESOURCE_STATE_COMMON;
        }
        m_rtvHeap.Reset();
        m_srvHeap.Reset();
        m_rtvCpu  = {};
        m_srvCpu  = {};
        m_srvGpu  = {};
        m_width   = 0;
        m_height  = 0;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE BloomPipeline::rtvCpu(UINT mip) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvCpu;
        h.ptr += static_cast<SIZE_T>(mip) * m_rtvIncr;
        return h;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE BloomPipeline::srvGpu(UINT slot) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE h = m_srvGpu;
        h.ptr += static_cast<SIZE_T>(slot) * m_srvIncr;
        return h;
    }

    void BloomPipeline::transitionMip(ID3D12GraphicsCommandList* cmd, UINT index, D3D12_RESOURCE_STATES after) const
    {
        if (!cmd || index >= kMipCount || !m_mips[index].res || m_mips[index].state == after)
            return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = m_mips[index].res.Get();
        b.Transition.StateBefore = m_mips[index].state;
        b.Transition.StateAfter  = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
        m_mips[index].state = after;
    }

    bool BloomPipeline::createPsos(ID3D12Device* device)
    {
        m_rootSignature.Reset();
        m_psoExtract.Reset();
        m_psoDownsample.Reset();
        m_psoUpsample.Reset();
        m_psoComposite.Reset();

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = 1;
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
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (bloom)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Bloom RS: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)), "CreateRootSignature (bloom)"))
            return false;

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> psExtract;
        ComPtr<ID3DBlob> psDown;
        ComPtr<ID3DBlob> psUp;
        ComPtr<ID3DBlob> psComp;
        if (!compileShaderFromContent("shaders/Bloom.hlsl", "VSMain", "vs_5_0", vs)
            || !compileShaderFromContent("shaders/Bloom.hlsl", "PSExtract", "ps_5_0", psExtract)
            || !compileShaderFromContent("shaders/Bloom.hlsl", "PSDownsample", "ps_5_0", psDown)
            || !compileShaderFromContent("shaders/Bloom.hlsl", "PSUpsample", "ps_5_0", psUp)
            || !compileShaderFromContent("shaders/Bloom.hlsl", "PSComposite", "ps_5_0", psComp))
        {
            m_rootSignature.Reset();
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature                                   = m_rootSignature.Get();
        pso.VS                                               = { vs->GetBufferPointer(), vs->GetBufferSize() };
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

        pso.PS = { psExtract->GetBufferPointer(), psExtract->GetBufferSize() };
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoExtract)), "CreateGraphicsPipelineState (bloom extract)"))
        {
            m_rootSignature.Reset();
            return false;
        }
        pso.PS = { psDown->GetBufferPointer(), psDown->GetBufferSize() };
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoDownsample)), "CreateGraphicsPipelineState (bloom downsample)"))
        {
            m_psoExtract.Reset();
            m_rootSignature.Reset();
            return false;
        }
        fillBlendAdditive(pso);
        pso.PS = { psUp->GetBufferPointer(), psUp->GetBufferSize() };
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoUpsample)), "CreateGraphicsPipelineState (bloom upsample)"))
        {
            m_psoDownsample.Reset();
            m_psoExtract.Reset();
            m_rootSignature.Reset();
            return false;
        }
        pso.PS = { psComp->GetBufferPointer(), psComp->GetBufferSize() };
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoComposite)), "CreateGraphicsPipelineState (bloom composite)"))
        {
            m_psoUpsample.Reset();
            m_psoDownsample.Reset();
            m_psoExtract.Reset();
            m_rootSignature.Reset();
            return false;
        }
        return true;
    }

    bool BloomPipeline::createTargets(ID3D12Device* device, uint32_t width, uint32_t height)
    {
        resetTargets();
        if (!device || width == 0 || height == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "BloomPipeline::createTargets: invalid device or size");
            return false;
        }

        m_rtvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.NumDescriptors = kMipCount;
        rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FailedHr(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap)), "CreateDescriptorHeap bloom RTV"))
            return false;

        D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
        srvDesc.NumDescriptors = kSrvCount;
        srvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap bloom SRV"))
        {
            m_rtvHeap.Reset();
            return false;
        }

        m_rtvCpu = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        m_srvCpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        m_srvGpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        uint32_t    mipW          = Math::Max(width / 2u, 1u);
        uint32_t    mipH          = Math::Max(height / 2u, 1u);

        for (UINT i = 0; i < kMipCount; ++i)
        {
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            rd.Width            = mipW;
            rd.Height           = mipH;
            rd.DepthOrArraySize = 1;
            rd.MipLevels        = 1;
            rd.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
            rd.SampleDesc       = { 1, 0 };
            rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

            D3D12_CLEAR_VALUE clear{};
            clear.Format   = DXGI_FORMAT_R16G16B16A16_FLOAT;
            clear.Color[0] = clearColor[0];
            clear.Color[1] = clearColor[1];
            clear.Color[2] = clearColor[2];
            clear.Color[3] = clearColor[3];

            if (FailedHr(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&m_mips[i].res)),
                         "CreateCommittedResource bloom mip"))
            {
                resetTargets();
                return false;
            }
            m_mips[i].res->SetName(kMipNames[i]);
            m_mips[i].w     = mipW;
            m_mips[i].h     = mipH;
            m_mips[i].state = D3D12_RESOURCE_STATE_RENDER_TARGET;

            device->CreateRenderTargetView(m_mips[i].res.Get(), nullptr, rtvCpu(i));

            D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = m_srvCpu;
            srvCpu.ptr += static_cast<SIZE_T>(kHdrSlots + i) * m_srvIncr;
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format                     = DXGI_FORMAT_R16G16B16A16_FLOAT;
            srv.ViewDimension              = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels        = 1;
            device->CreateShaderResourceView(m_mips[i].res.Get(), &srv, srvCpu);

            mipW = Math::Max(mipW / 2u, 1u);
            mipH = Math::Max(mipH / 2u, 1u);
        }

        m_width  = width;
        m_height = height;
        return true;
    }

    bool BloomPipeline::create(ID3D12Device* device, uint32_t width, uint32_t height)
    {
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "BloomPipeline::create: null device");
            resetTargets();
            m_psoExtract.Reset();
            m_psoDownsample.Reset();
            m_psoUpsample.Reset();
            m_psoComposite.Reset();
            m_rootSignature.Reset();
            return false;
        }
        if (!m_psoExtract && !createPsos(device))
        {
            resetTargets();
            return false;
        }
        if (!createTargets(device, width, height))
            return false;

        DE_LOG_INFO(LogCategory::Render, "BloomPipeline: ready {}x{} ({} mips)", width, height, kMipCount);
        m_loggedSkip = false;
        return true;
    }

    bool BloomPipeline::resize(ID3D12Device* device, uint32_t width, uint32_t height)
    {
        if (m_psoExtract && m_mips[0].res && m_width == width && m_height == height)
            return true;
        return create(device, width, height);
    }

    void BloomPipeline::drawFullscreen(ID3D12GraphicsCommandList* cmd, ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE srcGpu,
                                       D3D12_CPU_DESCRIPTOR_HANDLE rtv, uint32_t dstW, uint32_t dstH, uint32_t srcW, uint32_t srcH, float strength) const
    {
        D3D12_VIEWPORT vp{};
        vp.Width    = static_cast<float>(dstW);
        vp.Height   = static_cast<float>(dstH);
        vp.MaxDepth = 1.0f;
        D3D12_RECT sc{ 0, 0, static_cast<LONG>(dstW), static_cast<LONG>(dstH) };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);
        cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        cmd->SetPipelineState(pso);
        cmd->SetGraphicsRootDescriptorTable(kRootSrv, srcGpu);
        const float constants[kConstantCount] = {
            1.0f / static_cast<float>(Math::Max(srcW, 1u)),
            1.0f / static_cast<float>(Math::Max(srcH, 1u)),
            1.0f / static_cast<float>(Math::Max(dstW, 1u)),
            1.0f / static_cast<float>(Math::Max(dstH, 1u)),
            kThreshold,
            kKnee,
            strength,
            0.0f,
        };
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, kConstantCount, constants, 0);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

    void BloomPipeline::draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, float strength) const
    {
        if (!cmd || !isValid())
        {
            if (!isValid() && !m_loggedSkip)
            {
                DE_LOG_ERROR(LogCategory::Render, "BloomPipeline::draw: not valid — skipping");
                m_loggedSkip = true;
            }
            return;
        }
        ID3D12Device* device = renderer.device();
        const D3D12_CPU_DESCRIPTOR_HANDLE hdrSrv = renderer.hdrSrvCpu();
        const D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv = renderer.hdrRtv();
        if (!device || hdrSrv.ptr == 0 || hdrRtv.ptr == 0)
            return;

        const UINT hdrSlot = renderer.frameIndex() % kHdrSlots;
        D3D12_CPU_DESCRIPTOR_HANDLE hdrDst = m_srvCpu;
        hdrDst.ptr += static_cast<SIZE_T>(hdrSlot) * m_srvIncr;
        device->CopyDescriptorsSimple(1, hdrDst, hdrSrv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Unbind HDR/DSV, then HDR → SRV for extract.
        transitionMip(cmd, 0, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const D3D12_CPU_DESCRIPTOR_HANDLE extractRtv = rtvCpu(0);
        cmd->OMSetRenderTargets(1, &extractRtv, FALSE, nullptr);
        renderer.transitionHdr(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        drawFullscreen(cmd, m_psoExtract.Get(), srvGpu(hdrSlot), extractRtv, m_mips[0].w, m_mips[0].h, renderer.width(), renderer.height(), strength);

        for (UINT i = 0; i < kDownCount; ++i)
        {
            transitionMip(cmd, i, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            transitionMip(cmd, i + 1, D3D12_RESOURCE_STATE_RENDER_TARGET);
            drawFullscreen(cmd, m_psoDownsample.Get(), srvGpu(kHdrSlots + i), rtvCpu(i + 1), m_mips[i + 1].w, m_mips[i + 1].h, m_mips[i].w, m_mips[i].h, strength);
        }

        for (UINT i = kMipCount - 1; i > 0; --i)
        {
            transitionMip(cmd, i, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            transitionMip(cmd, i - 1, D3D12_RESOURCE_STATE_RENDER_TARGET);
            drawFullscreen(cmd, m_psoUpsample.Get(), srvGpu(kHdrSlots + i), rtvCpu(i - 1), m_mips[i - 1].w, m_mips[i - 1].h, m_mips[i].w, m_mips[i].h, strength);
        }

        transitionMip(cmd, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        renderer.transitionHdr(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
        drawFullscreen(cmd, m_psoComposite.Get(), srvGpu(kHdrSlots + 0), hdrRtv, renderer.width(), renderer.height(), m_mips[0].w, m_mips[0].h, strength);
        // HDR remains RENDER_TARGET for TaaPipeline::draw → bindPostHdr.
    }

} // namespace Dark
