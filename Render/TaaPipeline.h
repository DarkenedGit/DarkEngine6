#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    struct TaaSettings
    {
        float invViewProj[16]{};
        float prevViewProj[16]{};
        float blend = 0.1f; // current-frame weight
        bool  reset = false;
    };

    // What the graph pass binds (render-graph plan Phase 6 part 2): the post HDR target and this
    // frame's TAA history (SV_Target1, no Post->History copy), and one descriptor-ring table. The
    // graph has every resource in its layout already; draw() records no barrier.
    struct TaaTargets
    {
        D3D12_GPU_DESCRIPTOR_HANDLE srvTable{}; // scene HDR, history prev, velocity, depth
        D3D12_CPU_DESCRIPTOR_HANDLE postRtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE historyRtv{};
        uint32_t                    width  = 0;
        uint32_t                    height = 0;
    };

    class TaaPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;
        static constexpr UINT kSrvCount      = 4; // TaaTargets::srvTable

        TaaPipeline() = default;

        bool create(ID3D12Device* device);
        void draw(ID3D12GraphicsCommandList* cmd, const TaaSettings& settings, const TaaTargets& targets) const;

        bool isValid() const { return m_pso != nullptr; }

    private:
        static constexpr UINT kConstantCount = 36;

        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pso;
    };

} // namespace Dark
