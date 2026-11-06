#include "Render/MovieFrameTexture.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"
#include "Core/Log.h"

#include <cstring>

namespace Dark
{

    static_assert(MovieFrameTexture::kSlotCount == Renderer::kFrameCount, "movie slots match the swap-chain frame count");

    namespace
    {

        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = resource;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter  = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &barrier);
        }

        void CreateBgraSrv(ID3D12Device* device, ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE dest)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format                    = DXGI_FORMAT_B8G8R8A8_UNORM;
            srv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MostDetailedMip = 0;
            srv.Texture2D.MipLevels       = 1;
            srv.Texture2D.PlaneSlice      = 0;
            device->CreateShaderResourceView(resource, &srv, dest);
        }

        void CreateBgraUav(ID3D12Device* device, ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE dest)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format               = DXGI_FORMAT_B8G8R8A8_UNORM;
            uav.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
            uav.Texture2D.MipSlice   = 0;
            uav.Texture2D.PlaneSlice = 0;
            device->CreateUnorderedAccessView(resource, nullptr, &uav, dest);
        }

    } // namespace

    MovieFrameTexture::~MovieFrameTexture()
    {
        clear();
    }

    void MovieFrameTexture::clear()
    {
        for (uint32_t i = 0; i < kSlotCount; ++i)
        {
            if (m_upload[i] && m_mapped[i])
                m_upload[i]->Unmap(0, nullptr);
            m_mapped[i] = nullptr;
        }

        // COPY and DIRECT work may still reference these. Release waits in deferRelease.
        if (m_renderer)
        {
            for (uint32_t i = 0; i < kSlotCount; ++i)
            {
                m_renderer->deferRelease(m_frame[i].Get());
                m_renderer->deferRelease(m_upload[i].Get());
            }
            m_renderer->deferRelease(m_heap.Get());
            m_renderer->deferRelease(m_nv12Heap.Get());
            m_renderer->deferRelease(m_root.Get());
            m_renderer->deferRelease(m_pso.Get());
        }

        for (uint32_t i = 0; i < kSlotCount; ++i)
        {
            m_upload[i].Reset();
            m_frame[i].Reset();
            m_state[i] = D3D12_RESOURCE_STATE_COMMON;
        }
        m_heap.Reset();
        m_nv12Heap.Reset();
        m_root.Reset();
        m_pso.Reset();
        m_cpuStart     = {};
        m_gpuStart     = {};
        m_nv12CpuStart = {};
        m_incr         = 0;
        m_footprint    = {};
        m_width        = 0;
        m_height       = 0;
        m_hardware     = false;
        m_renderer     = nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE MovieFrameTexture::cpuAt(uint32_t index) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_cpuStart;
        h.ptr += static_cast<SIZE_T>(index) * static_cast<SIZE_T>(m_incr);
        return h;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE MovieFrameTexture::gpuAt(uint32_t index) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE h = m_gpuStart;
        h.ptr += static_cast<UINT64>(index) * static_cast<UINT64>(m_incr);
        return h;
    }

    uint32_t MovieFrameTexture::slotSrv(uint32_t slot) const
    {
        return slot * kPerSlotViews;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE MovieFrameTexture::nv12Cpu(uint32_t surface, uint32_t plane) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_nv12CpuStart;
        h.ptr += static_cast<SIZE_T>(surface * 2u + plane) * static_cast<SIZE_T>(m_incr);
        return h;
    }

    uint32_t MovieFrameTexture::dispatchIndex(uint32_t slot) const
    {
        return kSlotCount * kPerSlotViews + slot * kDispatchViews;
    }

    bool MovieFrameTexture::create(Renderer& renderer, uint32_t width, uint32_t height, bool hardware)
    {
        if (m_frame[0])
        {
            DE_LOG_ERROR(LogCategory::Render, "MovieFrameTexture::create: already created");
            return false;
        }

        ID3D12Device* device = renderer.device();
        const uint64_t tight = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4u;
        if (!device || width == 0 || height == 0 || tight == 0 || tight > 0xffffffffu)
        {
            DE_LOG_ERROR(LogCategory::Render, "MovieFrameTexture::create: invalid size or device");
            return false;
        }
        if (hardware && ((width | height) & 1u) != 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "MovieFrameTexture::create: NV12 size must be even");
            return false;
        }

        m_renderer = &renderer;
        m_hardware = hardware;
        m_width    = width;
        m_height   = height;

        if (hardware)
        {
            D3D12_DESCRIPTOR_RANGE ranges[2]{};
            ranges[0].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            ranges[0].NumDescriptors                    = 2;
            ranges[0].BaseShaderRegister                = 0;
            ranges[0].OffsetInDescriptorsFromTableStart = 0;
            ranges[1].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            ranges[1].NumDescriptors                    = 1;
            ranges[1].BaseShaderRegister                = 0;
            ranges[1].OffsetInDescriptorsFromTableStart = 2;

            D3D12_ROOT_PARAMETER params[2]{};
            params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
            params[0].Constants.ShaderRegister = 0;
            params[0].Constants.Num32BitValues = 2;
            params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
            params[1].DescriptorTable.NumDescriptorRanges = 2;
            params[1].DescriptorTable.pDescriptorRanges   = ranges;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = 2;
            rsDesc.pParameters   = params;
            rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            ComPtr<ID3DBlob> rsBlob;
            ComPtr<ID3DBlob> rsErr;
            if (FailedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (movie nv12)"))
            {
                if (rsErr)
                    DE_LOG_ERROR(LogCategory::Render, "MovieNv12 RS: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
                clear();
                return false;
            }
            if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_root)), "CreateRootSignature (movie nv12)"))
            {
                clear();
                return false;
            }
            m_root->SetName(L"DE.MovieNv12Root");

            ComPtr<ID3DBlob> cs;
            if (!compileShaderFromContent("shaders/MovieNv12Convert.hlsl", "CSMain", "cs_5_0", cs))
            {
                clear();
                return false;
            }
            D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
            pso.pRootSignature = m_root.Get();
            pso.CS             = { cs->GetBufferPointer(), cs->GetBufferSize() };
            if (FailedHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&m_pso)), "CreateComputePipelineState (movie nv12)"))
            {
                clear();
                return false;
            }
            m_pso->SetName(L"DE.MovieNv12Pso");
        }

        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width            = width;
        texDesc.Height           = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels        = 1;
        texDesc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        texDesc.SampleDesc       = { 1, 0 };
        texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        for (uint32_t i = 0; i < kSlotCount; ++i)
        {
            if (FailedHr(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_frame[i])),
                         "CreateCommittedResource movie frame"))
            {
                clear();
                return false;
            }
            m_frame[i]->SetName(i == 0 ? L"DE.MovieFrame0" : L"DE.MovieFrame1");
            m_state[i] = D3D12_RESOURCE_STATE_COMMON;
        }

        UINT   numRows     = 0;
        UINT64 uploadBytes = 0;
        device->GetCopyableFootprints(&texDesc, 0, 1, 0, &m_footprint, &numRows, nullptr, &uploadBytes);
        if (uploadBytes == 0 || numRows != height || m_footprint.Footprint.RowPitch < width * 4u)
        {
            DE_LOG_ERROR(LogCategory::Render, "MovieFrameTexture::create: bad footprint");
            clear();
            return false;
        }

        const uint32_t descriptorCount = hardware ? (kSlotCount * kPerSlotViews + kSlotCount * kDispatchViews) : (kSlotCount * kPerSlotViews);
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = descriptorCount;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_heap)), "CreateDescriptorHeap (movie frame)"))
        {
            clear();
            return false;
        }
        m_heap->SetName(L"DE.MovieFrameHeap");
        m_incr     = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
        m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();

        if (hardware)
        {
            // Shader-visible heaps are illegal CopyDescriptors sources.
            D3D12_DESCRIPTOR_HEAP_DESC cpuDesc{};
            cpuDesc.NumDescriptors = kNv12Surfaces * 2u;
            cpuDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            cpuDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (FailedHr(device->CreateDescriptorHeap(&cpuDesc, IID_PPV_ARGS(&m_nv12Heap)), "CreateDescriptorHeap (movie nv12 cpu)"))
            {
                clear();
                return false;
            }
            m_nv12Heap->SetName(L"DE.MovieNv12CpuHeap");
            m_nv12CpuStart = m_nv12Heap->GetCPUDescriptorHandleForHeapStart();
        }

        for (uint32_t i = 0; i < kSlotCount; ++i)
        {
            CreateBgraSrv(device, m_frame[i].Get(), cpuAt(slotSrv(i)));
            CreateBgraUav(device, m_frame[i].Get(), cpuAt(slotSrv(i) + 1));
            if (hardware)
                CreateBgraUav(device, m_frame[i].Get(), cpuAt(dispatchIndex(i) + 2));
        }

        if (!hardware)
        {
            D3D12_HEAP_PROPERTIES uploadHeap{};
            uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC uploadDesc{};
            uploadDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            uploadDesc.Width            = uploadBytes;
            uploadDesc.Height           = 1;
            uploadDesc.DepthOrArraySize = 1;
            uploadDesc.MipLevels        = 1;
            uploadDesc.Format           = DXGI_FORMAT_UNKNOWN;
            uploadDesc.SampleDesc       = { 1, 0 };
            uploadDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            const D3D12_RANGE noRead{ 0, 0 };
            for (uint32_t i = 0; i < kSlotCount; ++i)
            {
                if (FailedHr(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_upload[i])),
                             "CreateCommittedResource movie upload"))
                {
                    clear();
                    return false;
                }
                if (FailedHr(m_upload[i]->Map(0, &noRead, reinterpret_cast<void**>(&m_mapped[i])), "Map movie upload"))
                {
                    clear();
                    return false;
                }
                m_upload[i]->SetName(i == 0 ? L"DE.MovieUpload0" : L"DE.MovieUpload1");
            }
        }

        return true;
    }

    bool MovieFrameTexture::upload(const void* bgra, uint32_t byteCount)
    {
        const uint32_t slot = m_renderer ? m_renderer->frameIndex() : kSlotCount;
        if (m_hardware || !m_renderer || slot >= kSlotCount || !m_frame[slot] || !m_upload[slot] || !m_mapped[slot])
        {
            DE_LOG_ERROR(LogCategory::Render, "MovieFrameTexture::upload: software path only");
            return false;
        }

        const uint32_t rowBytes = m_width * 4u;
        const uint64_t tight    = static_cast<uint64_t>(rowBytes) * static_cast<uint64_t>(m_height);
        if (!bgra || tight != byteCount)
        {
            DE_LOG_ERROR(LogCategory::Render, "MovieFrameTexture::upload: expected {} BGRA bytes", tight);
            return false;
        }

        const UINT pitch = m_footprint.Footprint.RowPitch;
        if (pitch < rowBytes)
        {
            DE_LOG_ERROR(LogCategory::Render, "MovieFrameTexture::upload: footprint pitch {} < {}", pitch, rowBytes);
            return false;
        }

        uint8_t*       dst = m_mapped[slot] + m_footprint.Offset;
        const uint8_t* src = static_cast<const uint8_t*>(bgra);
        if (pitch == rowBytes)
            std::memcpy(dst, src, static_cast<size_t>(tight));
        else
        {
            for (uint32_t y = 0; y < m_height; ++y)
                std::memcpy(dst + static_cast<size_t>(y) * pitch, src + static_cast<size_t>(y) * rowBytes, rowBytes);
        }

        if (!m_renderer->submitTextureUpdate(m_frame[slot].Get(), m_state[slot], m_upload[slot].Get(), m_footprint))
            return false;
        m_state[slot] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        return true;
    }

    bool MovieFrameTexture::placeNv12(uint32_t surface, ID3D12Resource* nv12)
    {
        if (!m_hardware || !m_nv12Heap || !m_renderer || surface >= kNv12Surfaces || !nv12)
        {
            DE_LOG_ERROR(LogCategory::Render, "MovieFrameTexture::placeNv12: invalid surface");
            return false;
        }
        ID3D12Device* device = m_renderer->device();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "MovieFrameTexture::placeNv12: null device");
            return false;
        }

        const D3D12_RESOURCE_DESC desc = nv12->GetDesc();
        if (desc.Format != DXGI_FORMAT_NV12 || desc.Width != m_width || desc.Height != m_height)
        {
            DE_LOG_ERROR(LogCategory::Render, "MovieFrameTexture::placeNv12: need NV12 {}x{}", m_width, m_height);
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC y{};
        y.Format                    = DXGI_FORMAT_R8_UNORM;
        y.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        y.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        y.Texture2D.MostDetailedMip = 0;
        y.Texture2D.MipLevels       = 1;
        y.Texture2D.PlaneSlice      = 0;

        D3D12_SHADER_RESOURCE_VIEW_DESC uv = y;
        uv.Format               = DXGI_FORMAT_R8G8_UNORM;
        uv.Texture2D.PlaneSlice = 1;

        device->CreateShaderResourceView(nv12, &y, nv12Cpu(surface, 0));
        device->CreateShaderResourceView(nv12, &uv, nv12Cpu(surface, 1));
        return true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE MovieFrameTexture::nv12PlaneCpu(uint32_t surface, uint32_t plane) const
    {
        if (!m_hardware || !m_nv12Heap || surface >= kNv12Surfaces || plane >= 2u)
            return {};
        return nv12Cpu(surface, plane);
    }

    bool MovieFrameTexture::convertNv12(D3D12_CPU_DESCRIPTOR_HANDLE ySrv, D3D12_CPU_DESCRIPTOR_HANDLE uvSrv, uint32_t colorMode, uint32_t flip180, bool decodeFenceComplete)
    {
        if (!m_hardware || !m_pso || !m_root || !m_heap || !m_renderer || !m_frame[0])
        {
            DE_LOG_ERROR(LogCategory::Render, "MovieFrameTexture::convertNv12: hardware path only");
            return false;
        }
        // Incomplete decode: leave both slots where they are. Do not record.
        if (!decodeFenceComplete)
            return false;

        const uint32_t slot = m_renderer->frameIndex();
        if (slot >= kSlotCount || !m_frame[slot])
        {
            DE_LOG_ERROR(LogCategory::Render, "MovieFrameTexture::convertNv12: bad slot");
            return false;
        }
        if (colorMode > kColorFull709 || ySrv.ptr == 0 || uvSrv.ptr == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "MovieFrameTexture::convertNv12: bad mode or SRV");
            return false;
        }
        const D3D12_RESOURCE_STATES before = m_state[slot];
        if (before != D3D12_RESOURCE_STATE_COMMON && before != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
        {
            DE_LOG_ERROR(LogCategory::Render, "MovieFrameTexture::convertNv12: unexpected state");
            return false;
        }

        ID3D12Device* device = m_renderer->device();
        // Start-of-call Reset. One record after this; do not Reset again.
        ID3D12GraphicsCommandList* cmd = m_renderer->beginTexUpdateDirect();
        if (!cmd || !device)
            return false;

        const uint32_t table = dispatchIndex(slot);
        device->CopyDescriptorsSimple(1, cpuAt(table), ySrv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        device->CopyDescriptorsSimple(1, cpuAt(table + 1), uvSrv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        {
            GpuScope scope(cmd, "MovieNv12", ProfileColor::Hud);
            Transition(cmd, m_frame[slot].Get(), before, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            cmd->SetComputeRootSignature(m_root.Get());
            cmd->SetPipelineState(m_pso.Get());
            ID3D12DescriptorHeap* heaps[] = { m_heap.Get() };
            cmd->SetDescriptorHeaps(1, heaps);
            const uint32_t constants[2] = { colorMode, flip180 };
            cmd->SetComputeRoot32BitConstants(0, 2, constants, 0);
            cmd->SetComputeRootDescriptorTable(1, gpuAt(table));

            const UINT groupsX = (m_width + 7u) / 8u;
            const UINT groupsY = (m_height + 7u) / 8u;
            cmd->Dispatch(groupsX, groupsY, 1);

            Transition(cmd, m_frame[slot].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }

        if (!m_renderer->executeTexUpdateDirect())
            return false;
        m_state[slot] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        return true;
    }

    ID3D12Resource* MovieFrameTexture::resource(uint32_t slot) const
    {
        if (slot >= kSlotCount)
            return nullptr;
        return m_frame[slot].Get();
    }

    ID3D12DescriptorHeap* MovieFrameTexture::shaderHeap() const
    {
        return m_heap.Get();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE MovieFrameTexture::srvCpu(uint32_t slot) const
    {
        if (!m_heap || slot >= kSlotCount)
            return {};
        return cpuAt(slotSrv(slot));
    }

    D3D12_GPU_DESCRIPTOR_HANDLE MovieFrameTexture::srvGpu(uint32_t slot) const
    {
        if (!m_heap || slot >= kSlotCount)
            return {};
        return gpuAt(slotSrv(slot));
    }

    D3D12_RESOURCE_STATES MovieFrameTexture::state(uint32_t slot) const
    {
        if (slot >= kSlotCount)
            return D3D12_RESOURCE_STATE_COMMON;
        return m_state[slot];
    }

} // namespace Dark
