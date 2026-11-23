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
        // The HDR copy the pass samples, registered on first use (only underwater frames pay for
        // it). False when the target registry or the copy is missing.
        bool                        ensureCopy(Renderer& renderer);
        ID3D12Resource*             copyResource() const;
        D3D12_CPU_DESCRIPTOR_HANDLE copySrv() const;
        // table: a two-SRV ring table, the HDR copy (t0) and scene depth (t1). HDR is bound.
        void draw(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE table, const UnderwaterFrame& frame) const;

        bool isValid() const { return m_pso != nullptr && m_rootSignature != nullptr; }

    private:
        static constexpr UINT kSrvCount      = 2; // HDR copy + depth
        static constexpr UINT kConstantCount = 21;

        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pso;
        GraphTargets*               m_targets = nullptr; // set by the first ensureCopy
        GraphTarget                 m_copy;
    };

} // namespace Dark
