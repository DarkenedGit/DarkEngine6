#include "Render/TerrainErosionPipeline.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"

#include <d3dcompiler.h>

namespace Dark
{

    namespace
    {
        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        void UavBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource)
        {
            D3D12_RESOURCE_BARRIER b{};
            b.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            b.UAV.pResource = resource;
            cmd->ResourceBarrier(1, &b);
        }
    } // namespace

    bool TerrainErosionPipeline::create(ID3D12Device* device)
    {
        if (isValid() && m_heap)
            return true;
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainErosionPipeline::create: null device");
            return false;
        }

        m_rootSignature.Reset();
        m_psoThermal.Reset();
        m_psoPipe.Reset();
        m_heap.Reset();
        m_allocator.Reset();
        m_list.Reset();
        m_cpuStart = {};
        m_gpuStart = {};
        m_incr     = 0;

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = kSrvCount;
        srvRange.BaseShaderRegister                = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE uavRange{};
        uavRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors                    = kUavCount;
        uavRange.BaseShaderRegister                = 0;
        uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[3]{};
        params[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootConstants].Constants.ShaderRegister = 0;
        params[kRootConstants].Constants.Num32BitValues = kConstantCount;

        params[kRootSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootSrv].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootSrv].DescriptorTable.pDescriptorRanges   = &srvRange;

        params[kRootUav].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootUav].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootUav].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootUav].DescriptorTable.pDescriptorRanges   = &uavRange;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters = 3;
        rsDesc.pParameters   = params;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (terrain erosion)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "TerrainErosion RS: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)), "CreateRootSignature (terrain erosion)"))
            return false;

        if (!createPsos(device))
        {
            m_rootSignature.Reset();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = kHeapCount;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_heap)), "CreateDescriptorHeap (terrain erosion UAV/SRV)"))
        {
            m_rootSignature.Reset();
            m_psoThermal.Reset();
            m_psoPipe.Reset();
            return false;
        }

        m_incr     = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
        m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
        return true;
    }

    bool TerrainErosionPipeline::createPsos(ID3D12Device* device)
    {
        ComPtr<ID3DBlob> csThermal;
        ComPtr<ID3DBlob> csPipe;
        // Bake CS loops: never SKIP_OPTIMIZATION (Debug hitch), same as IblBake.
        if (!compileShaderFromContent("shaders/TerrainErosion.hlsl", "CSThermal", "cs_5_0", csThermal, nullptr, true) ||
            !compileShaderFromContent("shaders/TerrainErosion.hlsl", "CSPipe", "cs_5_0", csPipe, nullptr, true))
            return false;

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = m_rootSignature.Get();
        pso.CS             = { csThermal->GetBufferPointer(), csThermal->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&m_psoThermal)), "CreateComputePipelineState (terrain thermal)"))
            return false;

        pso.CS = { csPipe->GetBufferPointer(), csPipe->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&m_psoPipe)), "CreateComputePipelineState (terrain pipe)"))
        {
            m_psoThermal.Reset();
            return false;
        }
        return true;
    }

    bool TerrainErosionPipeline::resetCommands(ID3D12Device* device)
    {
        if (!device)
            return false;
        m_list.Reset();
        m_allocator.Reset();
        if (FailedHr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocator)), "CreateCommandAllocator (terrain erosion bake)"))
            return false;
        if (FailedHr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocator.Get(), nullptr, IID_PPV_ARGS(&m_list)), "CreateCommandList (terrain erosion bake)"))
        {
            m_allocator.Reset();
            return false;
        }
        return true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE TerrainErosionPipeline::cpuHandle(uint32_t slot) const
    {
        DE_ASSERT(slot < kHeapCount);
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_cpuStart;
        h.ptr += static_cast<SIZE_T>(slot) * m_incr;
        return h;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE TerrainErosionPipeline::gpuHandle(uint32_t slot) const
    {
        DE_ASSERT(slot < kHeapCount);
        D3D12_GPU_DESCRIPTOR_HANDLE h = m_gpuStart;
        h.ptr += static_cast<SIZE_T>(slot) * m_incr;
        return h;
    }

    UINT TerrainErosionPipeline::slotBase(uint32_t batchSlot) const
    {
        return batchSlot * kSlotStride;
    }

    bool TerrainErosionPipeline::bindCommon(ID3D12GraphicsCommandList* cmd, ID3D12PipelineState* pso, const ErosionGpuConstants& constants, uint32_t batchSlot)
    {
        if (!cmd || !pso || !isValid() || batchSlot >= kBatchSlots)
            return false;
        const UINT base = slotBase(batchSlot);
        cmd->SetComputeRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(pso);
        ID3D12DescriptorHeap* heaps[] = { m_heap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetComputeRoot32BitConstants(kRootConstants, kConstantCount, &constants, 0);
        cmd->SetComputeRootDescriptorTable(kRootSrv, gpuHandle(base + kSrvHeapSlot));
        cmd->SetComputeRootDescriptorTable(kRootUav, gpuHandle(base + kUavHeapSlot));
        return true;
    }

    bool TerrainErosionPipeline::dispatchThermal(ID3D12GraphicsCommandList* cmd, ID3D12Device* device, const Texture2D& src, const Texture2D& dst, const ErosionGpuConstants& constants,
                                                 uint32_t batchSlot)
    {
        if (!device || !src.valid() || !dst.valid() || src.resource() == dst.resource() || constants.width == 0 || constants.height == 0)
            return false;
        if (!bindCommon(cmd, m_psoThermal.Get(), constants, batchSlot))
            return false;

        const UINT base = slotBase(batchSlot);
        device->CopyDescriptorsSimple(1, cpuHandle(base + kSrvHeapSlot), src.cpuHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (UINT i = 0; i < kUavCount; ++i)
            device->CopyDescriptorsSimple(1, cpuHandle(base + kUavHeapSlot + i), dst.cpuHandleUav(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        const UINT gx = (constants.width + 7u) / 8u;
        const UINT gy = (constants.height + 7u) / 8u;
        cmd->Dispatch(gx, gy, 1);
        UavBarrier(cmd, dst.resource());
        return true;
    }

    bool TerrainErosionPipeline::dispatchPipe(ID3D12GraphicsCommandList* cmd, ID3D12Device* device, const Texture2D& height, const Texture2D& water, const Texture2D& flux, const Texture2D& sediment,
                                              const ErosionGpuConstants& constants, uint32_t batchSlot)
    {
        if (!device || !height.valid() || !water.valid() || !flux.valid() || !sediment.valid() || constants.width == 0 || constants.height == 0)
            return false;

        ErosionGpuConstants fluxConst = constants;
        fluxConst.stage               = 0;
        if (!bindCommon(cmd, m_psoPipe.Get(), fluxConst, batchSlot))
            return false;

        const UINT       base    = slotBase(batchSlot);
        const Texture2D* uavs[4] = { &height, &water, &flux, &sediment };
        device->CopyDescriptorsSimple(1, cpuHandle(base + kSrvHeapSlot), height.cpuHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (UINT i = 0; i < kUavCount; ++i)
            device->CopyDescriptorsSimple(1, cpuHandle(base + kUavHeapSlot + i), uavs[i]->cpuHandleUav(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        const UINT gx = (constants.width + 7u) / 8u;
        const UINT gy = (constants.height + 7u) / 8u;
        cmd->Dispatch(gx, gy, 1);

        D3D12_RESOURCE_BARRIER bars[4]{};
        ID3D12Resource*        res[4] = { height.resource(), water.resource(), flux.resource(), sediment.resource() };
        for (int i = 0; i < 4; ++i)
        {
            bars[i].Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            bars[i].UAV.pResource = res[i];
        }
        cmd->ResourceBarrier(4, bars);

        ErosionGpuConstants transConst = constants;
        transConst.stage               = 1;
        cmd->SetComputeRoot32BitConstants(kRootConstants, kConstantCount, &transConst, 0);
        cmd->Dispatch(gx, gy, 1);
        cmd->ResourceBarrier(4, bars);
        return true;
    }

} // namespace Dark
