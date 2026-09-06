#pragma once

#include "Math/Matrix4f.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;
    class Mesh;
    class LocalLightGpuList;
    class Camera3D;
    class World;
    struct LightingConstants;

    using Microsoft::WRL::ComPtr;

    struct LocalLightPassConstants
    {
        float    invViewProj[16];
        float    viewProj[16];
        float    cameraPos[3];
        float    fogDensity;
        float    fogColor[3];
        float    lighting;
        uint32_t baseIndex;
        uint32_t lightIndex;
        float    viewportW;
        float    viewportH;
    };
    static_assert(sizeof(LocalLightPassConstants) == 44 * sizeof(float), "local light root constants");

    class LocalLightVolumePipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrvTable  = 1;
        static constexpr UINT kRootLightsSrv = 2;
        static constexpr UINT kRootWorldSrv  = 3;

        LocalLightVolumePipeline() = default;

        bool create(ID3D12Device* device);

        void drawInstanced(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const LocalLightGpuList& gpuList, const Mesh& volume, uint32_t instanceCount,
                           uint32_t baseIndex, const LocalLightPassConstants& cb) const;

        void drawFullscreenScissor(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const LocalLightGpuList& gpuList, const D3D12_RECT& scissor,
                                   uint32_t lightIndex, const LocalLightPassConstants& cb) const;

        // Gather + upload + instanced sphere/cone + inside fullscreen. Fog/camera copied from `lighting`.
        // Skips if pipeline, GPU list, or either volume mesh is invalid.
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, World& world, LocalLightGpuList& gpuList, const Mesh& sphere, const Mesh& cone,
                  const Camera3D& camera, const Math::Matrix4f& viewProj, const LightingConstants& lighting) const;

        bool isValid() const { return m_psoMesh != nullptr && m_psoFullscreen != nullptr; }

    private:
        bool bindCommon(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const LocalLightGpuList& gpuList, const LocalLightPassConstants& cb) const;

        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_psoMesh;
        ComPtr<ID3D12PipelineState> m_psoFullscreen;
    };

} // namespace Dark
