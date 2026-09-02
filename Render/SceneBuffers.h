#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // Offscreen 3D color (and later G-buffer). Owned by Renderer. Does not record command lists.
    // PR1 creates HDR only. PR2 adds albedo/attrib + lighting SRV heap.
    class SceneBuffers
    {
    public:
        SceneBuffers() = default;

        SceneBuffers(const SceneBuffers&)            = delete;
        SceneBuffers& operator=(const SceneBuffers&) = delete;

        bool createHdr(ID3D12Device* device, uint32_t width, uint32_t height);
        void reset();

        bool     valid() const { return m_hdr != nullptr; }
        uint32_t width() const { return m_width; }
        uint32_t height() const { return m_height; }
        bool     matches(uint32_t width, uint32_t height) const { return valid() && m_width == width && m_height == height; }

        ID3D12Resource*             hdr() const { return m_hdr.Get(); }
        D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv() const { return m_hdrRtv; }
        D3D12_CPU_DESCRIPTOR_HANDLE hdrSrvCpu() const { return m_hdrSrvCpu; }
        D3D12_RESOURCE_STATES       hdrState() const { return m_hdrState; }

        void transitionHdr(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);

    private:
        ComPtr<ID3D12Resource>       m_hdr;
        ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
        ComPtr<ID3D12DescriptorHeap> m_hdrSrvHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE  m_hdrRtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_hdrSrvCpu{};
        D3D12_RESOURCE_STATES        m_hdrState = D3D12_RESOURCE_STATE_COMMON;
        uint32_t                     m_width    = 0;
        uint32_t                     m_height   = 0;
    };

} // namespace Dark
