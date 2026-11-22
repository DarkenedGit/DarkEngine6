#include "Render/SceneBuffers.h"

#include "Render/EnhancedBarrierEmitter.h"
#include "Render/GraphTargets.h"
#include "Math/Color.h"
#include "Core/Log.h"

namespace Dark
{

    namespace
    {
        bool checkHr(HRESULT hr, const char* what)
        {
            if (FAILED(hr))
            {
                DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
                return false;
            }
            return true;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE offsetHandle(D3D12_CPU_DESCRIPTOR_HANDLE start, UINT index, UINT incr)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE h = start;
            h.ptr += static_cast<SIZE_T>(index) * incr;
            return h;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC tex2dSrv(DXGI_FORMAT format)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format                  = format;
            srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels     = 1;
            return srv;
        }
    } // namespace

    SceneBuffers::~SceneBuffers()
    {
        reset();
    }

    void SceneBuffers::reset()
    {
        if (m_targets)
            for (Rg::TargetId& id : m_ids)
                m_targets->unregisterTexture(id);
        for (Rg::TargetId& id : m_ids)
            id = {};
        m_targets = nullptr;
        m_lightingHeap.Reset();
        m_lightingGpu    = {};
        m_heightGpu      = {};
        m_aoGpu          = {};
        m_iblGpu         = {};
        m_ssrGpu         = {};
        m_localShadowGpu = {};
        m_shadowCpu      = {};
        m_localShadowCpu = {};
        m_localShadowRecordsCpu = {};
        m_lightingAoCpu  = {};
        m_lightingSsrCpu = {};
        m_heightCpu      = {};
        m_iblIrrCpu      = {};
        m_iblPrefCpu     = {};
        m_iblLutCpu      = {};
        m_lightingAlbedoRaw = false;
        m_hdrClear[0] = 0.0f;
        m_hdrClear[1] = 0.0f;
        m_hdrClear[2] = 0.0f;
        m_hdrClear[3] = 1.0f;
    }

    bool SceneBuffers::valid() const
    {
        return hdr() != nullptr && albedo() != nullptr && ao() != nullptr;
    }

    uint32_t SceneBuffers::width() const
    {
        return m_targets ? m_targets->width(m_ids[kHdr]) : 0;
    }

    uint32_t SceneBuffers::height() const
    {
        return m_targets ? m_targets->height(m_ids[kHdr]) : 0;
    }

