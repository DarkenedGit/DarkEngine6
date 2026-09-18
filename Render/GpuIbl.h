#pragma once

#include "Render/IblBake.h"
#include "Render/IblBakePipeline.h"

#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;
    class Image;

    using Microsoft::WRL::ComPtr;

    class GpuIbl
    {
    public:
        GpuIbl() = default;

        GpuIbl(GpuIbl&&) noexcept            = default;
        GpuIbl& operator=(GpuIbl&&) noexcept = default;

        GpuIbl(const GpuIbl&)            = delete;
        GpuIbl& operator=(const GpuIbl&) = delete;

        // Pointer, not reference: UnitTests never construct a D3D Renderer.
        bool bake(Renderer* renderer, const Image& equirect, const IblBakeSettings& settings = {});
        bool isReady() const { return m_ready; }

        D3D12_CPU_DESCRIPTOR_HANDLE irradianceCpu() const { return m_irradianceCpu; }
        D3D12_CPU_DESCRIPTOR_HANDLE prefilterCpu() const { return m_prefilterCpu; }

    private:
        void resetGpu();

        IblBakePipeline             m_pipeline;
        ComPtr<ID3D12Resource>      m_envCube;
        ComPtr<ID3D12Resource>      m_irradiance;
        ComPtr<ID3D12Resource>      m_prefilter;
        ComPtr<ID3D12DescriptorHeap> m_cpuSrvHeap; // FLAG_NONE: env, irr, pref
        D3D12_CPU_DESCRIPTOR_HANDLE m_envCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE m_irradianceCpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE m_prefilterCpu{};
        bool                        m_ready = false;
    };

    bool createBlackIblCube(Renderer& renderer, uint32_t size, ComPtr<ID3D12Resource>& outResource, ComPtr<ID3D12DescriptorHeap>& outCpuHeap,
                            D3D12_CPU_DESCRIPTOR_HANDLE& outCpu);

} // namespace Dark
