#pragma once

#include "Render/Texture2D.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // Packed as 16 root 32-bit values (b0). Matches TerrainErosion.hlsl ErosionConstants.
    struct ErosionGpuConstants
    {
        uint32_t width       = 0;
        uint32_t height      = 0;
        float    cellSize    = 1.0f;
        float    talusTan    = 0.7f;
        float    thermalRate = 0.5f;
        float    evaporate   = 0.02f;
        float    capacity    = 1.0f;
        float    erode       = 0.3f;
        float    deposit     = 0.3f;
        float    gravity     = 4.0f;
        float    seaLevelRaw = 0.0f;
        uint32_t stage       = 0; // 0 = Mei flux, 1 = transport
        float    pipeDt      = 0.2f;
        float    rain        = 0.02f;
        uint32_t _pad0       = 0;
        uint32_t _pad1       = 0;
    };
    static_assert(sizeof(ErosionGpuConstants) == 16 * sizeof(uint32_t), "ErosionGpuConstants is 16 root constants");

    // Bake-only compute PSO (first cs_5_0). Private DIRECT allocator/list — never Renderer::commandList().
    // UAV targets come from Texture2D::createUavR32Float / createUavRgba32Float, not general albedo/G-buffer/HDR.
    class TerrainErosionPipeline
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;
        static constexpr UINT kRootUav       = 2;
        static constexpr UINT kConstantCount = 16;
        static constexpr UINT kSrvCount      = 1;
        static constexpr UINT kUavCount      = 4;
        static constexpr UINT kSlotStride    = kSrvCount + kUavCount; // SRV + 4 UAV
        static constexpr UINT kBatchSlots    = 8;                     // unique heap range per recorded dispatch (tables resolve at execute)
        static constexpr UINT kHeapCount     = kBatchSlots * kSlotStride;
        static constexpr UINT kSrvHeapSlot   = 0;
        static constexpr UINT kUavHeapSlot   = 1;

        TerrainErosionPipeline() = default;

        bool create(ID3D12Device* device);
        bool isValid() const
        {
            return m_psoThermal != nullptr && m_psoPipe != nullptr;
        }

        bool resetCommands(ID3D12Device* device);

        // One Jacobi iteration. src = SRV, dst = UAV (distinct resources). batchSlot in [0, kBatchSlots).
        // Each slot owns a heap range so a later copy cannot change an in-list dispatch's table.
        bool dispatchThermal(ID3D12GraphicsCommandList* cmd, ID3D12Device* device, const Texture2D& src, const Texture2D& dst, const ErosionGpuConstants& constants, uint32_t batchSlot = 0);

        // One Mei pipe iteration: flux (stage 0) then transport (stage 1) with a UAV barrier between.
        // height/water/flux/sediment must be UAV. Never records onto Renderer::commandList().
        bool dispatchPipe(ID3D12GraphicsCommandList* cmd, ID3D12Device* device, const Texture2D& height, const Texture2D& water, const Texture2D& flux, const Texture2D& sediment,
                          const ErosionGpuConstants& constants, uint32_t batchSlot = 0);

        ID3D12RootSignature* rootSignature() const
        {
            return m_rootSignature.Get();
        }
        ID3D12PipelineState* psoThermal() const
        {
            return m_psoThermal.Get();
        }
        ID3D12PipelineState* psoPipe() const
        {
            return m_psoPipe.Get();
        }
        ID3D12DescriptorHeap* heap() const
        {
            return m_heap.Get();
        }
        ID3D12CommandAllocator* allocator() const
        {
            return m_allocator.Get();
        }
        ID3D12GraphicsCommandList* commandList() const
        {
            return m_list.Get();
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle(uint32_t slot) const;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle(uint32_t slot) const;

    private:
        bool createPsos(ID3D12Device* device);
        bool bindCommon(ID3D12GraphicsCommandList* cmd, ID3D12PipelineState* pso, const ErosionGpuConstants& constants, uint32_t batchSlot);
        UINT slotBase(uint32_t batchSlot) const;

        ComPtr<ID3D12RootSignature>       m_rootSignature;
        ComPtr<ID3D12PipelineState>       m_psoThermal;
        ComPtr<ID3D12PipelineState>       m_psoPipe;
        ComPtr<ID3D12DescriptorHeap>      m_heap;
        ComPtr<ID3D12CommandAllocator>    m_allocator;
        ComPtr<ID3D12GraphicsCommandList> m_list;
        D3D12_CPU_DESCRIPTOR_HANDLE       m_cpuStart{};
        D3D12_GPU_DESCRIPTOR_HANDLE       m_gpuStart{};
        UINT                              m_incr = 0;
    };

} // namespace Dark