    ID3D12Resource* SceneBuffers::resource(Slot slot) const
    {
        return m_targets ? m_targets->resource(m_ids[slot]) : nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SceneBuffers::rtv(Slot slot) const
    {
        return m_targets ? m_targets->rtv(m_ids[slot]) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SceneBuffers::srv(Slot slot) const
    {
        return m_targets ? m_targets->srv(m_ids[slot]) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    void SceneBuffers::transition(ID3D12GraphicsCommandList* cmd, Slot slot, D3D12_RESOURCE_STATES after)
    {
        // The targets are graph targets, tracked from creation: the frame ResourceStateTracker holds
        // their state (Phase 6), so a target recreated by a resize starts from its real creation state.
        ID3D12Resource* res = resource(slot);
        if (!cmd || !res)
            return;
        recordTransitionTo(cmd, res, after);
    }

    bool SceneBuffers::create(GraphTargets& targets, D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpu, const float hdrClear[4])
    {
        const D3D12_CPU_DESCRIPTOR_HANDLE savedShadow = m_shadowCpu;
        const D3D12_CPU_DESCRIPTOR_HANDLE savedLocalShadow = m_localShadowCpu;
        const D3D12_CPU_DESCRIPTOR_HANDLE savedLocalRecords = m_localShadowRecordsCpu;
        const D3D12_CPU_DESCRIPTOR_HANDLE savedHeight = m_heightCpu;
        const D3D12_CPU_DESCRIPTOR_HANDLE savedIblIrr = m_iblIrrCpu;
        const D3D12_CPU_DESCRIPTOR_HANDLE savedIblPref = m_iblPrefCpu;
        const D3D12_CPU_DESCRIPTOR_HANDLE savedIblLut = m_iblLutCpu;
        reset();
        m_shadowCpu  = savedShadow;
        m_localShadowCpu = savedLocalShadow;
        m_localShadowRecordsCpu = savedLocalRecords;
        m_heightCpu  = savedHeight;
        m_iblIrrCpu  = savedIblIrr;
        m_iblPrefCpu = savedIblPref;
        m_iblLutCpu  = savedIblLut;
        ID3D12Device* device = targets.device();
        if (!device || targets.pool().outputWidth() == 0 || targets.pool().outputHeight() == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "SceneBuffers::create: invalid device or size");
            return false;
        }
        if (hdrClear)
        {
            // UI / swapchain clears are sRGB; tonemap applies IEC OETF. Store linear so kVoid stays #081018 on screen.
            m_hdrClear[0] = Color::srgbToLinear(hdrClear[0]);
            m_hdrClear[1] = Color::srgbToLinear(hdrClear[1]);
            m_hdrClear[2] = Color::srgbToLinear(hdrClear[2]);
            m_hdrClear[3] = hdrClear[3];
        }

        m_targets = &targets;
        m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        const auto desc = [](DXGI_FORMAT format, DXGI_FORMAT rtvFormat, DXGI_FORMAT srvFormat, const float clear[4], bool uav)
        {
            Rg::TargetDesc d = colorTargetDesc(format, Rg::TargetSize::OutputFloor, 1, D3D12_RESOURCE_STATE_RENDER_TARGET, clear);
            d.targetFormat   = static_cast<uint32_t>(rtvFormat);
            d.srvFormat      = static_cast<uint32_t>(srvFormat);
            if (uav)
                d.flags |= static_cast<uint32_t>(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            return d;
        };
        const DXGI_FORMAT rgba16 = DXGI_FORMAT_R16G16B16A16_FLOAT;
        // Albedo/attrib are TYPELESS: RTV sRGB/UNORM; the graph SRV is raw UNORM (overlay byte dump, lighting attrib).
        m_ids[kHdr]      = targets.registerTexture("DE.HdrColor", desc(rgba16, rgba16, rgba16, m_hdrClear, false));
        m_ids[kAlbedo]   = targets.registerTexture("DE.GBuffer.Albedo", desc(DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM, kAlbedoClear, true));
        m_ids[kAttrib]   = targets.registerTexture("DE.GBuffer.Attrib", desc(DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, kAttribClear, true));
        m_ids[kVelocity] = targets.registerTexture("DE.GBuffer.Velocity", desc(DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT, kVelocityClear, false));
        m_ids[kPost]     = targets.registerTexture("DE.HdrPost", desc(rgba16, rgba16, rgba16, kPostClear, false));
        m_ids[kAo]       = targets.registerTexture("DE.GBuffer.Ao", desc(DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, kAoClear, false));
        for (const Rg::TargetId& id : m_ids)
        {
            if (!id.valid())
            {
                reset();
                return false;
            }
        }
        if (!rebuildViews(depthSrvCpu))
        {
            reset();
            return false;
        }
        DE_LOG_INFO(LogCategory::Render, "SceneBuffers: HDR {}x{} + G-buffer + velocity + post (graph targets)", width(), height());
        return true;
    }

    bool SceneBuffers::rebuildViews(D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpu)
    {
        ID3D12Device* device = m_targets ? m_targets->device() : nullptr;
        if (!device)
            return false;
        // Called with the GPU idle (create, or Renderer::resize after its drain), so the old table can go now.
        m_lightingHeap.Reset();
        D3D12_DESCRIPTOR_HEAP_DESC lightDesc{};
        lightDesc.NumDescriptors = kLightingCount;
        lightDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        lightDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!checkHr(device->CreateDescriptorHeap(&lightDesc, IID_PPV_ARGS(&m_lightingHeap)), "SceneBuffers CreateDescriptorHeap lighting"))
            return false;
        m_lightingHeap->SetName(L"DE.SceneBuffers.Lighting");
        m_lightingGpu = m_lightingHeap->GetGPUDescriptorHandleForHeapStart();
        m_heightGpu   = m_lightingGpu;
        m_heightGpu.ptr += static_cast<SIZE_T>(kLightingHeight) * m_srvIncr;
        m_aoGpu = m_lightingGpu;
        m_aoGpu.ptr += static_cast<SIZE_T>(kLightingAo) * m_srvIncr;
        m_iblGpu = m_lightingGpu;
        m_iblGpu.ptr += static_cast<SIZE_T>(kLightingIblIrradiance) * m_srvIncr;
        m_ssrGpu = m_lightingGpu;
        m_ssrGpu.ptr += static_cast<SIZE_T>(kLightingSsr) * m_srvIncr;
        m_localShadowGpu = m_lightingGpu;
        m_localShadowGpu.ptr += static_cast<SIZE_T>(kLightingLocalShadow) * m_srvIncr;
        // GTAO/SSR views from before the resize are gone; their pipelines set fresh ones every frame.
        m_lightingAoCpu  = {};
        m_lightingSsrCpu = {};
        packLightingHeap(device, depthSrvCpu);
        return true;
    }

    void SceneBuffers::packLightingHeap(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpu)
    {
        if (!device || !m_lightingHeap || !albedo() || !attrib() || !ao())
            return;

        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_lightingHeap->GetCPUDescriptorHandleForHeapStart();

        const D3D12_SHADER_RESOURCE_VIEW_DESC attribUnorm = tex2dSrv(DXGI_FORMAT_R8G8B8A8_UNORM);
        applyLightingAlbedoView(device);
        device->CreateShaderResourceView(attrib(), &attribUnorm, offsetHandle(cpu, kLightingAttrib, m_srvIncr));

        if (depthSrvCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingDepth, m_srvIncr), depthSrvCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_shadowCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingShadow, m_srvIncr), m_shadowCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_heightCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingHeight, m_srvIncr), m_heightCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const D3D12_CPU_DESCRIPTOR_HANDLE aoSrc = m_lightingAoCpu.ptr != 0 ? m_lightingAoCpu : aoSrvCpu();
        if (aoSrc.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingAo, m_srvIncr), aoSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_iblIrrCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingIblIrradiance, m_srvIncr), m_iblIrrCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_iblPrefCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingIblPrefilter, m_srvIncr), m_iblPrefCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_iblLutCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingIblBrdfLut, m_srvIncr), m_iblLutCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_lightingSsrCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingSsr, m_srvIncr), m_lightingSsrCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_localShadowCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingLocalShadow, m_srvIncr), m_localShadowCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_localShadowRecordsCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingLocalShadowRecords, m_srvIncr), m_localShadowRecordsCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void SceneBuffers::setLocalShadowSrvs(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE arraySrv, D3D12_CPU_DESCRIPTOR_HANDLE recordsSrv)
    {
        m_localShadowCpu         = arraySrv;
        m_localShadowRecordsCpu  = recordsSrv;
        if (!device || !m_lightingHeap)
            return;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_lightingHeap->GetCPUDescriptorHandleForHeapStart();
        if (arraySrv.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingLocalShadow, m_srvIncr), arraySrv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (recordsSrv.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingLocalShadowRecords, m_srvIncr), recordsSrv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void SceneBuffers::setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu)
    {
        m_shadowCpu = shadowCpu;
        if (!device || !m_lightingHeap || shadowCpu.ptr == 0)
            return;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_lightingHeap->GetCPUDescriptorHandleForHeapStart();
        device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingShadow, m_srvIncr), shadowCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void SceneBuffers::setLightingAoSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE aoCpu)
    {
        m_lightingAoCpu = aoCpu;
        if (!device || !m_lightingHeap || aoCpu.ptr == 0)
            return;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_lightingHeap->GetCPUDescriptorHandleForHeapStart();
        device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingAo, m_srvIncr), aoCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void SceneBuffers::setLightingSsrSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE ssrCpu)
    {
        m_lightingSsrCpu = ssrCpu;
        if (!device || !m_lightingHeap || ssrCpu.ptr == 0)
            return;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_lightingHeap->GetCPUDescriptorHandleForHeapStart();
        device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingSsr, m_srvIncr), ssrCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void SceneBuffers::applyLightingAlbedoView(ID3D12Device* device)
    {
        if (!device || !m_lightingHeap || !albedo())
            return;
        D3D12_CPU_DESCRIPTOR_HANDLE dest = offsetHandle(m_lightingHeap->GetCPUDescriptorHandleForHeapStart(), kLightingAlbedo, m_srvIncr);
        if (m_lightingAlbedoRaw)
        {
            if (albedoSrvCpu().ptr != 0)
                device->CopyDescriptorsSimple(1, dest, albedoSrvCpu(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return;
        }
        const D3D12_SHADER_RESOURCE_VIEW_DESC albedoSrgb = tex2dSrv(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        device->CreateShaderResourceView(albedo(), &albedoSrgb, dest);
    }

    void SceneBuffers::setLightingAlbedoRaw(ID3D12Device* device, bool raw)
    {
        m_lightingAlbedoRaw = raw;
        applyLightingAlbedoView(device);
    }

    void SceneBuffers::setHeightSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE heightCpu)
    {
        m_heightCpu = heightCpu;
        if (!device || !m_lightingHeap || heightCpu.ptr == 0)
            return;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_lightingHeap->GetCPUDescriptorHandleForHeapStart();
        device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingHeight, m_srvIncr), heightCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_heightGpu = m_lightingGpu;
        m_heightGpu.ptr += static_cast<SIZE_T>(kLightingHeight) * m_srvIncr;
    }

    void SceneBuffers::setIblSrvs(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE irradianceCpu, D3D12_CPU_DESCRIPTOR_HANDLE prefilterCpu,
                                  D3D12_CPU_DESCRIPTOR_HANDLE brdfLutCpu)
    {
        m_iblIrrCpu  = irradianceCpu;
        m_iblPrefCpu = prefilterCpu;
        m_iblLutCpu  = brdfLutCpu;
        if (!device || !m_lightingHeap)
            return;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_lightingHeap->GetCPUDescriptorHandleForHeapStart();
        if (irradianceCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingIblIrradiance, m_srvIncr), irradianceCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (prefilterCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingIblPrefilter, m_srvIncr), prefilterCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (brdfLutCpu.ptr != 0)
            device->CopyDescriptorsSimple(1, offsetHandle(cpu, kLightingIblBrdfLut, m_srvIncr), brdfLutCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_iblGpu = m_lightingGpu;
        m_iblGpu.ptr += static_cast<SIZE_T>(kLightingIblIrradiance) * m_srvIncr;
    }

} // namespace Dark
