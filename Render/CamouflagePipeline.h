#pragma once

#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Render/DebugRenderState.h"
#include "Render/GraphTargets.h"

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
        ~CamouflagePipeline();
        CamouflagePipeline(const CamouflagePipeline&)            = delete;
        CamouflagePipeline& operator=(const CamouflagePipeline&) = delete;

        bool create(ID3D12Device* device, DXGI_FORMAT colorFormat);
        void release();
        // The HDR copy the body refracts, registered on first use. False when the target registry or
        // the copy is missing.
        bool                        ensureCopy(Renderer& renderer);
        ID3D12Resource*             copyResource() const;
        D3D12_CPU_DESCRIPTOR_HANDLE copySrv() const;
        // sceneTable: a one-SRV ring table with the HDR copy (t0). HDR and writable depth are bound.
        // True when it reached the draws: depth may then hold the body, which the G-buffer does not.
        bool drawModel(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE sceneTable, GpuResourceCache& gpu, SkinningUploadRing& ring, const Model& model, const AnimPose* pose,
                       const Math::Matrix4f& world, const Math::Matrix4f& viewProj, const Math::Vector3f& cameraPos, float time, DebugFill fill = DebugFill::Solid);

        bool isValid() const { return m_psoSkinned != nullptr && m_rootSignature != nullptr; }

    private:
        bool bindDraw(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE sceneTable, ID3D12PipelineState* pso) const;

        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_psoSkinned;
        ComPtr<ID3D12PipelineState> m_psoStatic;
        GraphTargets*               m_targets = nullptr;
        GraphTarget                 m_copy;
        DXGI_FORMAT                 m_format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    };

} // namespace Dark
