#pragma once

#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Render/DebugRenderState.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;
    class GpuResourceCache;
    class Model;
    class SkinningUploadRing;
    struct AnimPose;

    using Microsoft::WRL::ComPtr;

    struct CamouflageConstants
    {
        float worldViewProj[16];
        float world[16];
        float cameraPos[3];
        float time;
        float invSize[2];
        float distort;
        float rimStrength;
    };
    static_assert(sizeof(CamouflageConstants) == 40 * sizeof(float), "camouflage root constants");

    class CamouflagePipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSceneSrv  = 1;
        static constexpr UINT kRootBoneCbv   = 2;

        CamouflagePipeline() = default;

        bool create(ID3D12Device* device, DXGI_FORMAT colorFormat);
        bool ensureCopy(ID3D12Device* device, uint32_t width, uint32_t height);
        void captureScene(ID3D12GraphicsCommandList* cmd, Renderer& renderer);
        void drawModel(ID3D12GraphicsCommandList* cmd, Renderer& renderer, GpuResourceCache& gpu, SkinningUploadRing& ring, const Model& model,
                       const AnimPose* pose, const Math::Matrix4f& world, const Math::Matrix4f& viewProj, const Math::Vector3f& cameraPos, float time,
                       DebugFill fill = DebugFill::Solid);

        bool isValid() const { return m_psoSkinned != nullptr && m_rootSignature != nullptr; }

    private:
        bool bindDraw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, ID3D12PipelineState* pso) const;

        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_psoSkinned;
        ComPtr<ID3D12PipelineState>  m_psoStatic;
        ComPtr<ID3D12Resource>       m_copy;
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        D3D12_GPU_DESCRIPTOR_HANDLE  m_srvGpu{};
        D3D12_RESOURCE_STATES        m_copyState = D3D12_RESOURCE_STATE_COMMON;
        uint32_t                     m_width     = 0;
        uint32_t                     m_height    = 0;
        DXGI_FORMAT                  m_format    = DXGI_FORMAT_R16G16B16A16_FLOAT;
    };

} // namespace Dark
