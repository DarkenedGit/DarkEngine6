#include "Render/InstanceCull.h"

#include "Core/Log.h"
#include "Render/RenderItem.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"

#include <cstring>
#include <d3dcompiler.h>

namespace Dark
{

    namespace
    {

        constexpr UINT kRootConstants = 0;
        constexpr UINT kRootAabbs     = 1;
        constexpr UINT kRootBatches   = 2;
        constexpr UINT kRootIndices   = 3;
        constexpr UINT kRootCounters  = 4;
        constexpr UINT kRootArgs      = 5;

        struct CullConstants
        {
            float    planes[6][4];
            uint32_t itemCount;
            uint32_t batchCount;
            uint32_t indexBase;
            uint32_t argBase;
            uint32_t counterBase;
            uint32_t useDepthBatch;
            uint32_t pad0;
            uint32_t pad1;
        };
        static_assert(sizeof(CullConstants) == 32 * sizeof(uint32_t), "cull constants");

        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        bool serializeRoot(const D3D12_ROOT_SIGNATURE_DESC& desc, const char* label, ComPtr<ID3DBlob>& blob)
        {
            ComPtr<ID3DBlob> err;
            if (FailedHr(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err), label))
            {
                if (err)
                    DE_LOG_ERROR(LogCategory::Render, "{}: {}", label, static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }
            return true;
        }

