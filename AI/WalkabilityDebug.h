#pragma once

#include "Math/Matrix4f.h"
#include "AI/DebugColorMesh.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{
    class Renderer;

    // GPU mesh of the hunter walk bake (AI::walkabilityMeshSource builds the source). Drawn when
    // DebugRenderState::showWalkability is set.
    class WalkabilityDebug
    {
    public:
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const DebugMeshSource& source, const Math::Matrix4f& viewProj);

    private:
        bool ensurePipeline(Renderer& renderer);
        void sync(Renderer& renderer, const DebugMeshSource& source);
        void releaseMesh(Renderer& renderer);

        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
        Microsoft::WRL::ComPtr<ID3D12Resource>      m_vb;
        Microsoft::WRL::ComPtr<ID3D12Resource>      m_ib;
        D3D12_VERTEX_BUFFER_VIEW                    m_vbv{};
        D3D12_INDEX_BUFFER_VIEW                     m_ibv{};
        uint32_t                                    m_indexCount = 0;
        uint32_t                                    m_serial     = 0;
        // Set when the pipeline fails to build. Cleared when the overlay is turned off, so turning it
        // back on (after fixing Walkability.hlsl) tries once more. No per-frame retries or log spam.
        bool m_pipelineFailed = false;
    };

} // namespace Dark
