#pragma once

#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Render/CloudVolumeGpuList.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Camera3D;
    class Renderer;
    class World;

    using Microsoft::WRL::ComPtr;

    // Distance LOD for the ray march. A ray that exits the volume before detailDistance
    // keeps the fixed 40/56-step march. Longer rays spend full quality on the near shell,
    // blend across fadeDistance, then take a coarse tail.
    struct CloudLodSettings
    {
        bool  enabled         = true;
        float detailDistance  = 200.0f;
        float fadeDistance    = 48.0f;
        float nearStep        = 4.0f;
        float farStep         = 18.0f;
        int   maxNearSteps    = 32;
        int   maxFarSteps     = 16;
        int   nearLightSteps  = 5;
        int   farLightSteps   = 2;
    };

    inline void sanitizeCloudLod(CloudLodSettings& lod)
    {
        if (lod.detailDistance < 1.0f)
            lod.detailDistance = 1.0f;
        if (lod.fadeDistance < 0.0f)
            lod.fadeDistance = 0.0f;
        if (lod.nearStep < 0.5f)
            lod.nearStep = 0.5f;
        if (lod.nearStep > 64.0f)
            lod.nearStep = 64.0f;
        if (lod.farStep < lod.nearStep)
            lod.farStep = lod.nearStep;
        if (lod.farStep > 256.0f)
            lod.farStep = 256.0f;
        if (lod.maxNearSteps < 1)
            lod.maxNearSteps = 1;
        if (lod.maxNearSteps > 64)
            lod.maxNearSteps = 64;
        if (lod.maxFarSteps < 1)
            lod.maxFarSteps = 1;
        if (lod.maxFarSteps > 64)
            lod.maxFarSteps = 64;
        if (lod.nearLightSteps < 1)
            lod.nearLightSteps = 1;
        if (lod.nearLightSteps > 8)
            lod.nearLightSteps = 8;
        if (lod.farLightSteps < 1)
            lod.farLightSteps = 1;
        if (lod.farLightSteps > 8)
            lod.farLightSteps = 8;
    }

    // HLSL cbuffer order. lodEnabled occupies the old pad so the tail stays float4-aligned.
    struct CloudVolumePassConstants
    {
        float invViewProj[16];
        float cameraPos[3];
        float time;
        float sunDir[3];
        float volumeCount;
        float sunColor[3];
        float nearZ;
        float ambientColor[3];
        float lodEnabled;
        float lodDetailDist;
        float lodFadeDist;
        float lodNearStep;
        float lodFarStep;
        float lodMaxNear;
        float lodMaxFar;
        float lodNearLight;
        float lodFarLight;
    };
    static_assert(sizeof(CloudVolumePassConstants) == 40 * sizeof(float), "cloud volume root constants");

    struct CloudVolumeFrame
    {
        Math::Vector3f sunDir{ 0.35f, 0.85f, -0.35f };
        Math::Vector3f sunColor{ 1.0f, 0.96f, 0.88f };
        Math::Vector3f ambientColor{ 0.18f, 0.20f, 0.24f };
        float          time = 0.0f;
        CloudLodSettings lod{};
    };

    class CloudVolumePipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootDepthSrv  = 1;
        static constexpr UINT kRootVolumes   = 2;

        CloudVolumePipeline() = default;

        bool create(ID3D12Device* device, DXGI_FORMAT colorFormat);
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, World& world, CloudVolumeGpuList& gpuList, const Camera3D& camera,
                  const Math::Matrix4f& viewProj, const CloudVolumeFrame& frame) const;

        bool isValid() const { return m_pso != nullptr && m_rootSignature != nullptr; }

    private:
        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_pso;
        ComPtr<ID3D12DescriptorHeap> m_depthHeap;
        D3D12_GPU_DESCRIPTOR_HANDLE  m_depthGpu{};
    };

} // namespace Dark
