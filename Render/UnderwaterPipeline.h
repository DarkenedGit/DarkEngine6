#pragma once

#include "Render/GraphTargets.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;

    using Microsoft::WRL::ComPtr;

    // Fullscreen Beer-Lambert on scene HDR. amount 0 skips the draw.
    struct UnderwaterFrame
    {
        float invViewProj[16]{};
        float cameraPos[3]{};
        float waterLevel = 0.0f;
        float amount     = 0.0f;
    };

    class UnderwaterPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;

        UnderwaterPipeline() = default;
        ~UnderwaterPipeline();
        UnderwaterPipeline(const UnderwaterPipeline&)            = delete;
        UnderwaterPipeline& operator=(const UnderwaterPipeline&) = delete;
        void release();

        bool create(ID3D12Device* device);
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const UnderwaterFrame& frame);

        bool isValid() const { return m_pso != nullptr && m_rootSignature != nullptr; }

    private:
        static constexpr UINT kBufferedFrames = 2;
        static constexpr UINT kSrvPerFrame    = 2; // HDR copy + depth
        static constexpr UINT kConstantCount  = 21;

        bool ensureCopy(Renderer& renderer);

        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_pso;
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE  m_cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_gpu{};
        UINT                         m_srvIncr = 0;
        GraphTargets*                m_targets = nullptr; // set by the first ensureCopy
        GraphTarget                  m_copy;              // graph target (render-graph plan Phase 5)
    };

} // namespace Dark
