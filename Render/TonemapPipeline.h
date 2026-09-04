#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;

    using Microsoft::WRL::ComPtr;

    // HDR → UNORM swap chain. blur/fade 0 is a straight copy or ACES.
    struct TonemapSettings
    {
        float exposure     = 1.0f;
        float mode         = 0.0f; // 0 saturate, 1 Narkowicz ACES
        float blur         = 0.0f; // 0-1, max CoC
        float fade         = 0.0f; // 0-1, multiply toward black
        float focusZ       = 8.0f;
        float focusRange   = 14.0f;
        float nearZ        = 0.18f;
        float farZ         = 2000.0f;
        float uniformBlur  = 0.0f; // 0 depth CoC, 1 full-frame defocus
        bool  usePostHdr   = false; // motion-blur dest instead of scene HDR
    };

    class TonemapPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;

        TonemapPipeline() = default;

        bool create(ID3D12Device* device);
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, float mode, float exposure) const;
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const TonemapSettings& settings) const;

        bool isValid() const { return m_pso != nullptr; }

    private:
        static constexpr UINT kBufferedFrames = 2;
        static constexpr UINT kSrvPerFrame    = 2; // HDR + depth
        static constexpr UINT kConstantCount  = 12;

        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_pso;
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE  m_cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_gpu{};
        UINT                         m_srvIncr = 0;
    };

} // namespace Dark
