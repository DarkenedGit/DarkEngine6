#include "Render/D3D12TargetBackend.h"

#include "Core/Log.h"
#include "Render/EnhancedBarrierEmitter.h"
#include "Render/GpuAllocator.h"

#include <algorithm>
#include <array>
#include <d3d12.h>
#include <string>
#include <utility>
#include <vector>
#include <wrl/client.h>

namespace Dark
{
    namespace
    {
        constexpr uint32_t kPageSize = 64;

        struct DescriptorPage
        {
            Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
            std::array<bool, kPageSize>                  used{};
            uint32_t                                     increment = 0;
        };

        struct DescriptorLease
        {
            std::shared_ptr<DescriptorPage> page;
            uint32_t                        index = 0;
            ~DescriptorLease()
            {
                if (page)
                    page->used[index] = false;
            }
            D3D12_CPU_DESCRIPTOR_HANDLE cpu() const
            {
                if (!page)
                    return {};
                D3D12_CPU_DESCRIPTOR_HANDLE result = page->heap->GetCPUDescriptorHandleForHeapStart();
                result.ptr += static_cast<SIZE_T>(index) * page->increment;
                return result;
            }
        };

        struct NativeTarget final : Rg::TargetStorage
        {
            DescriptorLease srvLease, targetLease, readOnlyLease;
            GpuResource     resource;
        };

        struct DescriptorPool
        {
            D3D12_DESCRIPTOR_HEAP_TYPE                   type;
            const wchar_t*                               name;
            std::vector<std::shared_ptr<DescriptorPage>> pages;

