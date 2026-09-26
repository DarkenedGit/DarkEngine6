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

    struct CloudVolumePassConstants
    {
        float    invViewProj[16];
        float    cameraPos[3];
        float    time;
        float    sunDir[3];
        float    volumeCount;
        float    sunColor[3];
        float    nearZ;
        float    ambientColor[3];
        float    pad;
    };
    static_assert(sizeof(CloudVolumePassConstants) == 32 * sizeof(float), "cloud volume root constants");

    struct CloudVolumeFrame
    {
        Math::Vector3f sunDir{ 0.35f, 0.85f, -0.35f };
        Math::Vector3f sunColor{ 1.0f, 0.96f, 0.88f };
        Math::Vector3f ambientColor{ 0.18f, 0.20f, 0.24f };
        float          time = 0.0f;
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
