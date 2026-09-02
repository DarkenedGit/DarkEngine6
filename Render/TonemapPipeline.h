#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;

    using Microsoft::WRL::ComPtr;

    // Fullscreen HDR → UNORM swap chain. mode=0 saturate-copy (PR1 default); mode=1 Narkowicz ACES.
    class TonemapPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;

        TonemapPipeline() = default;

        bool create(ID3D12Device* device);
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, float mode, float exposure) const;

        bool isValid() const { return m_pso != nullptr; }

    private:
        static constexpr UINT kBufferedFrames = 2;

        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_pso;
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE  m_cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_gpu{};
        UINT                         m_srvIncr = 0;
    };

} // namespace Dark
