#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // Offscreen 3D color + optional G-buffer. Owned by Renderer. Does not record command lists.
    class SceneBuffers
    {
    public:
        static constexpr UINT kRtvHdr      = 0;
        static constexpr UINT kRtvAlbedo   = 1;
        static constexpr UINT kRtvAttrib   = 2;
        static constexpr UINT kRtvVelocity = 3;
        static constexpr UINT kRtvPost     = 4;
        static constexpr UINT kRtvHistory  = 5;
        static constexpr UINT kRtvCountGBuffer = 6;
        static constexpr UINT kLightingAlbedo = 0;
        static constexpr UINT kLightingAttrib = 1;
        static constexpr UINT kLightingDepth  = 2;
        static constexpr UINT kLightingShadow = 3;
        static constexpr UINT kLightingCount  = 4;

        SceneBuffers() = default;

        SceneBuffers(const SceneBuffers&)            = delete;
        SceneBuffers& operator=(const SceneBuffers&) = delete;

        // G-buffer ClearRTV colors. Must match the D3D12_CLEAR_VALUE used at CreateCommittedResource.
        static constexpr float kAlbedoClear[4]   = { 0.0f, 0.0f, 0.0f, 1.0f };
        static constexpr float kAttribClear[4]   = { 0.5f, 0.5f, 1.0f, 0.0f };
        static constexpr float kVelocityClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        static constexpr float kPostClear[4]     = { 0.0f, 0.0f, 0.0f, 1.0f };

        bool create(ID3D12Device* device, uint32_t width, uint32_t height, bool gbuffer, D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpu, const float hdrClear[4]);
        void reset();

        bool     valid() const { return m_hdr != nullptr; }
        bool     hasGBuffer() const { return m_albedo != nullptr; }
        uint32_t width() const { return m_width; }
        uint32_t height() const { return m_height; }
        bool     matches(uint32_t width, uint32_t height, bool gbuffer) const
        {
            return valid() && m_width == width && m_height == height && hasGBuffer() == gbuffer;
        }

        ID3D12Resource*             hdr() const { return m_hdr.Get(); }
        ID3D12Resource*             albedo() const { return m_albedo.Get(); }
        ID3D12Resource*             attrib() const { return m_attrib.Get(); }
        ID3D12Resource*             velocity() const { return m_velocity.Get(); }
        ID3D12Resource*             post() const { return m_post.Get(); }
        ID3D12Resource*             history() const { return m_history.Get(); }
        D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv() const { return m_hdrRtv; }
        D3D12_CPU_DESCRIPTOR_HANDLE albedoRtv() const { return m_albedoRtv; }
        D3D12_CPU_DESCRIPTOR_HANDLE attribRtv() const { return m_attribRtv; }
        D3D12_CPU_DESCRIPTOR_HANDLE velocityRtv() const { return m_velocityRtv; }
        D3D12_CPU_DESCRIPTOR_HANDLE postRtv() const { return m_postRtv; }
        D3D12_CPU_DESCRIPTOR_HANDLE hdrSrvCpu() const { return m_hdrSrvCpu; }
        D3D12_CPU_DESCRIPTOR_HANDLE velocitySrvCpu() const { return m_velocitySrvCpu; }
        D3D12_CPU_DESCRIPTOR_HANDLE postSrvCpu() const { return m_postSrvCpu; }
        D3D12_CPU_DESCRIPTOR_HANDLE historySrvCpu() const { return m_historySrvCpu; }
        D3D12_CPU_DESCRIPTOR_HANDLE historyRtv() const { return m_historyRtv; }
        D3D12_CPU_DESCRIPTOR_HANDLE albedoSrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE attribSrvCpu() const;
        D3D12_GPU_DESCRIPTOR_HANDLE lightingTableGpu() const { return m_lightingGpu; }
        ID3D12DescriptorHeap*       lightingHeap() const { return m_lightingHeap.Get(); }
        const float*                hdrClear() const { return m_hdrClear; }
        D3D12_RESOURCE_STATES       hdrState() const { return m_hdrState; }
        D3D12_RESOURCE_STATES       albedoState() const { return m_albedoState; }
        D3D12_RESOURCE_STATES       attribState() const { return m_attribState; }
        D3D12_RESOURCE_STATES       velocityState() const { return m_velocityState; }
        D3D12_RESOURCE_STATES       postState() const { return m_postState; }
        D3D12_RESOURCE_STATES       historyState() const { return m_historyState; }

        void transitionHdr(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        void transitionAlbedo(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        void transitionAttrib(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        void transitionVelocity(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        void transitionPost(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        void transitionHistory(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);

        void setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);
        void packLightingHeap(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpu);

    private:
        bool createColorTarget(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format, const float clear[4], const wchar_t* name, ComPtr<ID3D12Resource>& out, D3D12_RESOURCE_STATES& state);
        static void transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res, D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES after);

        ComPtr<ID3D12Resource>       m_hdr;
        ComPtr<ID3D12Resource>       m_albedo;
        ComPtr<ID3D12Resource>       m_attrib;
        ComPtr<ID3D12Resource>       m_velocity;
        ComPtr<ID3D12Resource>       m_post;
        ComPtr<ID3D12Resource>       m_history;
        ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
        ComPtr<ID3D12DescriptorHeap> m_hdrSrvHeap;
        ComPtr<ID3D12DescriptorHeap> m_velocitySrvHeap;
        ComPtr<ID3D12DescriptorHeap> m_postSrvHeap;
        ComPtr<ID3D12DescriptorHeap> m_historySrvHeap;
        ComPtr<ID3D12DescriptorHeap> m_lightingHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE  m_hdrRtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_albedoRtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_attribRtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_velocityRtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_postRtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_historyRtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_hdrSrvCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_velocitySrvCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_postSrvCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_historySrvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_lightingGpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_lightingCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_shadowCpu{};
        D3D12_RESOURCE_STATES        m_hdrState      = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES        m_albedoState   = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES        m_attribState   = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES        m_velocityState = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES        m_postState     = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES        m_historyState  = D3D12_RESOURCE_STATE_COMMON;
        UINT                         m_rtvIncr     = 0;
        UINT                         m_srvIncr     = 0;
        uint32_t                     m_width       = 0;
        uint32_t                     m_height      = 0;
        float                        m_hdrClear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    };

} // namespace Dark
