#include "Render/SsrPipeline.h"
#include "Render/GraphTargets.h"
#include "Render/Profile.h"
#include "Render/Camera3D.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Math/Vector2f.h"

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
        return m_psoTrace && m_psoUpsample && m_psoDownsample && m_psoDebug && m_targets && m_targets->resource(m_half.id) && m_targets->resource(m_full.id) &&
               m_targets->resource(m_sceneColor[0].id) && m_targets->resource(m_sceneColor[1].id) && m_targets->resource(m_debug.id) && m_cbUpload && m_cbMapped;
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

    const GraphTarget& SsrPipeline::target(SsrTarget which) const
    {
        switch (which)
        {
        case SsrTarget::Half:
            return m_half;
        case SsrTarget::Full:
            return m_full;
        case SsrTarget::SceneColor0:
            return m_sceneColor[0];
        case SsrTarget::SceneColor1:
            return m_sceneColor[1];
        case SsrTarget::Debug:
            break;
        }
        return m_debug;
    }

    ID3D12Resource* SsrPipeline::resource(SsrTarget which) const
    {
        return m_targets ? m_targets->resource(target(which).id) : nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SsrPipeline::srv(SsrTarget which) const
    {
        return m_targets ? m_targets->srv(target(which).id) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SsrPipeline::rtv(SsrTarget which) const
    {
        return m_targets ? m_targets->rtv(target(which).id) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    const char* SsrPipeline::name(SsrTarget which)
    {
        switch (which)
        {
        case SsrTarget::Half:
            return "DE.Ssr.Half";
        case SsrTarget::Full:
            return "DE.Ssr.Full";
        case SsrTarget::SceneColor0:
            return "DE.Ssr.SceneColor0";
        case SsrTarget::SceneColor1:
            return "DE.Ssr.SceneColor1";
        case SsrTarget::Debug:
            break;
        }
        return "DE.Ssr.Debug";
    }

    SsrTarget SsrPipeline::sceneColorRead() const
    {
        return m_sceneRead == 0 ? SsrTarget::SceneColor0 : SsrTarget::SceneColor1;
    }

    SsrTarget SsrPipeline::beginCapture()
    {
        const SsrTarget written = m_sceneWrite == 0 ? SsrTarget::SceneColor0 : SsrTarget::SceneColor1;
        const UINT      tmp     = m_sceneRead;
        m_sceneRead             = m_sceneWrite;
        m_sceneWrite            = tmp;
        m_hasSceneColor         = true;
        m_sceneColorGeneration  = m_targets ? m_targets->generation(m_sceneColor[m_sceneRead].id) : 0;
        return written;
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
        m_debug                = {};
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

    bool SsrPipeline::createTargets()
    {
        resetTargets();
        // Half-res targets round up (ceil(w/2)). SceneColor0/1 is SSR's own capture ping-pong (read by
        // water too), not a graph history: it is written mid-frame and read later in the same frame.
        const float                 zero4[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        const DXGI_FORMAT           rgba16   = DXGI_FORMAT_R16G16B16A16_FLOAT;
        const D3D12_RESOURCE_STATES rt       = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_half                               = m_targets->registerTarget(name(SsrTarget::Half), colorTargetDesc(rgba16, Rg::TargetSize::OutputCeil, 2, rt, zero4));
        m_full                               = m_targets->registerTarget(name(SsrTarget::Full), colorTargetDesc(rgba16, Rg::TargetSize::OutputFloor, 1, rt, zero4));
        m_sceneColor[0]                      = m_targets->registerTarget(name(SsrTarget::SceneColor0), colorTargetDesc(rgba16, Rg::TargetSize::OutputCeil, 2, rt, zero4));
        m_sceneColor[1]                      = m_targets->registerTarget(name(SsrTarget::SceneColor1), colorTargetDesc(rgba16, Rg::TargetSize::OutputCeil, 2, rt, zero4));
        m_debug                              = m_targets->registerTarget(name(SsrTarget::Debug), colorTargetDesc(DXGI_FORMAT_R8_UNORM, Rg::TargetSize::OutputFloor, 1, rt, zero4));
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
        if (!createTargets())
            return false;

        DE_LOG_INFO(LogCategory::Render, "SsrPipeline: ready {}x{} (half {}x{}, graph targets)", targets.width(m_full.id), targets.height(m_full.id), targets.width(m_half.id),
                    targets.height(m_half.id));
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

    SsrFrameParams SsrPipeline::uploadParams(const Camera3D& camera, const Math::Matrix4f& prevViewProj, const IEnvironmentProvider* env, const SsrSettings& settings, bool resetHistory,
                                             uint32_t frameIndex)
    {
        if (!isValid())
            return {};
        SsrGpuParams params{};
        fillParams(params, camera, prevViewProj, settings, resetHistory, frameIndex);
        params.hasSkyEval = env ? 1.0f : 0.0f;
        const UINT cbOff  = cbvByteOffset(frameIndex, false);
        std::memcpy(m_cbMapped + cbOff, &params, sizeof(params));

        SkyEvalParams sky{};
        if (env)
            env->skyEval(sky);
        const UINT skyOff = skyEvalCbvByteOffset(frameIndex);
        std::memcpy(m_cbMapped + skyOff, &sky, sizeof(sky));
        return { m_cbGpu + cbOff, m_cbGpu + skyOff };
    }

    SsrFrameParams SsrPipeline::paramsAddress(uint32_t frameIndex) const
    {
        if (!isValid())
            return {};
        return { m_cbGpu + cbvByteOffset(frameIndex, false), m_cbGpu + skyEvalCbvByteOffset(frameIndex) };
    }

    SsrFrameParams SsrPipeline::uploadCaptureParams(uint32_t frameIndex)
    {
        if (!isValid())
            return {};
        SsrGpuParams   params{};
        const uint32_t halfW  = m_targets->width(m_half.id);
        const uint32_t halfH  = m_targets->height(m_half.id);
        params.invSizeHalf[0] = 1.0f / static_cast<float>(Math::Max(halfW, 1u));
        params.invSizeHalf[1] = 1.0f / static_cast<float>(Math::Max(halfH, 1u));
        params.invSizeFull[0] = 1.0f / static_cast<float>(Math::Max(m_targets->width(m_full.id), 1u));
        params.invSizeFull[1] = 1.0f / static_cast<float>(Math::Max(m_targets->height(m_full.id), 1u));
        const UINT cbOff      = cbvByteOffset(frameIndex, true);
        std::memcpy(m_cbMapped + cbOff, &params, sizeof(params));
        // The capture binds whatever sky constants this frame index holds (the root signature needs one).
        return { m_cbGpu + cbOff, m_cbGpu + skyEvalCbvByteOffset(frameIndex) };
    }

    void SsrPipeline::drawStep(ID3D12GraphicsCommandList* cmd, SsrStep step, const SsrFrameParams& params, D3D12_GPU_DESCRIPTOR_HANDLE table, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                               D3D12_CPU_DESCRIPTOR_HANDLE historyRtv) const
    {
        if (!cmd || !isValid() || params.params == 0 || params.sky == 0 || table.ptr == 0 || rtv.ptr == 0)
            return;
        ID3D12PipelineState* pso  = nullptr;
        UINT                 nRtv = 1;
        GraphTarget          size = m_full;
        switch (step)
        {
        case SsrStep::Trace:
            pso  = m_psoTrace.Get();
            size = m_half;
            break;
        case SsrStep::Upsample:
            if (historyRtv.ptr == 0)
                return;
            pso  = m_psoUpsample.Get();
            nRtv = 2;
            break;
        case SsrStep::Debug:
            pso = m_psoDebug.Get();
            break;
        case SsrStep::Capture:
            pso  = m_psoDownsample.Get();
            size = m_half; // the captures are half res like Half
            break;
        }
        const uint32_t w = m_targets->width(size.id);
        const uint32_t h = m_targets->height(size.id);
        D3D12_VIEWPORT vp{};
        vp.Width    = static_cast<float>(w);
        vp.Height   = static_cast<float>(h);
        vp.MaxDepth = 1.0f;
        D3D12_RECT                        sc{ 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
        const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { rtv, historyRtv };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);
        cmd->OMSetRenderTargets(nRtv, rtvs, FALSE, nullptr);
        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(pso);
        cmd->SetGraphicsRootConstantBufferView(kRootCbv, params.params);
        cmd->SetGraphicsRootConstantBufferView(kRootSkyCbv, params.sky);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->SetGraphicsRootDescriptorTable(kRootSrv, table);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

} // namespace Dark
