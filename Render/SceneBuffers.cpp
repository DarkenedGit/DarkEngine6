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
        m_viewHeap.Reset();
        m_localShadowCpu        = {};
        m_localShadowRecordsCpu = {};
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
        return hdr() != nullptr && albedo() != nullptr && ao() != nullptr && surfaceTag() != nullptr;
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

    bool SceneBuffers::create(GraphTargets& targets, const float hdrClear[4])
    {
        const D3D12_CPU_DESCRIPTOR_HANDLE savedLocalShadow = m_localShadowCpu;
        const D3D12_CPU_DESCRIPTOR_HANDLE savedLocalRecords = m_localShadowRecordsCpu;
        const D3D12_CPU_DESCRIPTOR_HANDLE savedHeight = m_heightCpu;
        const D3D12_CPU_DESCRIPTOR_HANDLE savedIblIrr = m_iblIrrCpu;
        const D3D12_CPU_DESCRIPTOR_HANDLE savedIblPref = m_iblPrefCpu;
        const D3D12_CPU_DESCRIPTOR_HANDLE savedIblLut = m_iblLutCpu;
        reset();
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
        m_ids[kAo]         = targets.registerTexture("DE.GBuffer.Ao", desc(DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, kAoClear, false));
        m_ids[kSurfaceTag] = targets.registerTexture("DE.GBuffer.SurfaceTag", desc(DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM, kSurfaceTagClear, false));
        for (const Rg::TargetId& id : m_ids)
        {
            if (!id.valid())
            {
                reset();
                return false;
            }
        }
        if (!rebuildViews())
        {
            reset();
            return false;
        }
        DE_LOG_INFO(LogCategory::Render, "SceneBuffers: HDR {}x{} + G-buffer + velocity + post + SurfaceTag (graph targets)", width(), height());
        return true;
    }

    bool SceneBuffers::rebuildViews()
    {
        ID3D12Device* device = m_targets ? m_targets->device() : nullptr;
        if (!device)
            return false;
        // CPU only: the lighting passes copy from it into ring tables when they record, so the old
        // heap can go as soon as the new one exists.
        if (!m_viewHeap)
        {
            D3D12_DESCRIPTOR_HEAP_DESC viewDesc{};
            viewDesc.NumDescriptors = kViewCount;
            viewDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            viewDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (!checkHr(device->CreateDescriptorHeap(&viewDesc, IID_PPV_ARGS(&m_viewHeap)), "SceneBuffers CreateDescriptorHeap lighting views"))
                return false;
            m_viewHeap->SetName(L"DE.SceneBuffers.LightingViews");

            // Null SRVs for views a host has not set (no shadows, no local shadows yet, ...).
            D3D12_SHADER_RESOURCE_VIEW_DESC nullTex = tex2dSrv(DXGI_FORMAT_R8G8B8A8_UNORM);
            device->CreateShaderResourceView(nullptr, &nullTex, view(kViewNullTex2D));
            D3D12_SHADER_RESOURCE_VIEW_DESC nullArray{};
            nullArray.Format                   = DXGI_FORMAT_R32_FLOAT;
            nullArray.ViewDimension            = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            nullArray.Shader4ComponentMapping  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            nullArray.Texture2DArray.MipLevels = 1;
            nullArray.Texture2DArray.ArraySize = 1;
            device->CreateShaderResourceView(nullptr, &nullArray, view(kViewNullTex2DArray));
            D3D12_SHADER_RESOURCE_VIEW_DESC nullCube{};
            nullCube.Format                  = DXGI_FORMAT_R16G16B16A16_FLOAT;
            nullCube.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURECUBE;
            nullCube.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            nullCube.TextureCube.MipLevels   = 1;
            device->CreateShaderResourceView(nullptr, &nullCube, view(kViewNullCube));
            D3D12_SHADER_RESOURCE_VIEW_DESC nullBuffer{};
            nullBuffer.Format                     = DXGI_FORMAT_UNKNOWN;
            nullBuffer.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
            nullBuffer.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            nullBuffer.Buffer.NumElements         = 1;
            nullBuffer.Buffer.StructureByteStride = 16;
            device->CreateShaderResourceView(nullptr, &nullBuffer, view(kViewNullBuffer));
        }
        applyLightingAlbedoView();
        return true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE SceneBuffers::view(ViewSlot slot) const
    {
        if (!m_viewHeap)
            return {};
        const UINT incr = m_srvIncr;
        return offsetHandle(m_viewHeap->GetCPUDescriptorHandleForHeapStart(), slot, incr);
    }

    LightingViews SceneBuffers::lightingViews() const
    {
        const auto    orNull = [](D3D12_CPU_DESCRIPTOR_HANDLE h, D3D12_CPU_DESCRIPTOR_HANDLE fallback) { return h.ptr != 0 ? h : fallback; };
        LightingViews v;
        v.albedo             = m_viewHeap ? view(kViewAlbedo) : D3D12_CPU_DESCRIPTOR_HANDLE{};
        v.attrib             = attribSrvCpu(); // the graph view is already UNORM
        v.shadow             = m_viewHeap ? view(kViewNullTex2DArray) : D3D12_CPU_DESCRIPTOR_HANDLE{};
        v.height             = orNull(m_heightCpu, view(kViewNullTex2D));
        v.iblIrradiance      = orNull(m_iblIrrCpu, view(kViewNullCube));
        v.iblPrefilter       = orNull(m_iblPrefCpu, view(kViewNullCube));
        v.iblBrdfLut         = orNull(m_iblLutCpu, view(kViewNullTex2D));
        v.localShadow        = orNull(m_localShadowCpu, view(kViewNullTex2DArray));
        v.localShadowRecords = orNull(m_localShadowRecordsCpu, view(kViewNullBuffer));
        return v;
    }

    void SceneBuffers::applyLightingAlbedoView()
    {
        ID3D12Device* device = m_targets ? m_targets->device() : nullptr;
        if (!device || !m_viewHeap || !albedo())
            return;
        const D3D12_CPU_DESCRIPTOR_HANDLE dest = view(kViewAlbedo);
        if (m_lightingAlbedoRaw)
        {
            if (albedoSrvCpu().ptr != 0)
                device->CopyDescriptorsSimple(1, dest, albedoSrvCpu(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return;
        }
        const D3D12_SHADER_RESOURCE_VIEW_DESC albedoSrgb = tex2dSrv(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        device->CreateShaderResourceView(albedo(), &albedoSrgb, dest);
    }

    void SceneBuffers::setLightingAlbedoRaw(bool raw)
    {
        m_lightingAlbedoRaw = raw;
        applyLightingAlbedoView();
    }

} // namespace Dark
