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
    class LocalShadowSystem;
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
        float    heightFogDensity;
        float    heightFogFalloff;
        float    heightFogHeight;
        float    volumetricFogDensity;
        float    waterLevel;
        float    volumetricHeight;
        float    heightOriginX;
        float    heightOriginZ;
        float    heightCellSize;
        float    heightWorldSizeX;
        float    heightWorldSizeZ;
        float    localShadowDebug;
    };
    static_assert(sizeof(LocalLightPassConstants) == 56 * sizeof(float), "local light root constants");
    // 56 constants + 1 srv table + 2 lights + 2 worlds + 1 height + 1 AO + 1 shadow table = 64.
    static_assert(56 + 1 + 2 + 2 + 1 + 1 + 1 == 64, "local light root signature DWORD budget");
    static_assert(56 + 1 + 2 + 2 + 1 + 1 + 1 <= 64, "local light root signature DWORD budget");

    class LocalLightVolumePipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrvTable  = 1;
        static constexpr UINT kRootLightsSrv = 2;
        static constexpr UINT kRootWorldSrv  = 3;
        static constexpr UINT kRootHeightSrv = 4;
        static constexpr UINT kRootAoSrv       = 5;
        static constexpr UINT kRootShadowTable = 6;

        LocalLightVolumePipeline() = default;

        bool create(ID3D12Device* device);

        void drawInstanced(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const LocalLightGpuList& gpuList, const Mesh& volume, uint32_t instanceCount,
                           uint32_t baseIndex, const LocalLightPassConstants& cb) const;

        void drawFullscreenScissor(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const LocalLightGpuList& gpuList, const D3D12_RECT& scissor,
                                   uint32_t lightIndex, const LocalLightPassConstants& cb) const;

        // Gather + upload + instanced sphere/cone + inside fullscreen. Fog/camera copied from `lighting`.
        // Skips if pipeline, GPU list, or either volume mesh is invalid.
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, World& world, LocalLightGpuList& gpuList, const Mesh& sphere, const Mesh& cone,
                  const Camera3D& camera, const Math::Matrix4f& viewProj, const LightingConstants& lighting, const LocalShadowSystem* localShadows) const;

        bool isValid() const { return m_psoMesh != nullptr && m_psoFullscreen != nullptr; }

    private:
        bool bindCommon(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const LocalLightGpuList& gpuList, const LocalLightPassConstants& cb) const;

        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_psoMesh;
        ComPtr<ID3D12PipelineState> m_psoFullscreen;
    };

} // namespace Dark