        ComPtr<ID3D12Resource> createUpload(ID3D12Device* device, UINT64 bytes, const char* what)
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width            = bytes;
            desc.Height           = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.SampleDesc       = { 1, 0 };
            desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ComPtr<ID3D12Resource> res;
            if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)), what))
                return nullptr;
            return res;
        }

        ComPtr<ID3D12Resource> createUavBuffer(ID3D12Device* device, UINT64 bytes, D3D12_RESOURCE_STATES state, const char* what)
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type                 = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width            = bytes;
            desc.Height           = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.SampleDesc       = { 1, 0 };
            desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            ComPtr<ID3D12Resource> res;
            if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&res)), what))
                return nullptr;
            return res;
        }

        void transitionBuffer(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
        {
            if (!cmd || !resource || before == after)
                return;
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = resource;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter  = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &barrier);
        }

        void uavBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource)
        {
            if (!cmd || !resource)
                return;
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = resource;
            cmd->ResourceBarrier(1, &barrier);
        }

        bool computable(D3D12_RESOURCE_STATES state)
        {
            return state == D3D12_RESOURCE_STATE_COPY_DEST || state == D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT || state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        bool indexComputable(D3D12_RESOURCE_STATES state)
        {
            return state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS || state == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }

    } // namespace

    bool InstanceCull::isReady() const
    {
        return m_device && m_root && m_compact && m_argsPso && m_heap && m_indices[0] && m_indices[1] && m_counters[0] && m_counters[1];
    }

    bool InstanceCull::createShaderVisibleHeap(ID3D12Device* device)
    {
        // ClearUnorderedAccessViewUint reads a CPU descriptor and clears through a GPU one.
        // The CPU handle must be non-shader-visible. The GPU handle must sit in the heap bound on this list.
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = kFrameSlots * kMaxStaticViews;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_heap)), "CreateDescriptorHeap (instance cull)"))
            return false;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_cpuHeap)), "CreateDescriptorHeap (instance cull CPU)"))
            return false;
        m_increment   = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_gpuCpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
        m_cpuStart    = m_cpuHeap->GetCPUDescriptorHandleForHeapStart();
        m_gpuStart    = m_heap->GetGPUDescriptorHandleForHeapStart();
        return true;
    }

    bool InstanceCull::createBuffers(ID3D12Device* device)
    {
        const UINT64 indexBytes   = static_cast<UINT64>(kMaxStaticViews) * kMaxStaticItems * sizeof(uint32_t);
        const UINT64 counterBytes = static_cast<UINT64>(kMaxStaticViews) * kMaxBatches * sizeof(uint32_t);
        m_indices[0]              = createUavBuffer(device, indexBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "CreateCommittedResource (draw list cull indices 0)");
        m_indices[1]              = createUavBuffer(device, indexBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "CreateCommittedResource (draw list cull indices 1)");
        m_counters[0]             = createUavBuffer(device, counterBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "CreateCommittedResource (draw list batch counters 0)");
        m_counters[1]             = createUavBuffer(device, counterBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "CreateCommittedResource (draw list batch counters 1)");
        if (!m_indices[0] || !m_indices[1] || !m_counters[0] || !m_counters[1])
            return false;
        m_indices[0]->SetName(L"DrawListCullIndices0");
        m_indices[1]->SetName(L"DrawListCullIndices1");
        m_counters[0]->SetName(L"DrawListBatchCounters0");
        m_counters[1]->SetName(L"DrawListBatchCounters1");
        m_indexState[0] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        m_indexState[1] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        return true;
    }

    bool InstanceCull::create(ID3D12Device* device)
    {
        m_device.Reset();
        m_root.Reset();
        m_compact.Reset();
        m_argsPso.Reset();
        m_heap.Reset();
        m_cpuHeap.Reset();
        m_indices[0].Reset();
        m_indices[1].Reset();
        m_counters[0].Reset();
        m_counters[1].Reset();
        if (!device)
            return false;
        static_assert(Renderer::kFrameCount == kFrameSlots, "cull frame slots");

        D3D12_ROOT_PARAMETER params[6]{};
        params[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootConstants].Constants.ShaderRegister = 0;
        params[kRootConstants].Constants.RegisterSpace  = 0;
        params[kRootConstants].Constants.Num32BitValues = 32;

        params[kRootAabbs].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kRootAabbs].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootAabbs].Descriptor.ShaderRegister = 0;
        params[kRootAabbs].Descriptor.RegisterSpace  = 0;

        params[kRootBatches].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kRootBatches].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootBatches].Descriptor.ShaderRegister = 1;
        params[kRootBatches].Descriptor.RegisterSpace  = 0;

        params[kRootIndices].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kRootIndices].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootIndices].Descriptor.ShaderRegister = 0;
        params[kRootIndices].Descriptor.RegisterSpace  = 0;

        params[kRootCounters].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kRootCounters].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootCounters].Descriptor.ShaderRegister = 1;
        params[kRootCounters].Descriptor.RegisterSpace  = 0;

        params[kRootArgs].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kRootArgs].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootArgs].Descriptor.ShaderRegister = 2;
        params[kRootArgs].Descriptor.RegisterSpace  = 0;

        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 6;
        rootDesc.pParameters   = params;

        ComPtr<ID3DBlob> rsBlob;
        if (!serializeRoot(rootDesc, "D3D12SerializeRootSignature (instance cull)", rsBlob))
            return false;
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_root)), "CreateRootSignature (instance cull)"))
            return false;

        ComPtr<ID3DBlob> compact;
        ComPtr<ID3DBlob> args;
        if (!compileShaderFromContent("shaders/InstanceCull.hlsl", "CSCompact", "cs_5_0", compact) || !compileShaderFromContent("shaders/InstanceCull.hlsl", "CSArgs", "cs_5_0", args))
            return false;

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = m_root.Get();
        pso.CS             = { compact->GetBufferPointer(), compact->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&m_compact)), "CreateComputePipelineState (instance cull compact)"))
            return false;
        pso.CS = { args->GetBufferPointer(), args->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&m_argsPso)), "CreateComputePipelineState (instance cull args)"))
            return false;

        if (!createShaderVisibleHeap(device) || !createBuffers(device))
            return false;

        m_device = device;
        return true;
    }

    D3D12_GPU_VIRTUAL_ADDRESS InstanceCull::indexAddress(uint32_t frameSlot, uint32_t viewIndex) const
    {
        if (frameSlot >= kFrameSlots || viewIndex >= kMaxStaticViews || !m_indices[frameSlot])
            return 0;
        return m_indices[frameSlot]->GetGPUVirtualAddress() + static_cast<UINT64>(viewIndex) * kMaxStaticItems * sizeof(uint32_t);
    }

    ID3D12Resource* InstanceCull::indexResource(uint32_t frameSlot) const
    {
        if (frameSlot >= kFrameSlots)
            return nullptr;
        return m_indices[frameSlot].Get();
    }

    bool InstanceCull::dispatch(ID3D12GraphicsCommandList* cmd, uint32_t frameSlot, D3D12_GPU_VIRTUAL_ADDRESS aabbVA, D3D12_GPU_VIRTUAL_ADDRESS gbufferRecordsVA, D3D12_GPU_VIRTUAL_ADDRESS depthRecordsVA, ID3D12Resource* args, D3D12_RESOURCE_STATES& argState, const CullViewDesc* views, uint32_t viewCount)
    {
        if (!cmd || !isReady() || !args || !views || viewCount == 0 || frameSlot >= kFrameSlots)
            return false;

        bool any = false;
        for (uint32_t i = 0; i < viewCount; ++i)
        {
            if (views[i].batchCount > 0 && views[i].batchCount <= kMaxBatches && views[i].viewIndex < kMaxStaticViews)
                any = true;
        }
        if (!any)
            return false;

        const D3D12_RESOURCE_STATES indexBefore = m_indexState[frameSlot];
        if (!computable(argState) || !indexComputable(indexBefore))
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                DE_LOG_ERROR(LogCategory::Render, "DrawList: instance cull skipped; buffer state is not a compute source");
            }
            return false;
        }

        ID3D12Resource* indices  = m_indices[frameSlot].Get();
        ID3D12Resource* counters = m_counters[frameSlot].Get();
        if (argState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            transitionBuffer(cmd, args, argState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            argState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (indexBefore != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            transitionBuffer(cmd, indices, indexBefore, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            m_indexState[frameSlot] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        ID3D12DescriptorHeap* heaps[] = { m_heap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetComputeRootSignature(m_root.Get());
        cmd->SetComputeRootShaderResourceView(kRootAabbs, aabbVA);
        cmd->SetComputeRootUnorderedAccessView(kRootIndices, indices->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(kRootCounters, counters->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(kRootArgs, args->GetGPUVirtualAddress());

        for (uint32_t i = 0; i < viewCount; ++i)
        {
            const CullViewDesc& view = views[i];
            if (view.batchCount == 0 || view.batchCount > kMaxBatches || view.viewIndex >= kMaxStaticViews)
                continue;

            const UINT descriptor = frameSlot * kMaxStaticViews + view.viewIndex;
            D3D12_CPU_DESCRIPTOR_HANDLE gpuCpu = m_gpuCpuStart;
            D3D12_CPU_DESCRIPTOR_HANDLE cpu    = m_cpuStart;
            D3D12_GPU_DESCRIPTOR_HANDLE gpu    = m_gpuStart;
            gpuCpu.ptr += static_cast<SIZE_T>(descriptor) * m_increment;
            cpu.ptr += static_cast<SIZE_T>(descriptor) * m_increment;
            gpu.ptr += static_cast<UINT64>(descriptor) * m_increment;

            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format                      = DXGI_FORMAT_R32_UINT;
            uav.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
            uav.Buffer.FirstElement         = static_cast<UINT64>(view.viewIndex) * kMaxBatches;
            uav.Buffer.NumElements          = view.batchCount;
            m_device->CreateUnorderedAccessView(counters, nullptr, &uav, gpuCpu);
            m_device->CreateUnorderedAccessView(counters, nullptr, &uav, cpu);

            const UINT zeros[4] = { 0, 0, 0, 0 };
            cmd->ClearUnorderedAccessViewUint(gpu, cpu, counters, zeros, 0, nullptr);
            uavBarrier(cmd, counters);

            CullConstants constants{};
            for (int plane = 0; plane < 6; ++plane)
            {
                constants.planes[plane][0] = view.planes[plane][0];
                constants.planes[plane][1] = view.planes[plane][1];
                constants.planes[plane][2] = view.planes[plane][2];
                constants.planes[plane][3] = view.planes[plane][3];
            }
            constants.itemCount     = view.itemCount;
            constants.batchCount    = view.batchCount;
            constants.indexBase     = view.viewIndex * kMaxStaticItems;
            constants.argBase       = view.viewIndex * kMaxBatches;
            constants.counterBase   = view.viewIndex * kMaxBatches;
            constants.useDepthBatch = view.useDepthBatch;
            cmd->SetComputeRoot32BitConstants(kRootConstants, 32, &constants, 0);
            const D3D12_GPU_VIRTUAL_ADDRESS records = view.useDepthBatch != 0 ? depthRecordsVA : gbufferRecordsVA;
            cmd->SetComputeRootShaderResourceView(kRootBatches, records);

            cmd->SetPipelineState(m_compact.Get());
            if (view.itemCount > 0)
            {
                const UINT groups = (view.itemCount + 63u) / 64u;
                cmd->Dispatch(groups, 1, 1);
            }
            uavBarrier(cmd, counters);

            cmd->SetPipelineState(m_argsPso.Get());
            const UINT argGroups = (view.batchCount + 63u) / 64u;
            cmd->Dispatch(argGroups, 1, 1);
            // Pass 3 reads this view's counters. The next clear writes the same buffer.
            uavBarrier(cmd, counters);
        }

        D3D12_RESOURCE_BARRIER done[2]{};
        done[0].Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        done[0].UAV.pResource = args;
        done[1].Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        done[1].UAV.pResource = indices;
        cmd->ResourceBarrier(2, done);
        transitionBuffer(cmd, args, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        transitionBuffer(cmd, indices, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        argState                = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        m_indexState[frameSlot] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        return true;
    }

    namespace
    {
        constexpr UINT64 kSkinnedAabbBytes   = static_cast<UINT64>(SkinnedInstanceCull::kStride) * sizeof(MeshCullAabb);
        constexpr UINT64 kSkinnedRecordBytes = static_cast<UINT64>(SkinnedInstanceCull::kStride) * sizeof(GpuBatchRecord);
        constexpr UINT64 kSkinnedSlotBytes   = kSkinnedAabbBytes + 2u * kSkinnedRecordBytes;
    }

    SkinnedInstanceCull::SkinnedInstanceCull() = default;

    SkinnedInstanceCull::~SkinnedInstanceCull()
    {
        if (m_upload && m_uploadBytes)
            m_upload->Unmap(0, nullptr);
        m_uploadBytes = nullptr;
    }

    bool SkinnedInstanceCull::isReady() const
    {
        return m_device && m_root && m_compact && m_argsPso && m_heap && m_signature && m_uploadBytes
            && m_indices[0] && m_indices[1] && m_counters[0] && m_counters[1] && m_args[0] && m_args[1];
    }

    bool SkinnedInstanceCull::createShaderVisibleHeap(ID3D12Device* device)
    {
        // Same split as the static cull: the clear reads a non-shader-visible CPU descriptor.
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = kFrameSlots * kMaxStaticViews;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_heap)), "CreateDescriptorHeap (skinned cull)"))
            return false;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_cpuHeap)), "CreateDescriptorHeap (skinned cull CPU)"))
            return false;
        m_increment   = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_gpuCpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
        m_cpuStart    = m_cpuHeap->GetCPUDescriptorHandleForHeapStart();
        m_gpuStart    = m_heap->GetGPUDescriptorHandleForHeapStart();
        return true;
    }

    bool SkinnedInstanceCull::createBuffers(ID3D12Device* device)
    {
        const UINT64 indexBytes   = static_cast<UINT64>(kMaxStaticViews) * kStride * sizeof(uint32_t);
        const UINT64 counterBytes = static_cast<UINT64>(kMaxStaticViews) * kStride * sizeof(uint32_t);
        const UINT64 argBytes     = static_cast<UINT64>(kMaxStaticViews) * kStride * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        m_indices[0]              = createUavBuffer(device, indexBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "CreateCommittedResource (skinned cull indices 0)");
        m_indices[1]              = createUavBuffer(device, indexBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "CreateCommittedResource (skinned cull indices 1)");
        m_counters[0]             = createUavBuffer(device, counterBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "CreateCommittedResource (skinned cull counters 0)");
        m_counters[1]             = createUavBuffer(device, counterBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "CreateCommittedResource (skinned cull counters 1)");
        m_args[0]                 = createUavBuffer(device, argBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "CreateCommittedResource (skinned cull args 0)");
        m_args[1]                 = createUavBuffer(device, argBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "CreateCommittedResource (skinned cull args 1)");
        m_upload                  = createUpload(device, kSkinnedSlotBytes * kFrameSlots, "CreateCommittedResource (skinned cull upload)");
        if (!m_indices[0] || !m_indices[1] || !m_counters[0] || !m_counters[1] || !m_args[0] || !m_args[1] || !m_upload)
            return false;
        if (FailedHr(m_upload->Map(0, nullptr, reinterpret_cast<void**>(&m_uploadBytes)), "Map skinned cull upload"))
        {
            m_uploadBytes = nullptr;
            m_upload.Reset();
            return false;
        }
        m_indices[0]->SetName(L"DE.Skinned.CullIndices0");
        m_indices[1]->SetName(L"DE.Skinned.CullIndices1");
        m_counters[0]->SetName(L"DE.Skinned.CullCounters0");
        m_counters[1]->SetName(L"DE.Skinned.CullCounters1");
        m_args[0]->SetName(L"DE.Skinned.CullArgs0");
        m_args[1]->SetName(L"DE.Skinned.CullArgs1");
        m_upload->SetName(L"DE.Skinned.CullUpload");
        m_argState[0]   = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        m_argState[1]   = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        m_indexState[0] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        m_indexState[1] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        return true;
    }

    bool SkinnedInstanceCull::create(ID3D12Device* device)
    {
        m_device.Reset();
        m_root.Reset();
        m_compact.Reset();
        m_argsPso.Reset();
        m_heap.Reset();
        m_cpuHeap.Reset();
        m_signature.Reset();
        m_indices[0].Reset();
        m_indices[1].Reset();
        m_counters[0].Reset();
        m_counters[1].Reset();
        m_args[0].Reset();
        m_args[1].Reset();
        if (m_upload && m_uploadBytes)
            m_upload->Unmap(0, nullptr);
        m_uploadBytes = nullptr;
        m_upload.Reset();
        if (!device)
            return false;
        static_assert(Renderer::kFrameCount == kFrameSlots, "skinned cull frame slots");
        static_assert(kStride == kMaxSkinnedParts, "skinned cull stride");

        D3D12_INDIRECT_ARGUMENT_DESC argument{};
        argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        D3D12_COMMAND_SIGNATURE_DESC signature{};
        signature.ByteStride       = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        signature.NumArgumentDescs = 1;
        signature.pArgumentDescs   = &argument;
        if (FailedHr(device->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&m_signature)), "CreateCommandSignature (skinned cull)"))
            return false;
        m_signature->SetName(L"DE.Skinned.CommandSignature");

        D3D12_ROOT_PARAMETER params[6]{};
        params[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootConstants].Constants.ShaderRegister = 0;
        params[kRootConstants].Constants.RegisterSpace  = 0;
        params[kRootConstants].Constants.Num32BitValues = 32;

        params[kRootAabbs].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kRootAabbs].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootAabbs].Descriptor.ShaderRegister = 0;
        params[kRootAabbs].Descriptor.RegisterSpace  = 0;

        params[kRootBatches].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kRootBatches].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootBatches].Descriptor.ShaderRegister = 1;
        params[kRootBatches].Descriptor.RegisterSpace  = 0;

        params[kRootIndices].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kRootIndices].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootIndices].Descriptor.ShaderRegister = 0;
        params[kRootIndices].Descriptor.RegisterSpace  = 0;

        params[kRootCounters].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kRootCounters].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootCounters].Descriptor.ShaderRegister = 1;
        params[kRootCounters].Descriptor.RegisterSpace  = 0;

        params[kRootArgs].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kRootArgs].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootArgs].Descriptor.ShaderRegister = 2;
        params[kRootArgs].Descriptor.RegisterSpace  = 0;

        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 6;
        rootDesc.pParameters   = params;

        ComPtr<ID3DBlob> rsBlob;
        if (!serializeRoot(rootDesc, "D3D12SerializeRootSignature (skinned cull)", rsBlob))
            return false;
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_root)), "CreateRootSignature (skinned cull)"))
            return false;

        ComPtr<ID3DBlob> compact;
        ComPtr<ID3DBlob> args;
        if (!compileShaderFromContent("shaders/InstanceCull.hlsl", "CSCompact", "cs_5_0", compact) || !compileShaderFromContent("shaders/InstanceCull.hlsl", "CSArgs", "cs_5_0", args))
            return false;

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = m_root.Get();
        pso.CS             = { compact->GetBufferPointer(), compact->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&m_compact)), "CreateComputePipelineState (skinned cull compact)"))
            return false;
        pso.CS = { args->GetBufferPointer(), args->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&m_argsPso)), "CreateComputePipelineState (skinned cull args)"))
            return false;

        if (!createShaderVisibleHeap(device) || !createBuffers(device))
            return false;

        m_device = device;
        return true;
    }

    D3D12_GPU_VIRTUAL_ADDRESS SkinnedInstanceCull::slotBase(uint32_t frameSlot) const
    {
        if (!m_upload || frameSlot >= kFrameSlots)
            return 0;
        return m_upload->GetGPUVirtualAddress() + static_cast<UINT64>(frameSlot) * kSkinnedSlotBytes;
    }

    bool SkinnedInstanceCull::upload(uint32_t frameSlot, const MeshCullAabb* aabbs, uint32_t aabbCount,
        const GpuBatchRecord* gbuffer, uint32_t gbufferCount, const GpuBatchRecord* depth, uint32_t depthCount)
    {
        if (!m_uploadBytes || frameSlot >= kFrameSlots)
            return false;
        if (aabbCount > kStride || gbufferCount > kStride || depthCount > kStride)
            return false;
        if ((aabbCount > 0 && !aabbs) || (gbufferCount > 0 && !gbuffer) || (depthCount > 0 && !depth))
            return false;

        uint8_t* base = m_uploadBytes + static_cast<size_t>(frameSlot) * static_cast<size_t>(kSkinnedSlotBytes);
        if (aabbCount > 0)
            std::memcpy(base, aabbs, sizeof(MeshCullAabb) * aabbCount);
        if (gbufferCount > 0)
            std::memcpy(base + kSkinnedAabbBytes, gbuffer, sizeof(GpuBatchRecord) * gbufferCount);
        if (depthCount > 0)
            std::memcpy(base + kSkinnedAabbBytes + kSkinnedRecordBytes, depth, sizeof(GpuBatchRecord) * depthCount);
        return true;
    }

    D3D12_GPU_VIRTUAL_ADDRESS SkinnedInstanceCull::indexAddress(uint32_t frameSlot, uint32_t viewIndex) const
    {
        if (frameSlot >= kFrameSlots || viewIndex >= kMaxStaticViews || !m_indices[frameSlot])
            return 0;
        return m_indices[frameSlot]->GetGPUVirtualAddress() + static_cast<UINT64>(viewIndex) * kStride * sizeof(uint32_t);
    }

    ID3D12Resource* SkinnedInstanceCull::indexResource(uint32_t frameSlot) const
    {
        if (frameSlot >= kFrameSlots)
            return nullptr;
        return m_indices[frameSlot].Get();
    }

    ID3D12Resource* SkinnedInstanceCull::argumentResource(uint32_t frameSlot) const
    {
        if (frameSlot >= kFrameSlots)
            return nullptr;
        return m_args[frameSlot].Get();
    }

    ID3D12CommandSignature* SkinnedInstanceCull::signature() const
    {
        return m_signature.Get();
    }

    UINT64 SkinnedInstanceCull::argumentOffset(uint32_t viewIndex, uint32_t batchIndex)
    {
        return (static_cast<UINT64>(viewIndex) * kStride + batchIndex) * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    }

    bool SkinnedInstanceCull::dispatch(ID3D12GraphicsCommandList* cmd, uint32_t frameSlot, const CullViewDesc* views, uint32_t viewCount)
    {
        if (!cmd || !isReady() || !views || viewCount == 0 || frameSlot >= kFrameSlots)
            return false;

        bool any = false;
        for (uint32_t i = 0; i < viewCount; ++i)
        {
            if (views[i].batchCount > 0 && views[i].batchCount <= kStride && views[i].viewIndex < kMaxStaticViews)
                any = true;
        }
        if (!any)
            return false;

        const D3D12_GPU_VIRTUAL_ADDRESS base = slotBase(frameSlot);
        if (base == 0)
            return false;

        const D3D12_RESOURCE_STATES argBefore   = m_argState[frameSlot];
        const D3D12_RESOURCE_STATES indexBefore = m_indexState[frameSlot];
        if (!computable(argBefore) || !indexComputable(indexBefore))
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                DE_LOG_ERROR(LogCategory::Render, "SkinnedInstanceBuffer: skinned cull skipped; buffer state is not a compute source");
            }
            return false;
        }

        ID3D12Resource* indices  = m_indices[frameSlot].Get();
        ID3D12Resource* counters = m_counters[frameSlot].Get();
        ID3D12Resource* args     = m_args[frameSlot].Get();
        if (argBefore != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            transitionBuffer(cmd, args, argBefore, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            m_argState[frameSlot] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (indexBefore != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            transitionBuffer(cmd, indices, indexBefore, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            m_indexState[frameSlot] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        ID3D12DescriptorHeap* heaps[] = { m_heap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetComputeRootSignature(m_root.Get());
        cmd->SetComputeRootShaderResourceView(kRootAabbs, base);
        cmd->SetComputeRootUnorderedAccessView(kRootIndices, indices->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(kRootCounters, counters->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(kRootArgs, args->GetGPUVirtualAddress());

        for (uint32_t i = 0; i < viewCount; ++i)
        {
            const CullViewDesc& view = views[i];
            if (view.batchCount == 0 || view.batchCount > kStride || view.viewIndex >= kMaxStaticViews)
                continue;

            const UINT descriptor = frameSlot * kMaxStaticViews + view.viewIndex;
            D3D12_CPU_DESCRIPTOR_HANDLE gpuCpu = m_gpuCpuStart;
            D3D12_CPU_DESCRIPTOR_HANDLE cpu    = m_cpuStart;
            D3D12_GPU_DESCRIPTOR_HANDLE gpu    = m_gpuStart;
            gpuCpu.ptr += static_cast<SIZE_T>(descriptor) * m_increment;
            cpu.ptr += static_cast<SIZE_T>(descriptor) * m_increment;
            gpu.ptr += static_cast<UINT64>(descriptor) * m_increment;

            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format                      = DXGI_FORMAT_R32_UINT;
            uav.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
            uav.Buffer.FirstElement         = static_cast<UINT64>(view.viewIndex) * kStride;
            uav.Buffer.NumElements          = view.batchCount;
            m_device->CreateUnorderedAccessView(counters, nullptr, &uav, gpuCpu);
            m_device->CreateUnorderedAccessView(counters, nullptr, &uav, cpu);

            const UINT zeros[4] = { 0, 0, 0, 0 };
            cmd->ClearUnorderedAccessViewUint(gpu, cpu, counters, zeros, 0, nullptr);
            uavBarrier(cmd, counters);

            CullConstants constants{};
            for (int plane = 0; plane < 6; ++plane)
            {
                constants.planes[plane][0] = view.planes[plane][0];
                constants.planes[plane][1] = view.planes[plane][1];
                constants.planes[plane][2] = view.planes[plane][2];
                constants.planes[plane][3] = view.planes[plane][3];
            }
            constants.itemCount     = view.itemCount;
            constants.batchCount    = view.batchCount;
            constants.indexBase     = view.viewIndex * kStride;
            constants.argBase       = view.viewIndex * kStride;
            constants.counterBase   = view.viewIndex * kStride;
            constants.useDepthBatch = view.useDepthBatch;
            cmd->SetComputeRoot32BitConstants(kRootConstants, 32, &constants, 0);
            const D3D12_GPU_VIRTUAL_ADDRESS records = view.useDepthBatch != 0 ? base + kSkinnedAabbBytes + kSkinnedRecordBytes : base + kSkinnedAabbBytes;
            cmd->SetComputeRootShaderResourceView(kRootBatches, records);

            cmd->SetPipelineState(m_compact.Get());
            if (view.itemCount > 0)
            {
                const UINT groups = (view.itemCount + 63u) / 64u;
                cmd->Dispatch(groups, 1, 1);
            }
            uavBarrier(cmd, counters);

            cmd->SetPipelineState(m_argsPso.Get());
            const UINT argGroups = (view.batchCount + 63u) / 64u;
            cmd->Dispatch(argGroups, 1, 1);
            uavBarrier(cmd, counters);
        }

        D3D12_RESOURCE_BARRIER done[2]{};
        done[0].Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        done[0].UAV.pResource = args;
        done[1].Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        done[1].UAV.pResource = indices;
        cmd->ResourceBarrier(2, done);
        transitionBuffer(cmd, args, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        transitionBuffer(cmd, indices, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_argState[frameSlot]   = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        m_indexState[frameSlot] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        return true;
    }

} // namespace Dark
