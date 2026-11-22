#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

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
        float nightSight   = 0.0f; // 0 off, 1 scotopic grade in the shadows
        bool  usePostHdr   = false; // the post target (TAA / motion blur output) instead of scene HDR
    };

    // What the graph pass binds (Phase 6 part 2): the back buffer and one descriptor-ring table.
    // draw() records no barrier.
    struct TonemapTargets
    {
        D3D12_GPU_DESCRIPTOR_HANDLE srvTable{}; // HDR source (scene or post), depth
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        uint32_t                    width  = 0;
        uint32_t                    height = 0;
    };

    class TonemapPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;
        static constexpr UINT kSrvCount      = 2; // TonemapTargets::srvTable

        TonemapPipeline() = default;

        bool create(ID3D12Device* device);
        void draw(ID3D12GraphicsCommandList* cmd, const TonemapSettings& settings, const TonemapTargets& targets) const;

        bool isValid() const { return m_pso != nullptr; }

    private:
        static constexpr UINT kConstantCount = 12;

        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pso;
    };

} // namespace Dark
