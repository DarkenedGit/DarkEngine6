#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    struct MotionBlurSettings
    {
        float invViewProj[16]{};
        float prevViewProj[16]{};
        float strength   = 1.0f;
        float maxPixels  = 40.0f;
        bool  readPost   = false; // true: sample TAA/post, write scene HDR
    };

    // What the graph pass binds (Phase 6 part 2): the destination (scene HDR when reading post,
    // post otherwise) and one descriptor-ring table. draw() records no barrier.
    struct MotionBlurTargets
    {
        D3D12_GPU_DESCRIPTOR_HANDLE srvTable{}; // source colour, velocity, depth
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        uint32_t                    width  = 0;
        uint32_t                    height = 0;
    };

    class MotionBlurPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;
        static constexpr UINT kSrvCount      = 3; // MotionBlurTargets::srvTable

        MotionBlurPipeline() = default;

        bool create(ID3D12Device* device);
        void draw(ID3D12GraphicsCommandList* cmd, const MotionBlurSettings& settings, const MotionBlurTargets& targets) const;

        bool isValid() const { return m_pso != nullptr; }

    private:
        static constexpr UINT kConstantCount = 36;

        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pso;
    };

} // namespace Dark
