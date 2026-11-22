#pragma once

#include "Render/RenderTargetPool.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    class GraphTargets;

    // Offscreen 3D color + G-buffer. Owned by Renderer. Does not record command lists.
    // The textures are graph-registered persistent targets (GraphTargets, render-graph plan Phase 5):
    // Renderer::resize recreates them after its one GPU drain and then calls rebuildViews().
    class SceneBuffers
    {
    public:
        static constexpr UINT kLightingAlbedo = 0;
        static constexpr UINT kLightingAttrib = 1;
        static constexpr UINT kLightingDepth  = 2;
        static constexpr UINT kLightingShadow = 3;
        static constexpr UINT kLightingHeight = 4;
        static constexpr UINT kLightingAo            = 5;
        static constexpr UINT kLightingIblIrradiance = 6;
        static constexpr UINT kLightingIblPrefilter  = 7;
        static constexpr UINT kLightingIblBrdfLut    = 8;
        static constexpr UINT kLightingSsr           = 9;
        static constexpr UINT kLightingLocalShadow   = 10;
        static constexpr UINT kLightingLocalShadowRecords = 11;
        static constexpr UINT kLightingCount         = 12;

        SceneBuffers() = default;
        ~SceneBuffers();

        SceneBuffers(const SceneBuffers&)            = delete;
        SceneBuffers& operator=(const SceneBuffers&) = delete;

        // G-buffer ClearRTV colors. Must match the optimized clear value the target is registered with.
        static constexpr float kAlbedoClear[4]   = { 0.0f, 0.0f, 0.0f, 0.0f };
        static constexpr float kAttribClear[4]   = { 0.5f, 0.5f, 1.0f, 0.0f };
        static constexpr float kVelocityClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        static constexpr float kPostClear[4]     = { 0.0f, 0.0f, 0.0f, 1.0f };
        static constexpr float kAoClear[4]       = { 1.0f, 0.0f, 0.0f, 0.0f };

        // hdrClear is display-referred (same space as Renderer::setClearColor / swapchain). RGB is converted to linear for the HDR target.
        // HDR, G-buffer, velocity, post and AO at the output size of `targets`. The TAA history is a
        // graph history pair (FrameComposer::history), not a scene buffer.
        bool create(GraphTargets& targets, D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpu, const float hdrClear[4]);
        // After GraphTargets::resizeAfterDrain (GPU idle): new lighting table, states back to RENDER_TARGET.
        bool rebuildViews(D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpu);
        void reset();

        bool     valid() const;
        uint32_t width() const;
        uint32_t height() const;
        bool     matches(uint32_t width, uint32_t height) const
        {
            return valid() && this->width() == width && this->height() == height;
        }

        ID3D12Resource*             hdr() const { return resource(kHdr); }
        ID3D12Resource*             albedo() const { return resource(kAlbedo); }
        ID3D12Resource*             attrib() const { return resource(kAttrib); }
        ID3D12Resource*             velocity() const { return resource(kVelocity); }
        ID3D12Resource*             post() const { return resource(kPost); }
        ID3D12Resource*             ao() const { return resource(kAo); }
        D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv() const { return rtv(kHdr); }
        D3D12_CPU_DESCRIPTOR_HANDLE albedoRtv() const { return rtv(kAlbedo); }
        D3D12_CPU_DESCRIPTOR_HANDLE attribRtv() const { return rtv(kAttrib); }
        D3D12_CPU_DESCRIPTOR_HANDLE velocityRtv() const { return rtv(kVelocity); }
        D3D12_CPU_DESCRIPTOR_HANDLE postRtv() const { return rtv(kPost); }
        D3D12_CPU_DESCRIPTOR_HANDLE aoRtv() const { return rtv(kAo); }
        D3D12_CPU_DESCRIPTOR_HANDLE hdrSrvCpu() const { return srv(kHdr); }
        D3D12_CPU_DESCRIPTOR_HANDLE velocitySrvCpu() const { return srv(kVelocity); }
        D3D12_CPU_DESCRIPTOR_HANDLE postSrvCpu() const { return srv(kPost); }
        // Graph-owned CPU views — legal CopyDescriptors sources (shader-visible lighting heap is not).
        // Overlay albedo is raw UNORM (byte dump). Lighting heap slot 0 is UNORM_SRGB.
        D3D12_CPU_DESCRIPTOR_HANDLE albedoSrvCpu() const { return srv(kAlbedo); }
        D3D12_CPU_DESCRIPTOR_HANDLE attribSrvCpu() const { return srv(kAttrib); }
        D3D12_CPU_DESCRIPTOR_HANDLE aoSrvCpu() const { return srv(kAo); }
        D3D12_CPU_DESCRIPTOR_HANDLE lightingAoCpu() const { return m_lightingAoCpu; }
        D3D12_CPU_DESCRIPTOR_HANDLE lightingSsrCpu() const { return m_lightingSsrCpu; }
        D3D12_GPU_DESCRIPTOR_HANDLE lightingTableGpu() const { return m_lightingGpu; }
        D3D12_GPU_DESCRIPTOR_HANDLE heightTableGpu() const { return m_heightGpu; }
        D3D12_GPU_DESCRIPTOR_HANDLE aoTableGpu() const { return m_aoGpu; }
        D3D12_GPU_DESCRIPTOR_HANDLE iblTableGpu() const { return m_iblGpu; }
        D3D12_GPU_DESCRIPTOR_HANDLE ssrTableGpu() const { return m_ssrGpu; }
        D3D12_GPU_DESCRIPTOR_HANDLE localShadowTableGpu() const { return m_localShadowGpu; }
        ID3D12DescriptorHeap*       lightingHeap() const { return m_lightingHeap.Get(); }
        const float*                hdrClear() const { return m_hdrClear; }

        void transitionHdr(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after) { transition(cmd, kHdr, after); }
        void transitionAlbedo(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after) { transition(cmd, kAlbedo, after); }
        void transitionAttrib(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after) { transition(cmd, kAttrib, after); }
        void transitionVelocity(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after) { transition(cmd, kVelocity, after); }
        void transitionPost(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after) { transition(cmd, kPost, after); }
        void transitionAo(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after) { transition(cmd, kAo, after); }

        void setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);
        void setLocalShadowSrvs(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE arraySrv, D3D12_CPU_DESCRIPTOR_HANDLE recordsSrv);
        void setLightingAoSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE aoCpu);
        void setLightingSsrSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE ssrCpu);
        void setHeightSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE heightCpu);
        void setIblSrvs(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE irradianceCpu, D3D12_CPU_DESCRIPTOR_HANDLE prefilterCpu,
                        D3D12_CPU_DESCRIPTOR_HANDLE brdfLutCpu);
        void setLightingAlbedoRaw(ID3D12Device* device, bool raw);
        void packLightingHeap(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpu);

    private:
        enum Slot : uint32_t
        {
            kHdr,
            kAlbedo,
            kAttrib,
            kVelocity,
            kPost,
            kAo,
            kSlotCount,
        };

        ID3D12Resource*             resource(Slot slot) const;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv(Slot slot) const;
        D3D12_CPU_DESCRIPTOR_HANDLE srv(Slot slot) const;
        void                        applyLightingAlbedoView(ID3D12Device* device);
        void                        transition(ID3D12GraphicsCommandList* cmd, Slot slot, D3D12_RESOURCE_STATES after);

        GraphTargets*                m_targets = nullptr;
        Rg::TargetId                 m_ids[kSlotCount];
        ComPtr<ID3D12DescriptorHeap> m_lightingHeap;
        D3D12_GPU_DESCRIPTOR_HANDLE  m_lightingGpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_heightGpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_aoGpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_iblGpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_ssrGpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_localShadowGpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_shadowCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_localShadowCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_localShadowRecordsCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_lightingAoCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_lightingSsrCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_heightCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_iblIrrCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_iblPrefCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_iblLutCpu{};
        bool                         m_lightingAlbedoRaw = false;
        UINT                         m_srvIncr     = 0;
        float                        m_hdrClear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    };

} // namespace Dark
