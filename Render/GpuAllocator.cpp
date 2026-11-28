#include "Render/GpuAllocator.h"

#include "Render/EnhancedBarrierEmitter.h"

#include "Core/Log.h"

#include <D3D12MemAlloc.h>
#include <dxgi1_6.h>
#include <utility>

namespace Dark
{

    namespace
    {
        D3D12MA::Allocator* toAllocator(IUnknown* p)
        {
            return static_cast<D3D12MA::Allocator*>(static_cast<D3D12MA::IUnknownImpl*>(p));
        }

        Microsoft::WRL::ComPtr<IDXGIAdapter> adapterForDevice(ID3D12Device* device)
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter>  adapter;
            Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
            if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
                return adapter;
            const LUID luid = device->GetAdapterLuid();
            if (FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))))
                adapter.Reset();
            return adapter;
        }

        double toMiB(uint64_t bytes)
        {
            return static_cast<double>(bytes) / (1024.0 * 1024.0);
        }
    } // namespace

    // ---------------------------------------------------------------- GpuResource

    GpuResource::~GpuResource()
    {
        reset();
    }

    GpuResource::GpuResource(GpuResource&& other) noexcept :
        m_allocator(std::move(other.m_allocator)),
        m_pool(std::move(other.m_pool)),
        m_allocation(std::move(other.m_allocation)),
        m_resource(std::move(other.m_resource)),
        m_bytes(other.m_bytes)
    {
        other.m_bytes = 0;
    }

    GpuResource& GpuResource::operator=(GpuResource&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_allocator   = std::move(other.m_allocator);
            m_pool        = std::move(other.m_pool);
            m_allocation  = std::move(other.m_allocation);
            m_resource    = std::move(other.m_resource);
            m_bytes       = other.m_bytes;
            other.m_bytes = 0;
        }
        return *this;
    }

    void GpuResource::reset()
    {
        // The resource reference first, then the allocation (which owns the memory and its own
        // resource reference), then the pool the allocation came from, then the allocator.
        m_resource.Reset();
        m_allocation.Reset();
        m_pool.Reset();
        m_allocator.Reset();
        m_bytes = 0;
    }

    // ---------------------------------------------------------------- GpuAllocator

    GpuAllocator::~GpuAllocator()
    {
        shutdown();
    }

    bool GpuAllocator::init(ID3D12Device* device, IDXGIAdapter* adapter)
    {
        shutdown();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuAllocator: null device");
            return false;
        }
        Microsoft::WRL::ComPtr<IDXGIAdapter> found;
        if (!adapter)
        {
            found   = adapterForDevice(device);
            adapter = found.Get();
        }
        if (!adapter)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuAllocator: no DXGI adapter for the device LUID");
            return false;
        }

        D3D12MA::ALLOCATOR_DESC desc{};
        desc.Flags                    = D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS;
        desc.pDevice                  = device;
        desc.pAdapter                 = adapter;
        D3D12MA::Allocator* allocator = nullptr;
        const HRESULT       hr        = D3D12MA::CreateAllocator(&desc, &allocator);
        if (FAILED(hr) || !allocator)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuAllocator: D3D12MA::CreateAllocator failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
            return false;
        }
        m_allocator.Attach(static_cast<D3D12MA::IUnknownImpl*>(allocator));
        m_device = device;
        DE_LOG_INFO(LogCategory::Render, "GpuAllocator: D3D12MA ready (UMA {}, tight alignment {})", allocator->IsUMA() ? "yes" : "no", allocator->IsTightAlignmentSupported() ? "yes" : "no");
        return true;
    }

    void GpuAllocator::shutdown()
    {
        m_allocator.Reset(); // live GpuResources keep their own reference
        m_device.Reset();
    }

    bool GpuAllocator::createTexture(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initial, const D3D12_CLEAR_VALUE* clear, const wchar_t* name, GpuResource& out)
    {
        out.reset();
        if (!m_allocator)
            return false;
        D3D12MA::ALLOCATION_DESC alloc{};
        alloc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
        // Placed render targets and depth live in the not-zeroed default pools. The debug layer then
        // rejects the first draw until a discard, clear, or copy (error 1422). A committed resource is
        // zeroed, so the creation state is real contents.
        if ((desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) != 0)
            alloc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;
        D3D12MA::Allocation* result = nullptr;
        HRESULT              hr     = E_FAIL;
        if (createNeedsLayout(desc, initial))
        {
            // Born in the enhanced layout. A legacy creation state makes the first Barrier() an interop
            // (debug layer error 1350) unless the texture is in COMMON.
            const D3D12_RESOURCE_DESC1 desc1 = resourceDesc1(desc);
            hr = toAllocator(m_allocator.Get())->CreateResource3(&alloc, &desc1, initialLayout(initial), clear, 0, nullptr, &result, IID_NULL, nullptr);
        }
        else
        {
            hr = toAllocator(m_allocator.Get())->CreateResource(&alloc, &desc, initial, clear, &result, IID_NULL, nullptr);
        }
        if (FAILED(hr) || !result || !result->GetResource())
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuAllocator: CreateResource failed for {}x{} format {} (HRESULT 0x{:08X})", static_cast<uint64_t>(desc.Width), desc.Height,
                         static_cast<unsigned>(desc.Format), static_cast<unsigned>(hr));
            if (result)
                result->Release();
            return false;
        }
        if (name)
        {
            result->SetName(name);
            result->GetResource()->SetName(name);
        }
        out.m_allocator = m_allocator;
        out.m_resource  = result->GetResource();
        out.m_bytes     = result->GetSize();
        out.m_allocation.Attach(static_cast<D3D12MA::IUnknownImpl*>(result));
        return true;
    }

    bool GpuAllocator::createBuffer(uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES initial, D3D12_RESOURCE_FLAGS flags, const wchar_t* name, GpuResource& out)
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width            = bytes;
        desc.Height           = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc       = { 1, 0 };
        desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags            = flags;

        out.reset();
        if (!m_allocator)
            return false;
        D3D12MA::ALLOCATION_DESC alloc{};
        alloc.HeapType              = heap;
        D3D12MA::Allocation* result = nullptr;
        const HRESULT        hr     = toAllocator(m_allocator.Get())->CreateResource(&alloc, &desc, initial, nullptr, &result, IID_NULL, nullptr);
        if (FAILED(hr) || !result || !result->GetResource())
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuAllocator: CreateResource failed for a {}-byte buffer (HRESULT 0x{:08X})", bytes, static_cast<unsigned>(hr));
            if (result)
                result->Release();
            return false;
        }
        if (name)
        {
            result->SetName(name);
            result->GetResource()->SetName(name);
        }
        out.m_allocator = m_allocator;
        out.m_resource  = result->GetResource();
        out.m_bytes     = result->GetSize();
        out.m_allocation.Attach(static_cast<D3D12MA::IUnknownImpl*>(result));
        return true;
    }

    bool GpuAllocator::allocatePlacementMemory(uint64_t bytes, const wchar_t* name, GpuResource& out)
    {
        out.reset();
        if (!m_allocator || bytes == 0)
            return false;
        // One block per pool, sized to the request, so the heap is exactly the planned capacity.
        D3D12MA::POOL_DESC poolDesc{};
        poolDesc.HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        poolDesc.HeapFlags           = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
        poolDesc.BlockSize           = bytes;
        poolDesc.MinBlockCount       = 0;
        poolDesc.MaxBlockCount       = 1;
        D3D12MA::Pool* pool          = nullptr;
        HRESULT        hr            = toAllocator(m_allocator.Get())->CreatePool(&poolDesc, &pool);
        if (FAILED(hr) || !pool)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuAllocator: CreatePool failed for {:.1f} MiB of placement memory (HRESULT 0x{:08X})", toMiB(bytes), static_cast<unsigned>(hr));
            return false;
        }
        Microsoft::WRL::ComPtr<IUnknown> poolRef;
        poolRef.Attach(static_cast<D3D12MA::IUnknownImpl*>(pool));
        if (name)
            pool->SetName(name);

        D3D12MA::ALLOCATION_DESC alloc{};
        alloc.CustomPool = pool;
        D3D12_RESOURCE_ALLOCATION_INFO info{};
        info.SizeInBytes            = bytes;
        info.Alignment              = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        D3D12MA::Allocation* result = nullptr;
        hr                          = toAllocator(m_allocator.Get())->AllocateMemory(&alloc, &info, &result);
        if (FAILED(hr) || !result)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuAllocator: AllocateMemory failed for {:.1f} MiB of placement memory (HRESULT 0x{:08X})", toMiB(bytes), static_cast<unsigned>(hr));
            if (result)
                result->Release();
            return false;
        }
        if (name)
            result->SetName(name);
        out.m_allocator = m_allocator;
        out.m_pool      = std::move(poolRef);
        out.m_bytes     = result->GetSize();
        out.m_allocation.Attach(static_cast<D3D12MA::IUnknownImpl*>(result));
        return true;
    }

    bool GpuAllocator::createPlacedTexture(const GpuResource& memory, uint64_t offset, const D3D12_RESOURCE_DESC& desc, const D3D12_CLEAR_VALUE* clear, const wchar_t* name, GpuResource& out)
    {
        out.reset();
        if (!m_allocator || !memory.allocation())
            return false;
        D3D12MA::Allocation*                   allocation = static_cast<D3D12MA::Allocation*>(static_cast<D3D12MA::IUnknownImpl*>(memory.allocation()));
        const D3D12_RESOURCE_DESC1             desc1      = resourceDesc1(desc);
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        const HRESULT hr = toAllocator(m_allocator.Get())->CreateAliasingResource2(allocation, offset, &desc1, D3D12_BARRIER_LAYOUT_UNDEFINED, clear, 0, nullptr, IID_PPV_ARGS(&resource));
        if (FAILED(hr) || !resource)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuAllocator: CreateAliasingResource2 failed for {}x{} format {} at offset {} (HRESULT 0x{:08X})", static_cast<uint64_t>(desc.Width), desc.Height,
                         static_cast<unsigned>(desc.Format), offset, static_cast<unsigned>(hr));
            return false;
        }
        if (name)
            resource->SetName(name);
        uint64_t bytes = 0, alignment = 0;
        allocationInfo(desc, bytes, alignment);
        out.m_allocator  = m_allocator;
        out.m_pool       = memory.m_pool;
        out.m_allocation = memory.m_allocation;
        out.m_resource   = std::move(resource);
        out.m_bytes      = bytes;
        return true;
    }

    bool GpuAllocator::allocationInfo(const D3D12_RESOURCE_DESC& desc, uint64_t& bytes, uint64_t& alignment) const
    {
        bytes     = 0;
        alignment = 0;
        if (!m_device)
            return false;
        const D3D12_RESOURCE_ALLOCATION_INFO info = m_device->GetResourceAllocationInfo(0, 1, &desc);
        if (info.SizeInBytes == 0 || info.SizeInBytes == UINT64_MAX)
            return false;
        bytes     = info.SizeInBytes;
        alignment = info.Alignment;
        return true;
    }

    GpuMemoryStats GpuAllocator::stats() const
    {
        GpuMemoryStats s;
        if (!m_allocator)
            return s;
        D3D12MA::Budget local{};
        D3D12MA::Budget nonLocal{};
        toAllocator(m_allocator.Get())->GetBudget(&local, &nonLocal);
        s.allocations     = local.Stats.AllocationCount + nonLocal.Stats.AllocationCount;
        s.blocks          = local.Stats.BlockCount + nonLocal.Stats.BlockCount;
        s.allocationBytes = local.Stats.AllocationBytes + nonLocal.Stats.AllocationBytes;
        s.blockBytes      = local.Stats.BlockBytes + nonLocal.Stats.BlockBytes;
        s.localUsage      = local.UsageBytes;
        s.localBudget     = local.BudgetBytes;
        return s;
    }

    void GpuAllocator::logStats(const char* when) const
    {
        if (!m_allocator)
            return;
        const GpuMemoryStats s = stats();
        DE_LOG_INFO(LogCategory::Render, "GpuAllocator ({}): {} allocations in {} blocks, {:.1f} MiB allocated / {:.1f} MiB in blocks; local usage {:.1f} / budget {:.1f} MiB", when ? when : "stats",
                    s.allocations, s.blocks, toMiB(s.allocationBytes), toMiB(s.blockBytes), toMiB(s.localUsage), toMiB(s.localBudget));
    }

} // namespace Dark