            bool allocate(ID3D12Device* device, DescriptorLease& out)
            {
                for (const auto& page : pages)
                {
                    for (uint32_t index = 0; index < kPageSize; ++index)
                    {
                        if (!page->used[index])
                        {
                            page->used[index] = true;
                            out.page          = page;
                            out.index         = index;
                            return true;
                        }
                    }
                }
                auto                       page = std::make_shared<DescriptorPage>();
                D3D12_DESCRIPTOR_HEAP_DESC hd{};
                hd.Type           = type;
                hd.NumDescriptors = kPageSize;
                hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                const HRESULT hr  = device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&page->heap));
                if (FAILED(hr))
                {
                    DE_LOG_ERROR(LogCategory::Render, "Graph targets: CreateDescriptorHeap type {} failed (HRESULT 0x{:08X})", static_cast<unsigned>(type), static_cast<unsigned>(hr));
                    return false;
                }
                const std::wstring pageName = std::wstring(name) + L"." + std::to_wstring(pages.size());
                page->heap->SetName(pageName.c_str());
                page->increment = device->GetDescriptorHandleIncrementSize(type);
                page->used[0]   = true;
                out.page        = page;
                out.index       = 0;
                pages.push_back(std::move(page));
                return true;
            }
        };

        std::wstring wideName(const char* name)
        {
            if (!name)
                return L"DE.Graph.Target";
            const int bytes = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, nullptr, 0);
            if (bytes <= 1)
                return L"DE.Graph.Target";
            std::wstring result(static_cast<size_t>(bytes), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, result.data(), bytes);
            result.pop_back();
            return result;
        }
    } // namespace

    struct D3D12TargetBackend::Impl
    {
        explicit Impl(GpuAllocator& gpuAllocator) :
            allocator(gpuAllocator)
        {
        }
        GpuAllocator&  allocator;
        DescriptorPool srv{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, L"DE.Graph.CpuSrv", {} };
        DescriptorPool rtv{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, L"DE.Graph.Rtv", {} };
        DescriptorPool dsv{ D3D12_DESCRIPTOR_HEAP_TYPE_DSV, L"DE.Graph.Dsv", {} };
        struct Retired
        {
            uint64_t                           fence;
            std::unique_ptr<Rg::TargetStorage> storage;
        };
        uint64_t             retireFence = UINT64_MAX; // fail safe: caller must provide an actual submission fence
        std::vector<Retired> retired;
    };

    D3D12TargetBackend::D3D12TargetBackend(GpuAllocator& allocator) :
        m_impl(std::make_unique<Impl>(allocator))
    {
    }
    D3D12TargetBackend::~D3D12TargetBackend() = default;

    std::unique_ptr<Rg::TargetStorage> D3D12TargetBackend::create(const Rg::TargetDesc& desc, Rg::TargetExtent extent, const char* name)
    {
        ID3D12Device* device = m_impl->allocator.device();
        if (!device || !m_impl->allocator.valid() || extent.width == 0 || extent.height == 0 || desc.mips == 0 || desc.arraySize == 0 || desc.format == 0 || (desc.rtv && desc.dsv) ||
            (desc.rtv && !(desc.flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)) || (desc.dsv && !(desc.flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) ||
            (desc.srv && (desc.flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)))
        {
            DE_LOG_ERROR(LogCategory::Render, "Graph targets: invalid device/description for {}", name ? name : "<unnamed>");
            return nullptr;
        }
        auto                target = std::make_unique<NativeTarget>();
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = extent.width;
        rd.Height           = extent.height;
        rd.DepthOrArraySize = desc.arraySize;
        rd.MipLevels        = desc.mips;
        rd.Format           = static_cast<DXGI_FORMAT>(desc.format);
        rd.SampleDesc       = { 1, 0 };
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags            = static_cast<D3D12_RESOURCE_FLAGS>(desc.flags);

        const DXGI_FORMAT targetFormat = static_cast<DXGI_FORMAT>(desc.targetFormat ? desc.targetFormat : desc.format);
        const DXGI_FORMAT srvFormat    = static_cast<DXGI_FORMAT>(desc.srvFormat ? desc.srvFormat : desc.format);
        D3D12_CLEAR_VALUE clear{};
        clear.Format = targetFormat;
        if (desc.dsv)
        {
            clear.DepthStencil.Depth   = desc.depth;
            clear.DepthStencil.Stencil = desc.stencil;
        }
        else
        {
            std::copy(std::begin(desc.color), std::end(desc.color), clear.Color);
        }
        const std::wstring debugName = wideName(name);
        if (!m_impl->allocator.createTexture(rd, static_cast<D3D12_RESOURCE_STATES>(desc.initialState), desc.clear ? &clear : nullptr, debugName.c_str(), target->resource))
            return nullptr;
        target->native = target->resource.get();

        if (desc.srv)
        {
            if (!m_impl->srv.allocate(device, target->srvLease))
                return nullptr;
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format                  = srvFormat;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            if (desc.arraySize == 1)
            {
                view.ViewDimension       = D3D12_SRV_DIMENSION_TEXTURE2D;
                view.Texture2D.MipLevels = desc.mips;
            }
            else
            {
                view.ViewDimension            = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                view.Texture2DArray.MipLevels = desc.mips;
                view.Texture2DArray.ArraySize = desc.arraySize;
            }
            device->CreateShaderResourceView(target->native, &view, target->srvLease.cpu());
            target->srv = target->srvLease.cpu().ptr;
        }
        // Opaque owners currently use single-mip textures. The target view addresses mip 0;
        // a future multi-mip pass must request explicit subresource views (not infer an RTV).
        if (desc.rtv)
        {
            if (!m_impl->rtv.allocate(device, target->targetLease))
                return nullptr;
            D3D12_RENDER_TARGET_VIEW_DESC view{};
            view.Format        = targetFormat;
            view.ViewDimension = desc.arraySize == 1 ? D3D12_RTV_DIMENSION_TEXTURE2D : D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            if (desc.arraySize != 1)
                view.Texture2DArray.ArraySize = desc.arraySize;
            device->CreateRenderTargetView(target->native, &view, target->targetLease.cpu());
            target->rtv = target->targetLease.cpu().ptr;
        }
        if (desc.dsv)
        {
            if (!m_impl->dsv.allocate(device, target->targetLease) || !m_impl->dsv.allocate(device, target->readOnlyLease))
                return nullptr;
            D3D12_DEPTH_STENCIL_VIEW_DESC view{};
            view.Format        = targetFormat;
            view.ViewDimension = desc.arraySize == 1 ? D3D12_DSV_DIMENSION_TEXTURE2D : D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            if (desc.arraySize != 1)
                view.Texture2DArray.ArraySize = desc.arraySize;
            device->CreateDepthStencilView(target->native, &view, target->targetLease.cpu());
            view.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
            device->CreateDepthStencilView(target->native, &view, target->readOnlyLease.cpu());
            target->dsv         = target->targetLease.cpu().ptr;
            target->readOnlyDsv = target->readOnlyLease.cpu().ptr;
        }
        // The frame ResourceStateTracker owns the target's state from here (Phase 6): owners and the
        // graph read their before-states from it.
        trackResource(target->native, static_cast<D3D12_RESOURCE_STATES>(desc.initialState));
        return target;
    }

    void D3D12TargetBackend::retire(std::unique_ptr<Rg::TargetStorage> storage)
    {
        if (!storage)
            return;
        // Nothing records on a retired target; forget it now so a new resource at the same address
        // never inherits its state.
        forgetResource(storage->native);
        m_impl->retired.push_back({ m_impl->retireFence, std::move(storage) });
    }

    void D3D12TargetBackend::retireAfter(uint64_t lastSubmittedFence)
    {
        m_impl->retireFence = lastSubmittedFence;
    }

    void D3D12TargetBackend::collect(uint64_t completedFence)
    {
        std::erase_if(m_impl->retired, [&](const Impl::Retired& retired) { return retired.fence <= completedFence; });
    }

    uint32_t D3D12TargetBackend::retiredCount() const
    {
        return static_cast<uint32_t>(m_impl->retired.size());
    }
} // namespace Dark
