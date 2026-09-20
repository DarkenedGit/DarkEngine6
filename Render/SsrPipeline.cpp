#include "Render/SsrPipeline.h"
#include "Render/Camera3D.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Math/Vector2f.h"

#include <cmath>
#include <cstring>
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

        void copyMatrix(float dst[16], const Math::Matrix4f& m)
        {
            std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
        }

        void fillFullscreenPso(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso, ID3D12RootSignature* rs, ID3DBlob* vs, ID3DBlob* ps, DXGI_FORMAT format, UINT nRtv)
        {
            pso                                                  = {};
            pso.pRootSignature                                   = rs;
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
            pso.NumRenderTargets                                 = nRtv;
            pso.RTVFormats[0]                                    = format;
            if (nRtv > 1)
            {
                pso.RTVFormats[1]                                    = format;
                pso.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            }
            pso.DSVFormat  = DXGI_FORMAT_UNKNOWN;
            pso.SampleDesc = { 1, 0 };
        }

        Math::Vector2f clipToUv(const Math::Vector4f& clip)
        {
            const float iw = 1.0f / clip.w;
            return Math::Vector2f(clip.x * iw * 0.5f + 0.5f, clip.y * iw * -0.5f + 0.5f);
        }
    } // namespace

    SsrRaySetup ssrSetupDda(const Math::Vector3f& worldPos, const Math::Vector3f& R, const Math::Matrix4f& viewProj, float nearZ, float resX, float resY,
                            float rayLength)
    {
        SsrRaySetup out{};
        Math::Vector3f P0 = worldPos;
        Math::Vector3f P1 = worldPos + R * rayLength;
        Math::Vector4f clip0 = viewProj * Math::Vector4f(P0, 1.0f);
        Math::Vector4f clip1 = viewProj * Math::Vector4f(P1, 1.0f);
        const float    wMin  = nearZ > 1.0e-3f ? nearZ : 1.0e-3f;
        out.clipW0           = clip0.w;
        out.clipW1           = clip1.w;

        if (clip0.w <= wMin && clip1.w <= wMin)
        {
            out.miss = SsrMissKind::Sky;
            return out;
        }
        if (clip1.w <= wMin)
        {
            const float denom = clip0.w - clip1.w;
            if (denom > -1.0e-8f && denom < 1.0e-8f)
            {
                out.miss = SsrMissKind::Sky;
                return out;
            }
            const float tClip = ssrSaturate((clip0.w - wMin) / denom);
            P1                = Math::Vector3f(P0.x + (P1.x - P0.x) * tClip, P0.y + (P1.y - P0.y) * tClip, P0.z + (P1.z - P0.z) * tClip);
            clip1             = viewProj * Math::Vector4f(P1, 1.0f);
        }
        if (clip0.w <= wMin)
        {
            const float denom = clip1.w - clip0.w;
            if (denom > -1.0e-8f && denom < 1.0e-8f)
            {
                out.miss = SsrMissKind::Sky;
                return out;
            }
            const float tClip = ssrSaturate((wMin - clip0.w) / denom);
            P0                = Math::Vector3f(P0.x + (P1.x - P0.x) * tClip, P0.y + (P1.y - P0.y) * tClip, P0.z + (P1.z - P0.z) * tClip);
            clip0             = viewProj * Math::Vector4f(P0, 1.0f);
        }
        if (clip0.w <= wMin || clip1.w <= wMin)
        {
            out.miss = SsrMissKind::Sky;
            return out;
        }

        out.clipW0                 = clip0.w;
        out.clipW1                 = clip1.w;
        const Math::Vector2f uv0   = clipToUv(clip0);
        const Math::Vector2f uv1   = clipToUv(clip1);
        const float          du    = (uv1.x - uv0.x) * resX;
        const float          dv    = (uv1.y - uv0.y) * resY;
        out.lenPx                  = sqrtf(du * du + dv * dv);
        if (out.lenPx < 1.0f)
        {
            out.miss = SsrMissKind::Sky;
            return out;
        }
        out.ready = true;
        out.miss  = SsrMissKind::Hit;
        return out;
    }

    bool SsrPipeline::isValid() const
    {
        return m_psoTrace && m_psoUpsample && m_psoDownsample && m_psoDebug && m_half.res && m_full.res && m_history[0].res && m_history[1].res
            && m_sceneColor[0].res && m_sceneColor[1].res && m_debug.res && m_rtvHeap && m_srvHeap && m_cpuSrvHeap && m_cbUpload && m_cbMapped;
    }

    bool SsrPipeline::hasSceneColor() const
    {
        return isValid() && m_hasSceneColor;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SsrPipeline::fullSrvCpu() const
    {
        return isValid() ? cpuSrv(kCpuSrvFull) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SsrPipeline::debugConfSrvCpu() const
    {
        return isValid() ? cpuSrv(kCpuSrvDebug) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SsrPipeline::sceneColorSrvCpu() const
    {
        if (!isValid())
            return {};
        return cpuSrv(m_sceneRead == 0 ? kCpuSrvScene0 : kCpuSrvScene1);
    }

    void SsrPipeline::transitionFull(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        transition(cmd, m_full, after);
    }

    void SsrPipeline::transitionDebug(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        transition(cmd, m_debug, after);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SsrPipeline::rtvCpu(UINT slot) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvStart;
        h.ptr += static_cast<SIZE_T>(slot) * m_rtvIncr;
        return h;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SsrPipeline::cpuSrv(UINT slot) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_cpuSrvStart;
        h.ptr += static_cast<SIZE_T>(slot) * m_srvIncr;
        return h;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE SsrPipeline::passSrvGpu(UINT frame, UINT pass) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE h = m_srvGpu;
        h.ptr += static_cast<SIZE_T>((frame * kPassCount + pass) * kSrvPerPass) * m_srvIncr;
        return h;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SsrPipeline::passSrvCpu(UINT frame, UINT pass) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_srvCpu;
        h.ptr += static_cast<SIZE_T>((frame * kPassCount + pass) * kSrvPerPass) * m_srvIncr;
        return h;
    }

    void SsrPipeline::copySrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dst, D3D12_CPU_DESCRIPTOR_HANDLE src) const
    {
        if (!device || dst.ptr == 0 || src.ptr == 0)
            return;
        device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void SsrPipeline::resetTargets()
    {
        m_half.res.Reset();
        m_full.res.Reset();
        m_history[0].res.Reset();
        m_history[1].res.Reset();
        m_sceneColor[0].res.Reset();
        m_sceneColor[1].res.Reset();
        m_debug.res.Reset();
        m_half.state         = D3D12_RESOURCE_STATE_COMMON;
        m_full.state         = D3D12_RESOURCE_STATE_COMMON;
        m_history[0].state   = D3D12_RESOURCE_STATE_COMMON;
        m_history[1].state   = D3D12_RESOURCE_STATE_COMMON;
        m_sceneColor[0].state = D3D12_RESOURCE_STATE_COMMON;
        m_sceneColor[1].state = D3D12_RESOURCE_STATE_COMMON;
        m_debug.state        = D3D12_RESOURCE_STATE_COMMON;
        m_rtvHeap.Reset();
        m_srvHeap.Reset();
        m_cpuSrvHeap.Reset();
        m_rtvStart       = {};
        m_srvCpu         = {};
        m_srvGpu         = {};
        m_cpuSrvStart    = {};
        m_width          = 0;
        m_height         = 0;
        m_halfW          = 0;
        m_halfH          = 0;
        m_historyIndex   = 0;
        m_sceneRead      = 0;
        m_sceneWrite     = 1;
        m_needReset      = true;
        m_hasSceneColor  = false;
    }

    void SsrPipeline::resetAll()
    {
        resetTargets();
        m_psoTrace.Reset();
        m_psoUpsample.Reset();
        m_psoDownsample.Reset();
        m_psoDebug.Reset();
        m_rootSignature.Reset();
        if (m_cbUpload && m_cbMapped)
            m_cbUpload->Unmap(0, nullptr);
        m_cbUpload.Reset();
        m_cbMapped = nullptr;
        m_cbGpu    = 0;
    }

    void SsrPipeline::transition(ID3D12GraphicsCommandList* cmd, Target& t, D3D12_RESOURCE_STATES after) const
    {
        if (!cmd || !t.res || t.state == after)
            return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = t.res.Get();
        b.Transition.StateBefore = t.state;
        b.Transition.StateAfter  = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
        t.state = after;
    }

    bool SsrPipeline::createColorTarget(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format, const float clearColor[4],
                                        const wchar_t* name, Target& out)
    {
        out.res.Reset();
        out.state = D3D12_RESOURCE_STATE_COMMON;
        if (!device || width == 0 || height == 0)
            return false;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = width;
        rd.Height           = height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = format;
        rd.SampleDesc       = { 1, 0 };
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clear{};
        clear.Format   = format;
        clear.Color[0] = clearColor[0];
        clear.Color[1] = clearColor[1];
        clear.Color[2] = clearColor[2];
        clear.Color[3] = clearColor[3];

        if (FailedHr(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&out.res)),
                     "CreateCommittedResource SSR target"))
            return false;
        if (name)
            out.res->SetName(name);
        out.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        return true;
    }

    bool SsrPipeline::createPsos(ID3D12Device* device)
    {
        m_rootSignature.Reset();
        m_psoTrace.Reset();
        m_psoUpsample.Reset();
        m_psoDownsample.Reset();
        m_psoDebug.Reset();

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = 5;
        srvRange.BaseShaderRegister                = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[2]{};
        params[kRootCbv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[kRootCbv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootCbv].Descriptor.ShaderRegister = 0;

        params[kRootSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootSrv].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootSrv].DescriptorTable.pDescriptorRanges   = &srvRange;

        D3D12_STATIC_SAMPLER_DESC samps[2]{};
        samps[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samps[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[0].MaxLOD           = D3D12_FLOAT32_MAX;
        samps[0].ShaderRegister   = 0;
        samps[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        samps[1].Filter           = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        samps[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[1].MaxLOD           = D3D12_FLOAT32_MAX;
        samps[1].ShaderRegister   = 1;
        samps[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters     = 2;
        rsDesc.pParameters       = params;
        rsDesc.NumStaticSamplers = 2;
        rsDesc.pStaticSamplers   = samps;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (SSR)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "SSR RS: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
                     "CreateRootSignature (SSR)"))
            return false;

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> psTrace;
        ComPtr<ID3DBlob> psUp;
        ComPtr<ID3DBlob> psDown;
        ComPtr<ID3DBlob> psDebug;
        if (!compileShaderFromContent("shaders/Ssr.hlsl", "VSMain", "vs_5_0", vs) || !compileShaderFromContent("shaders/Ssr.hlsl", "PSTrace", "ps_5_0", psTrace)
            || !compileShaderFromContent("shaders/Ssr.hlsl", "PSUpsampleTemporal", "ps_5_0", psUp)
            || !compileShaderFromContent("shaders/Ssr.hlsl", "PSDownsample", "ps_5_0", psDown)
            || !compileShaderFromContent("shaders/Ssr.hlsl", "PSDebugConf", "ps_5_0", psDebug))
        {
            m_rootSignature.Reset();
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        fillFullscreenPso(pso, m_rootSignature.Get(), vs.Get(), psTrace.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoTrace)), "CreateGraphicsPipelineState (SSR trace)"))
        {
            m_rootSignature.Reset();
            return false;
        }

        fillFullscreenPso(pso, m_rootSignature.Get(), vs.Get(), psUp.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, 2);
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoUpsample)), "CreateGraphicsPipelineState (SSR upsample)"))
        {
            m_psoTrace.Reset();
            m_rootSignature.Reset();
            return false;
        }

        fillFullscreenPso(pso, m_rootSignature.Get(), vs.Get(), psDown.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoDownsample)), "CreateGraphicsPipelineState (SSR downsample)"))
        {
            m_psoUpsample.Reset();
            m_psoTrace.Reset();
            m_rootSignature.Reset();
            return false;
        }

        fillFullscreenPso(pso, m_rootSignature.Get(), vs.Get(), psDebug.Get(), DXGI_FORMAT_R8_UNORM, 1);
        if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoDebug)), "CreateGraphicsPipelineState (SSR debug)"))
        {
            m_psoDownsample.Reset();
            m_psoUpsample.Reset();
            m_psoTrace.Reset();
            m_rootSignature.Reset();
            return false;
        }
        return true;
    }

    bool SsrPipeline::createCbv(ID3D12Device* device)
    {
        if (m_cbUpload && m_cbMapped)
            return true;

        m_cbUpload.Reset();
        m_cbMapped = nullptr;
        m_cbGpu    = 0;

        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC cbDesc{};
        cbDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        cbDesc.Width            = static_cast<UINT64>(kCbBytes) * kFrameCount * kCbSlotsPerFrame;
        cbDesc.Height           = 1;
        cbDesc.DepthOrArraySize = 1;
        cbDesc.MipLevels        = 1;
        cbDesc.SampleDesc       = { 1, 0 };
        cbDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FailedHr(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&m_cbUpload)),
                     "CreateCommittedResource SSR CBV"))
            return false;
        if (FailedHr(m_cbUpload->Map(0, nullptr, reinterpret_cast<void**>(&m_cbMapped)), "Map SSR CBV"))
        {
            m_cbUpload.Reset();
            m_cbMapped = nullptr;
            return false;
        }
        m_cbGpu = m_cbUpload->GetGPUVirtualAddress();
        std::memset(m_cbMapped, 0, static_cast<size_t>(kCbBytes) * kFrameCount * kCbSlotsPerFrame);
        return true;
    }

    bool SsrPipeline::createTargets(ID3D12Device* device, uint32_t width, uint32_t height)
    {
        resetTargets();
        if (!device || width == 0 || height == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "SsrPipeline::createTargets: invalid device or size");
            return false;
        }

        m_halfW   = (width + 1u) / 2u;
        m_halfH   = (height + 1u) / 2u;
        m_rtvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.NumDescriptors = kRtvCount;
        rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FailedHr(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap)), "CreateDescriptorHeap SSR RTV"))
            return false;

        D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
        srvDesc.NumDescriptors = kFrameCount * kPassCount * kSrvPerPass;
        srvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap SSR SRV"))
        {
            m_rtvHeap.Reset();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC cpuDesc{};
        cpuDesc.NumDescriptors = kCpuSrvCount;
        cpuDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cpuDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&cpuDesc, IID_PPV_ARGS(&m_cpuSrvHeap)), "CreateDescriptorHeap SSR CPU SRV"))
        {
            m_rtvHeap.Reset();
            m_srvHeap.Reset();
            return false;
        }

        m_rtvStart    = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        m_srvCpu      = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        m_srvGpu      = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        m_cpuSrvStart = m_cpuSrvHeap->GetCPUDescriptorHandleForHeapStart();

        const float zero4[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        if (!createColorTarget(device, m_halfW, m_halfH, DXGI_FORMAT_R16G16B16A16_FLOAT, zero4, L"DE.Ssr.Half", m_half)
            || !createColorTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, zero4, L"DE.Ssr.Full", m_full)
            || !createColorTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, zero4, L"DE.Ssr.History0", m_history[0])
            || !createColorTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, zero4, L"DE.Ssr.History1", m_history[1])
            || !createColorTarget(device, m_halfW, m_halfH, DXGI_FORMAT_R16G16B16A16_FLOAT, zero4, L"DE.Ssr.SceneColor0", m_sceneColor[0])
            || !createColorTarget(device, m_halfW, m_halfH, DXGI_FORMAT_R16G16B16A16_FLOAT, zero4, L"DE.Ssr.SceneColor1", m_sceneColor[1])
            || !createColorTarget(device, width, height, DXGI_FORMAT_R8_UNORM, zero4, L"DE.Ssr.Debug", m_debug))
        {
            resetTargets();
            return false;
        }

        device->CreateRenderTargetView(m_half.res.Get(), nullptr, rtvCpu(kRtvHalf));
        device->CreateRenderTargetView(m_full.res.Get(), nullptr, rtvCpu(kRtvFull));
        device->CreateRenderTargetView(m_history[0].res.Get(), nullptr, rtvCpu(kRtvHistory0));
        device->CreateRenderTargetView(m_history[1].res.Get(), nullptr, rtvCpu(kRtvHistory1));
        device->CreateRenderTargetView(m_sceneColor[0].res.Get(), nullptr, rtvCpu(kRtvScene0));
        device->CreateRenderTargetView(m_sceneColor[1].res.Get(), nullptr, rtvCpu(kRtvScene1));
        device->CreateRenderTargetView(m_debug.res.Get(), nullptr, rtvCpu(kRtvDebug));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvRgba{};
        srvRgba.Format                  = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvRgba.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvRgba.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvRgba.Texture2D.MipLevels     = 1;
        D3D12_SHADER_RESOURCE_VIEW_DESC srvR8 = srvRgba;
        srvR8.Format                          = DXGI_FORMAT_R8_UNORM;
        device->CreateShaderResourceView(m_half.res.Get(), &srvRgba, cpuSrv(kCpuSrvHalf));
        device->CreateShaderResourceView(m_full.res.Get(), &srvRgba, cpuSrv(kCpuSrvFull));
        device->CreateShaderResourceView(m_history[0].res.Get(), &srvRgba, cpuSrv(kCpuSrvHist0));
        device->CreateShaderResourceView(m_history[1].res.Get(), &srvRgba, cpuSrv(kCpuSrvHist1));
        device->CreateShaderResourceView(m_sceneColor[0].res.Get(), &srvRgba, cpuSrv(kCpuSrvScene0));
        device->CreateShaderResourceView(m_sceneColor[1].res.Get(), &srvRgba, cpuSrv(kCpuSrvScene1));
        device->CreateShaderResourceView(m_debug.res.Get(), &srvR8, cpuSrv(kCpuSrvDebug));

        m_width         = width;
        m_height        = height;
        m_historyIndex  = 0;
        m_sceneRead     = 0;
        m_sceneWrite    = 1;
        m_needReset     = true;
        m_hasSceneColor = false;
        return true;
    }

    bool SsrPipeline::create(ID3D12Device* device, uint32_t width, uint32_t height)
    {
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "SsrPipeline::create: null device");
            resetAll();
            return false;
        }
        if (width == 0 || height == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "SsrPipeline::create: invalid size {}x{}", width, height);
            resetTargets();
            return false;
        }
        if (!m_psoTrace && !createPsos(device))
        {
            resetAll();
            return false;
        }
        if (!createCbv(device))
        {
            resetAll();
            return false;
        }
        if (!createTargets(device, width, height))
            return false;

        DE_LOG_INFO(LogCategory::Render, "SsrPipeline: ready {}x{} (half {}x{})", width, height, m_halfW, m_halfH);
        m_loggedSkip = false;
        return true;
    }

    bool SsrPipeline::resize(ID3D12Device* device, uint32_t width, uint32_t height)
    {
        if (isValid() && m_width == width && m_height == height)
            return true;
        return create(device, width, height);
    }

    void SsrPipeline::fillParams(SsrGpuParams& out, const Camera3D& camera, const Math::Matrix4f& prevViewProj, const SsrSettings& settings, bool resetHistory,
                                 uint32_t frameIndex) const
    {
        out                = {};
        out.invSizeHalf[0] = 1.0f / static_cast<float>(Math::Max(m_halfW, 1u));
        out.invSizeHalf[1] = 1.0f / static_cast<float>(Math::Max(m_halfH, 1u));
        out.invSizeFull[0] = 1.0f / static_cast<float>(Math::Max(m_width, 1u));
        out.invSizeFull[1] = 1.0f / static_cast<float>(Math::Max(m_height, 1u));
        const Math::Matrix4f viewProj = camera.GetViewProj();
        copyMatrix(out.invViewProj, viewProj.Inverse());
        copyMatrix(out.viewProj, viewProj);
        copyMatrix(out.reprojection, ssrReprojectionMatrix(viewProj, prevViewProj));
        out.nearZ        = camera.GetNearZ();
        out.thickness    = settings.thickness;
        out.stride       = settings.stride;
        out.maxRoughness = settings.maxRoughness;
        out.edgeFade     = settings.edgeFade;
        out.reset        = (resetHistory || m_needReset) ? 1.0f : 0.0f;
        out.frameOffset  = static_cast<float>(frameIndex & 7u);
        const Math::Vector3f pos = camera.GetPosition();
        out.cameraPos[0]         = pos.x;
        out.cameraPos[1]         = pos.y;
        out.cameraPos[2]         = pos.z;
    }

    void SsrPipeline::bindPass(ID3D12GraphicsCommandList* cmd, ID3D12PipelineState* pso, UINT nRtv, const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs, uint32_t w,
                               uint32_t h, D3D12_GPU_DESCRIPTOR_HANDLE tableGpu) const
    {
        D3D12_VIEWPORT vp{};
        vp.Width    = static_cast<float>(w);
        vp.Height   = static_cast<float>(h);
        vp.MaxDepth = 1.0f;
        D3D12_RECT sc{ 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);
        cmd->OMSetRenderTargets(nRtv, rtvs, FALSE, nullptr);
        cmd->SetPipelineState(pso);
        cmd->SetGraphicsRootDescriptorTable(kRootSrv, tableGpu);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

    void SsrPipeline::draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const Camera3D& camera, const Math::Matrix4f& prevViewProj,
                           const SsrSettings& settings, bool resetHistory)
    {
        if (!settings.enabled)
            return;
        if (!cmd || !isValid())
        {
            if (!isValid() && !m_loggedSkip)
            {
                DE_LOG_ERROR(LogCategory::Render, "SsrPipeline::draw: not valid — skipping");
                m_loggedSkip = true;
            }
            return;
        }
        if (!renderer.hasGBuffer())
            return;

        ID3D12Device*                     device = renderer.device();
        const D3D12_CPU_DESCRIPTOR_HANDLE depth  = renderer.depthSrvCpu();
        const D3D12_CPU_DESCRIPTOR_HANDLE attrib = renderer.attribSrvCpu();
        const D3D12_CPU_DESCRIPTOR_HANDLE vel    = renderer.velocitySrvCpu();
        const D3D12_CPU_DESCRIPTOR_HANDLE scene  = cpuSrv(m_sceneRead == 0 ? kCpuSrvScene0 : kCpuSrvScene1);
        if (!device || depth.ptr == 0 || attrib.ptr == 0 || vel.ptr == 0 || scene.ptr == 0)
            return;

        const UINT  frame = renderer.frameIndex() % kFrameCount;
        SsrGpuParams params{};
        fillParams(params, camera, prevViewProj, settings, resetHistory, renderer.frameIndex());
        const UINT cbOff = cbvByteOffset(renderer.frameIndex(), false);
        std::memcpy(m_cbMapped + cbOff, &params, sizeof(params));
        const D3D12_GPU_VIRTUAL_ADDRESS cbVa = m_cbGpu + cbOff;

        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootConstantBufferView(kRootCbv, cbVa);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        const UINT histRead  = m_historyIndex;
        const UINT histWrite = 1u - m_historyIndex;

        transition(cmd, m_sceneColor[m_sceneRead], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        {
            D3D12_CPU_DESCRIPTOR_HANDLE table = passSrvCpu(frame, kPassTrace);
            copySrv(device, table, depth);
            D3D12_CPU_DESCRIPTOR_HANDLE t1 = table;
            t1.ptr += m_srvIncr;
            copySrv(device, t1, attrib);
            D3D12_CPU_DESCRIPTOR_HANDLE t2 = t1;
            t2.ptr += m_srvIncr;
            copySrv(device, t2, scene);
            D3D12_CPU_DESCRIPTOR_HANDLE t3 = t2;
            t3.ptr += m_srvIncr;
            copySrv(device, t3, scene);
            D3D12_CPU_DESCRIPTOR_HANDLE t4 = t3;
            t4.ptr += m_srvIncr;
            copySrv(device, t4, scene);

            transition(cmd, m_half, D3D12_RESOURCE_STATE_RENDER_TARGET);
            const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvCpu(kRtvHalf);
            bindPass(cmd, m_psoTrace.Get(), 1, &rtv, m_halfW, m_halfH, passSrvGpu(frame, kPassTrace));
        }

        {
            transition(cmd, m_half, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            transition(cmd, m_history[histRead], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            transition(cmd, m_full, D3D12_RESOURCE_STATE_RENDER_TARGET);
            transition(cmd, m_history[histWrite], D3D12_RESOURCE_STATE_RENDER_TARGET);

            D3D12_CPU_DESCRIPTOR_HANDLE table = passSrvCpu(frame, kPassUpsample);
            copySrv(device, table, cpuSrv(kCpuSrvHalf));
            D3D12_CPU_DESCRIPTOR_HANDLE t1 = table;
            t1.ptr += m_srvIncr;
            copySrv(device, t1, depth);
            D3D12_CPU_DESCRIPTOR_HANDLE t2 = t1;
            t2.ptr += m_srvIncr;
            copySrv(device, t2, attrib);
            D3D12_CPU_DESCRIPTOR_HANDLE t3 = t2;
            t3.ptr += m_srvIncr;
            copySrv(device, t3, cpuSrv(histRead == 0 ? kCpuSrvHist0 : kCpuSrvHist1));
            D3D12_CPU_DESCRIPTOR_HANDLE t4 = t3;
            t4.ptr += m_srvIncr;
            copySrv(device, t4, vel);

            D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { rtvCpu(kRtvFull), rtvCpu(histWrite == 0 ? kRtvHistory0 : kRtvHistory1) };
            bindPass(cmd, m_psoUpsample.Get(), 2, rtvs, m_width, m_height, passSrvGpu(frame, kPassUpsample));
        }

        transition(cmd, m_full, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (renderer.debugState().ssrDebug == 2)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE table = passSrvCpu(frame, kPassDebug);
            copySrv(device, table, cpuSrv(kCpuSrvFull));
            D3D12_CPU_DESCRIPTOR_HANDLE t1 = table;
            t1.ptr += m_srvIncr;
            copySrv(device, t1, cpuSrv(kCpuSrvFull));
            D3D12_CPU_DESCRIPTOR_HANDLE t2 = t1;
            t2.ptr += m_srvIncr;
            copySrv(device, t2, cpuSrv(kCpuSrvFull));
            D3D12_CPU_DESCRIPTOR_HANDLE t3 = t2;
            t3.ptr += m_srvIncr;
            copySrv(device, t3, cpuSrv(kCpuSrvFull));
            D3D12_CPU_DESCRIPTOR_HANDLE t4 = t3;
            t4.ptr += m_srvIncr;
            copySrv(device, t4, cpuSrv(kCpuSrvFull));

            transition(cmd, m_debug, D3D12_RESOURCE_STATE_RENDER_TARGET);
            const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvCpu(kRtvDebug);
            bindPass(cmd, m_psoDebug.Get(), 1, &rtv, m_width, m_height, passSrvGpu(frame, kPassDebug));
            transition(cmd, m_debug, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }

        m_historyIndex = histWrite;
        m_needReset    = false;
    }

    void SsrPipeline::captureSceneColor(ID3D12GraphicsCommandList* cmd, Renderer& renderer)
    {
        if (!cmd || !isValid())
            return;
        ID3D12Device*                     device = renderer.device();
        const D3D12_CPU_DESCRIPTOR_HANDLE hdr    = renderer.hdrSrvCpu();
        if (!device || hdr.ptr == 0)
            return;

        cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        renderer.transitionHdr(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        const UINT frame = renderer.frameIndex() % kFrameCount;
        SsrGpuParams params{};
        params.invSizeHalf[0] = 1.0f / static_cast<float>(Math::Max(m_halfW, 1u));
        params.invSizeHalf[1] = 1.0f / static_cast<float>(Math::Max(m_halfH, 1u));
        params.invSizeFull[0] = 1.0f / static_cast<float>(Math::Max(m_width, 1u));
        params.invSizeFull[1] = 1.0f / static_cast<float>(Math::Max(m_height, 1u));
        const UINT cbOff = cbvByteOffset(renderer.frameIndex(), true);
        std::memcpy(m_cbMapped + cbOff, &params, sizeof(params));
        const D3D12_GPU_VIRTUAL_ADDRESS cbVa = m_cbGpu + cbOff;

        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootConstantBufferView(kRootCbv, cbVa);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        D3D12_CPU_DESCRIPTOR_HANDLE table = passSrvCpu(frame, kPassDownsample);
        copySrv(device, table, hdr);
        D3D12_CPU_DESCRIPTOR_HANDLE t = table;
        for (UINT i = 1; i < kSrvPerPass; ++i)
        {
            t.ptr += m_srvIncr;
            copySrv(device, t, hdr);
        }

        transition(cmd, m_sceneColor[m_sceneWrite], D3D12_RESOURCE_STATE_RENDER_TARGET);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvCpu(m_sceneWrite == 0 ? kRtvScene0 : kRtvScene1);
        bindPass(cmd, m_psoDownsample.Get(), 1, &rtv, m_halfW, m_halfH, passSrvGpu(frame, kPassDownsample));
        transition(cmd, m_sceneColor[m_sceneWrite], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        const UINT tmp = m_sceneRead;
        m_sceneRead    = m_sceneWrite;
        m_sceneWrite   = tmp;
        m_hasSceneColor = true;

        renderer.bindHdrDepthRead();
    }

} // namespace Dark
