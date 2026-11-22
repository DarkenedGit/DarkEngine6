#include "Render/SsrPipeline.h"
#include "Render/EnhancedBarrierEmitter.h"
#include "Render/GraphTargets.h"
#include "Render/Profile.h"
#include "Render/Camera3D.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Math/Vector2f.h"
#include "Sky/Environment.h"

#include <cmath>
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

        void copyMatrix(float dst[16], const Math::Matrix4f& m)
        {
            m.store(dst);
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

        void transitionHistory(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
        {
            recordTransition(cmd, res, before, after);
        }

        Math::Vector2f clipToUv(const Math::Vector4f& clip)
        {
            const float iw = 1.0f / clip.w;
            return Math::Vector2f(clip.x * iw * 0.5f + 0.5f, clip.y * iw * -0.5f + 0.5f);
        }

        void fillSkyEvalParams(SkyEvalParams& out, const Sky::Environment* env)
        {
            out = {};
            if (!env)
                return;
            out.sunDir[0]     = env->sunDir().x;
            out.sunDir[1]     = env->sunDir().y;
            out.sunDir[2]     = env->sunDir().z;
            out.coverage      = env->weather.cloudCoverage;
            out.sunColor[0]   = env->sunColor().x;
            out.sunColor[1]   = env->sunColor().y;
            out.sunColor[2]   = env->sunColor().z;
            out.turbidity     = env->weather.turbidity;
            out.moonDir[0]    = env->moonDir().x;
            out.moonDir[1]    = env->moonDir().y;
            out.moonDir[2]    = env->moonDir().z;
            out.rain          = env->weather.rain;
            out.moonColor[0]  = env->moonColor().x;
            out.moonColor[1]  = env->moonColor().y;
            out.moonColor[2]  = env->moonColor().z;
            out.windSpeed     = env->weather.windSpeed;
            out.windDir[0]    = env->weather.windDir.x;
            out.windDir[1]    = env->weather.windDir.y;
            out.sunElevation  = env->sunElevation();
            out.exposure      = 1.0f; // deferred sky pass: tonemap owns exposure
            out.cloudTime     = env->timeOfDay;
            Sky::CloudLayerGpu layer{};
            Sky::writeCloudLayer(layer, *env);
            std::memcpy(reinterpret_cast<uint8_t*>(&out) + offsetof(SkyEvalParams, clLightColor), &layer, sizeof(layer));
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

    SsrPipeline::~SsrPipeline()
    {
        release();
    }

    Rg::TargetDesc SsrPipeline::historyDesc()
    {
        const float zero4[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        return colorTargetDesc(DXGI_FORMAT_R16G16B16A16_FLOAT, Rg::TargetSize::OutputFloor, 1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, zero4);
    }

    bool SsrPipeline::isValid() const
    {
        return m_psoTrace && m_psoUpsample && m_psoDownsample && m_psoDebug && m_targets && m_targets->resource(m_half.id) && m_targets->resource(m_full.id)
            && m_targets->resource(m_sceneColor[0].id) && m_targets->resource(m_sceneColor[1].id) && m_targets->resource(m_debug.id) && m_srvHeap && m_cbUpload
            && m_cbMapped;
    }

    bool SsrPipeline::hasSceneColor() const
    {
        // A resize recreates the captures (new generation): nothing captured into them yet.
        return isValid() && m_hasSceneColor && m_targets->generation(m_sceneColor[m_sceneRead].id) == m_sceneColorGeneration;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SsrPipeline::fullSrvCpu() const
    {
        return isValid() ? m_targets->srv(m_full.id) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SsrPipeline::debugConfSrvCpu() const
    {
        return isValid() ? m_targets->srv(m_debug.id) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SsrPipeline::sceneColorSrvCpu() const
    {
        if (!isValid())
            return {};
        return m_targets->srv(m_sceneColor[m_sceneRead].id);
    }

    void SsrPipeline::transitionFull(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        if (m_targets)
            m_targets->transition(cmd, m_full, after);
    }

    void SsrPipeline::transitionDebug(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
    {
        if (m_targets)
            m_targets->transition(cmd, m_debug, after);
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
        if (m_targets)
        {
            m_targets->unregisterTarget(m_half);
            m_targets->unregisterTarget(m_full);
            m_targets->unregisterTarget(m_sceneColor[0]);
            m_targets->unregisterTarget(m_sceneColor[1]);
            m_targets->unregisterTarget(m_debug);
        }
        m_half          = {};
        m_full          = {};
        m_sceneColor[0] = {};
        m_sceneColor[1] = {};
        m_debug         = {};
        m_srvHeap.Reset();
        m_srvCpu               = {};
        m_srvGpu               = {};
        m_sceneRead            = 0;
        m_sceneWrite           = 1;
        m_hasSceneColor        = false;
        m_sceneColorGeneration = 0;
    }

    void SsrPipeline::release()
    {
        resetTargets();
        m_targets = nullptr;
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

        D3D12_ROOT_PARAMETER params[3]{};
        params[kRootCbv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[kRootCbv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootCbv].Descriptor.ShaderRegister = 0;

        params[kRootSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootSrv].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootSrv].DescriptorTable.pDescriptorRanges   = &srvRange;

        params[kRootSkyCbv].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[kRootSkyCbv].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootSkyCbv].Descriptor.ShaderRegister = 1;

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
        rsDesc.NumParameters     = 3;
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
        const D3D_SHADER_MACRO ssrMacros[] = {
            { "CLOUD_LAYER", "1" },
            { nullptr, nullptr },
        };
        if (!compileShaderFromContent("shaders/Ssr.hlsl", "VSMain", ShaderStage::Vertex, vs, ssrMacros)
            || !compileShaderFromContent("shaders/Ssr.hlsl", "PSTrace", ShaderStage::Pixel, psTrace, ssrMacros)
            || !compileShaderFromContent("shaders/Ssr.hlsl", "PSUpsampleTemporal", ShaderStage::Pixel, psUp, ssrMacros)
            || !compileShaderFromContent("shaders/Ssr.hlsl", "PSDownsample", ShaderStage::Pixel, psDown, ssrMacros)
            || !compileShaderFromContent("shaders/Ssr.hlsl", "PSDebugConf", ShaderStage::Pixel, psDebug, ssrMacros))
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
        cbDesc.Width            = cbUploadBytes();
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
        std::memset(m_cbMapped, 0, cbUploadBytes());
        return true;
    }

    bool SsrPipeline::createTargets(ID3D12Device* device)
    {
        resetTargets();
        m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Per-frame pass tables, filled from the graph targets' CPU views every draw: size independent.
        D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
        srvDesc.NumDescriptors = kFrameCount * kPassCount * kSrvPerPass;
        srvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap SSR SRV"))
            return false;
        m_srvHeap->SetName(L"DE.Ssr.Tables");
        m_srvCpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        m_srvGpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();

        // Half-res targets round up (ceil(w/2)). SceneColor0/1 is SSR's own capture ping-pong (read by
        // water too), not a graph history: it is written mid-frame and read later in the same frame.
        const float                 zero4[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        const DXGI_FORMAT           rgba16   = DXGI_FORMAT_R16G16B16A16_FLOAT;
        const D3D12_RESOURCE_STATES rt       = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_half          = m_targets->registerTarget("DE.Ssr.Half", colorTargetDesc(rgba16, Rg::TargetSize::OutputCeil, 2, rt, zero4));
        m_full          = m_targets->registerTarget("DE.Ssr.Full", colorTargetDesc(rgba16, Rg::TargetSize::OutputFloor, 1, rt, zero4));
        m_sceneColor[0] = m_targets->registerTarget("DE.Ssr.SceneColor0", colorTargetDesc(rgba16, Rg::TargetSize::OutputCeil, 2, rt, zero4));
        m_sceneColor[1] = m_targets->registerTarget("DE.Ssr.SceneColor1", colorTargetDesc(rgba16, Rg::TargetSize::OutputCeil, 2, rt, zero4));
        m_debug         = m_targets->registerTarget("DE.Ssr.Debug", colorTargetDesc(DXGI_FORMAT_R8_UNORM, Rg::TargetSize::OutputFloor, 1, rt, zero4));
        if (!m_half.id.valid() || !m_full.id.valid() || !m_sceneColor[0].id.valid() || !m_sceneColor[1].id.valid() || !m_debug.id.valid())
        {
            resetTargets();
            return false;
        }
        return true;
    }

    bool SsrPipeline::create(GraphTargets& targets)
    {
        ID3D12Device* device = targets.device();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "SsrPipeline::create: null device");
            release();
            return false;
        }
        if (m_targets != &targets)
            resetTargets();
        m_targets = &targets;
        if (!m_psoTrace && !createPsos(device))
        {
            release();
            return false;
        }
        if (!createCbv(device))
        {
            release();
            return false;
        }
        if (!createTargets(device))
            return false;

        DE_LOG_INFO(LogCategory::Render, "SsrPipeline: ready {}x{} (half {}x{}, graph targets)", targets.width(m_full.id), targets.height(m_full.id),
                    targets.width(m_half.id), targets.height(m_half.id));
        m_loggedSkip = false;
        return true;
    }

    void SsrPipeline::fillParams(SsrGpuParams& out, const Camera3D& camera, const Math::Matrix4f& prevViewProj, const SsrSettings& settings, bool resetHistory,
                                 uint32_t frameIndex) const
    {
        out                = {};
        out.invSizeHalf[0] = 1.0f / static_cast<float>(Math::Max(m_targets->width(m_half.id), 1u));
        out.invSizeHalf[1] = 1.0f / static_cast<float>(Math::Max(m_targets->height(m_half.id), 1u));
        out.invSizeFull[0] = 1.0f / static_cast<float>(Math::Max(m_targets->width(m_full.id), 1u));
        out.invSizeFull[1] = 1.0f / static_cast<float>(Math::Max(m_targets->height(m_full.id), 1u));
        const Math::Matrix4f viewProj = camera.GetViewProj();
        copyMatrix(out.invViewProj, viewProj.Inverse());
        copyMatrix(out.viewProj, viewProj);
        copyMatrix(out.reprojection, ssrReprojectionMatrix(viewProj, prevViewProj));
        out.nearZ        = camera.GetNearZ();
        out.thickness    = settings.thickness;
        out.stride       = settings.stride;
        out.maxRoughness = settings.maxRoughness;
        out.edgeFade     = settings.edgeFade;
        out.reset        = resetHistory ? 1.0f : 0.0f;
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
                           const Sky::Environment* env, const SsrSettings& settings, const HistoryView& history, bool resetHistory)
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
        if (!renderer.hasSceneBuffers())
            return;

        ID3D12Device*                     device = renderer.device();
        const D3D12_CPU_DESCRIPTOR_HANDLE depth  = renderer.depthSrvCpu();
        const D3D12_CPU_DESCRIPTOR_HANDLE attrib = renderer.attribSrvCpu();
        const D3D12_CPU_DESCRIPTOR_HANDLE vel    = renderer.velocitySrvCpu();
        const D3D12_CPU_DESCRIPTOR_HANDLE scene  = m_targets->srv(m_sceneColor[m_sceneRead].id);
        if (!device || depth.ptr == 0 || attrib.ptr == 0 || vel.ptr == 0 || scene.ptr == 0 || !history.valid())
            return;
        // Without a valid prev (graph SsrHistory) the shader ignores t3; it is still a real target.
        resetHistory = resetHistory || !history.prevValid;
        const uint32_t halfW = m_targets->width(m_half.id);
        const uint32_t halfH = m_targets->height(m_half.id);
        const uint32_t fullW = m_targets->width(m_full.id);
        const uint32_t fullH = m_targets->height(m_full.id);

        const GpuScope ssr(cmd, "SSR", ProfileColor::Ssr);
        const UINT  frame = renderer.frameIndex() % kFrameCount;
        SsrGpuParams params{};
        fillParams(params, camera, prevViewProj, settings, resetHistory, renderer.frameIndex());
        params.hasSkyEval = env ? 1.0f : 0.0f;
        const UINT cbOff = cbvByteOffset(renderer.frameIndex(), false);
        std::memcpy(m_cbMapped + cbOff, &params, sizeof(params));
        const D3D12_GPU_VIRTUAL_ADDRESS cbVa = m_cbGpu + cbOff;

        SkyEvalParams sky{};
        fillSkyEvalParams(sky, env);
        const UINT skyOff = skyEvalCbvByteOffset(renderer.frameIndex());
        std::memcpy(m_cbMapped + skyOff, &sky, sizeof(sky));
        const D3D12_GPU_VIRTUAL_ADDRESS skyVa = m_cbGpu + skyOff;

        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootConstantBufferView(kRootCbv, cbVa);
        cmd->SetGraphicsRootConstantBufferView(kRootSkyCbv, skyVa);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        m_targets->transition(cmd, m_sceneColor[m_sceneRead], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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

            m_targets->transition(cmd, m_half, D3D12_RESOURCE_STATE_RENDER_TARGET);
            const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_targets->rtv(m_half.id);
            bindPass(cmd, m_psoTrace.Get(), 1, &rtv, halfW, halfH, passSrvGpu(frame, kPassTrace));
        }

        {
            m_targets->transition(cmd, m_half, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            m_targets->transition(cmd, m_full, D3D12_RESOURCE_STATE_RENDER_TARGET);
            transitionHistory(cmd, history.curr, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

            D3D12_CPU_DESCRIPTOR_HANDLE table = passSrvCpu(frame, kPassUpsample);
            copySrv(device, table, m_targets->srv(m_half.id));
            D3D12_CPU_DESCRIPTOR_HANDLE t1 = table;
            t1.ptr += m_srvIncr;
            copySrv(device, t1, depth);
            D3D12_CPU_DESCRIPTOR_HANDLE t2 = t1;
            t2.ptr += m_srvIncr;
            copySrv(device, t2, attrib);
            D3D12_CPU_DESCRIPTOR_HANDLE t3 = t2;
            t3.ptr += m_srvIncr;
            copySrv(device, t3, history.prevSrv);
            D3D12_CPU_DESCRIPTOR_HANDLE t4 = t3;
            t4.ptr += m_srvIncr;
            copySrv(device, t4, vel);

            D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { m_targets->rtv(m_full.id), history.currRtv };
            bindPass(cmd, m_psoUpsample.Get(), 2, rtvs, fullW, fullH, passSrvGpu(frame, kPassUpsample));
            transitionHistory(cmd, history.curr, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }

        m_targets->transition(cmd, m_full, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (renderer.debugState().ssrDebug == 2)
        {
            const D3D12_CPU_DESCRIPTOR_HANDLE full  = m_targets->srv(m_full.id);
            D3D12_CPU_DESCRIPTOR_HANDLE       table = passSrvCpu(frame, kPassDebug);
            copySrv(device, table, full);
            D3D12_CPU_DESCRIPTOR_HANDLE t1 = table;
            t1.ptr += m_srvIncr;
            copySrv(device, t1, full);
            D3D12_CPU_DESCRIPTOR_HANDLE t2 = t1;
            t2.ptr += m_srvIncr;
            copySrv(device, t2, full);
            D3D12_CPU_DESCRIPTOR_HANDLE t3 = t2;
            t3.ptr += m_srvIncr;
            copySrv(device, t3, full);
            D3D12_CPU_DESCRIPTOR_HANDLE t4 = t3;
            t4.ptr += m_srvIncr;
            copySrv(device, t4, full);

            m_targets->transition(cmd, m_debug, D3D12_RESOURCE_STATE_RENDER_TARGET);
            const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_targets->rtv(m_debug.id);
            bindPass(cmd, m_psoDebug.Get(), 1, &rtv, fullW, fullH, passSrvGpu(frame, kPassDebug));
            m_targets->transition(cmd, m_debug, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
    }

    void SsrPipeline::captureSceneColor(ID3D12GraphicsCommandList* cmd, Renderer& renderer)
    {
        if (!cmd || !isValid())
            return;
        ID3D12Device*                     device = renderer.device();
        const D3D12_CPU_DESCRIPTOR_HANDLE hdr    = renderer.hdrSrvCpu();
        if (!device || hdr.ptr == 0)
            return;

        const GpuScope capture(cmd, "SSR Capture", ProfileColor::Ssr);
        cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        renderer.transitionHdr(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        const UINT frame = renderer.frameIndex() % kFrameCount;
        SsrGpuParams params{};
        const uint32_t halfW = m_targets->width(m_half.id);
        const uint32_t halfH = m_targets->height(m_half.id);
        params.invSizeHalf[0] = 1.0f / static_cast<float>(Math::Max(halfW, 1u));
        params.invSizeHalf[1] = 1.0f / static_cast<float>(Math::Max(halfH, 1u));
        params.invSizeFull[0] = 1.0f / static_cast<float>(Math::Max(m_targets->width(m_full.id), 1u));
        params.invSizeFull[1] = 1.0f / static_cast<float>(Math::Max(m_targets->height(m_full.id), 1u));
        const UINT cbOff = cbvByteOffset(renderer.frameIndex(), true);
        std::memcpy(m_cbMapped + cbOff, &params, sizeof(params));
        const D3D12_GPU_VIRTUAL_ADDRESS cbVa = m_cbGpu + cbOff;

        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootConstantBufferView(kRootCbv, cbVa);
        cmd->SetGraphicsRootConstantBufferView(kRootSkyCbv, m_cbGpu + skyEvalCbvByteOffset(renderer.frameIndex()));
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        D3D12_CPU_DESCRIPTOR_HANDLE table = passSrvCpu(frame, kPassDownsample);
        copySrv(device, table, hdr);
        D3D12_CPU_DESCRIPTOR_HANDLE t = table;
        for (UINT i = 1; i < kSrvPerPass; ++i)
        {
            t.ptr += m_srvIncr;
            copySrv(device, t, hdr);
        }

        m_targets->transition(cmd, m_sceneColor[m_sceneWrite], D3D12_RESOURCE_STATE_RENDER_TARGET);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_targets->rtv(m_sceneColor[m_sceneWrite].id);
        bindPass(cmd, m_psoDownsample.Get(), 1, &rtv, halfW, halfH, passSrvGpu(frame, kPassDownsample));
        m_targets->transition(cmd, m_sceneColor[m_sceneWrite], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        const UINT tmp = m_sceneRead;
        m_sceneRead    = m_sceneWrite;
        m_sceneWrite   = tmp;
        m_hasSceneColor        = true;
        m_sceneColorGeneration = m_targets->generation(m_sceneColor[m_sceneRead].id);

        renderer.bindHdrDepthRead();
    }

} // namespace Dark
