#include "Render/TransientHeap.h"

#include "Core/Log.h"

#include <algorithm>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace Dark
{

    namespace
    {
        // Typeless resource format and the depth format a DSV uses, for depth targets that are sampled.
        struct DepthFormats
        {
            DXGI_FORMAT depth, typeless, srv;
        };
        constexpr DepthFormats kDepthFormats[] = {
            { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT },
            { DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R24_UNORM_X8_TYPELESS },
            { DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_UNORM },
            { DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS },
        };

        const DepthFormats* depthFormats(DXGI_FORMAT f)
        {
            for (const DepthFormats& d : kDepthFormats)
                if (d.depth == f || d.typeless == f)
                    return &d;
            return nullptr;
        }

        double toMiB(uint64_t bytes)
        {
            return static_cast<double>(bytes) / (1024.0 * 1024.0);
        }
    } // namespace

    D3D12_RESOURCE_DESC transientResourceDesc(uint32_t width, uint32_t height, const Rg::TextureDesc& desc, uint8_t usage)
    {
        D3D12_RESOURCE_DESC rd{};
        const DXGI_FORMAT   format = static_cast<DXGI_FORMAT>(desc.format);
        if (width == 0 || height == 0 || format == DXGI_FORMAT_UNKNOWN)
            return rd;
        const bool          depth = (usage & Rg::kUsageDepthStencil) != 0;
        const DepthFormats* df    = depthFormats(format);
        if (depth && !df)
            return rd; // a depth target needs a depth (or matching typeless) format
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = width;
        rd.Height           = height;
        rd.DepthOrArraySize = desc.arraySize == 0 ? 1 : desc.arraySize;
        rd.MipLevels        = desc.mips == Rg::kAllMips ? 0 : (desc.mips == 0 ? 1 : desc.mips);
        rd.Format           = depth && (usage & Rg::kUsageShaderResource) != 0 ? df->typeless : format;
        rd.SampleDesc       = { 1, 0 };
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags            = D3D12_RESOURCE_FLAG_NONE;
        if ((usage & Rg::kUsageRenderTarget) != 0)
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (depth)
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if ((usage & Rg::kUsageUnordered) != 0)
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (depth && (usage & Rg::kUsageShaderResource) == 0)
            rd.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        return rd;
    }

    DXGI_FORMAT transientSrvFormat(const Rg::TextureDesc& desc)
    {
        const DXGI_FORMAT format = static_cast<DXGI_FORMAT>(desc.format);
        if (desc.viewFormat != 0)
            return static_cast<DXGI_FORMAT>(desc.viewFormat);
        if (const DepthFormats* df = depthFormats(format))
            return df->srv;
        return format;
    }

    DXGI_FORMAT transientDsvFormat(const Rg::TextureDesc& desc)
    {
        const DepthFormats* df = depthFormats(static_cast<DXGI_FORMAT>(desc.format));
        return df ? df->depth : DXGI_FORMAT_UNKNOWN;
    }

    struct TransientHeap::Impl
    {
        struct Placed
        {
            uint16_t         resource = 0;
            GpuResource      texture;
            Rg::TextureViews views;
        };
        struct Retired
        {
            GpuResource resource;
            uint64_t    fence = 0;
        };

        GpuAllocator*                                allocator = nullptr;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap, dsvHeap, srvHeap;
        uint32_t                                     rtvStep = 0, dsvStep = 0, srvStep = 0;
        GpuResource                                  memory;
        std::vector<Placed>                          placed;
        std::vector<Retired>                         retired;
        uint64_t                                     retireFence = 0;
        uint64_t                                     planKey     = 0;
        uint64_t                                     heapBytes   = 0;
        bool                                         havePlan    = false;
        bool                                         fallback    = false;
        uint32_t                                     rebuilds    = 0;
        uint32_t                                     logged      = 0; // one bit per message kind

        bool logOnce(uint32_t bit)
        {
            const bool first = (logged & bit) == 0;
            logged |= bit;
            return first;
        }

        void retire(GpuResource&& r)
        {
            if (r.allocation() || r.get())
                retired.push_back({ std::move(r), retireFence });
        }

        void retirePlaced()
        {
            for (Placed& p : placed)
                retire(std::move(p.texture));
            placed.clear();
            havePlan = false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpu(ID3D12DescriptorHeap* heap, uint32_t step, uint32_t index) const
        {
            D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
            h.ptr += static_cast<SIZE_T>(index) * step;
            return h;
        }

        // Views for the transient in slot `i` of the set.
        Rg::TextureViews makeViews(uint32_t i, ID3D12Resource* res, const Rg::TextureDesc& desc, uint8_t usage)
        {
            ID3D12Device*    device = allocator->device();
            Rg::TextureViews v;
            const UINT       slices = desc.arraySize == 0 ? 1u : desc.arraySize;
            if ((usage & Rg::kUsageShaderResource) != 0)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
                sd.Format                  = transientSrvFormat(desc);
                sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                if (slices > 1)
                {
                    sd.ViewDimension            = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                    sd.Texture2DArray.MipLevels = UINT(-1);
                    sd.Texture2DArray.ArraySize = slices;
                }
                else
                {
                    sd.ViewDimension       = D3D12_SRV_DIMENSION_TEXTURE2D;
                    sd.Texture2D.MipLevels = UINT(-1);
                }
                const D3D12_CPU_DESCRIPTOR_HANDLE h = cpu(srvHeap.Get(), srvStep, i);
                device->CreateShaderResourceView(res, &sd, h);
                v.srv = h.ptr;
            }
            if ((usage & Rg::kUsageRenderTarget) != 0)
            {
                D3D12_RENDER_TARGET_VIEW_DESC rd{};
                rd.Format = static_cast<DXGI_FORMAT>(desc.viewFormat != 0 ? desc.viewFormat : desc.format);
                if (slices > 1)
                {
                    rd.ViewDimension            = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                    rd.Texture2DArray.ArraySize = slices;
                }
                else
                {
                    rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                }
                const D3D12_CPU_DESCRIPTOR_HANDLE h = cpu(rtvHeap.Get(), rtvStep, i);
                device->CreateRenderTargetView(res, &rd, h);
                v.rtv = h.ptr;
            }
            if ((usage & Rg::kUsageDepthStencil) != 0)
            {
                D3D12_DEPTH_STENCIL_VIEW_DESC dd{};
                dd.Format = transientDsvFormat(desc);
                if (slices > 1)
                {
                    dd.ViewDimension            = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                    dd.Texture2DArray.ArraySize = slices;
                }
                else
                {
                    dd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                }
                const D3D12_CPU_DESCRIPTOR_HANDLE w = cpu(dsvHeap.Get(), dsvStep, 2 * i);
                device->CreateDepthStencilView(res, &dd, w);
                dd.Flags                            = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
                const D3D12_CPU_DESCRIPTOR_HANDLE r = cpu(dsvHeap.Get(), dsvStep, 2 * i + 1);
                device->CreateDepthStencilView(res, &dd, r);
                v.dsv         = w.ptr;
                v.dsvReadOnly = r.ptr;
            }
            return v;
        }
    };

    TransientHeap::TransientHeap() :
        m_impl(std::make_unique<Impl>())
    {
    }

    TransientHeap::~TransientHeap() = default;

    bool TransientHeap::init(GpuAllocator& allocator)
    {
        shutdown();
        ID3D12Device* device = allocator.device();
        if (!device)
            return false;
        Impl&      m    = *m_impl;
        const auto heap = [&](D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& out, uint32_t& step)
        {
            D3D12_DESCRIPTOR_HEAP_DESC hd{};
            hd.Type           = type;
            hd.NumDescriptors = count;
            if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&out))))
            {
                DE_LOG_ERROR(LogCategory::Render, "TransientHeap: CreateDescriptorHeap type {} failed", static_cast<unsigned>(type));
                return false;
            }
            step = device->GetDescriptorHandleIncrementSize(type);
            return true;
        };
        if (!heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kMaxTransients, m.rtvHeap, m.rtvStep) || !heap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2 * kMaxTransients, m.dsvHeap, m.dsvStep) ||
            !heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxTransients, m.srvHeap, m.srvStep))
        {
            shutdown();
            return false;
        }
        m.allocator = &allocator;
        return true;
    }

    void TransientHeap::shutdown()
    {
        Impl& m = *m_impl;
        m.placed.clear();
        m.retired.clear();
        m.memory.reset();
        m.rtvHeap.Reset();
        m.dsvHeap.Reset();
        m.srvHeap.Reset();
        m.allocator = nullptr;
        m.havePlan  = false;
        m.fallback  = false;
    }

    Rg::TransientSize TransientHeap::sizeTransient(void* user, uint32_t width, uint32_t height, const Rg::TextureDesc& desc, uint8_t usage)
    {
        const TransientHeap* self = static_cast<const TransientHeap*>(user);
        Rg::TransientSize    s;
        if (!self || !self->m_impl->allocator)
            return s;
        const D3D12_RESOURCE_DESC rd = transientResourceDesc(width, height, desc, usage);
        if (rd.Width == 0)
            return s;
        self->m_impl->allocator->allocationInfo(rd, s.bytes, s.alignment);
        return s;
    }

    bool TransientHeap::realize(Rg::RenderGraph& graph)
    {
        Impl&                 m = *m_impl;
        std::vector<uint16_t> live;
        for (uint16_t r = 0; r < graph.resourceCount(); ++r)
        {
            const Rg::ResourceInfo info = graph.resourceInfo(r);
            if (info.lifetime != Rg::Lifetime::Transient || !info.used)
                continue;
            if (info.kind != Rg::ResourceKind::Texture)
            {
                if (m.logOnce(1))
                    DE_LOG_ERROR(LogCategory::Render, "TransientHeap: transient buffer '{}' is not supported", info.name);
                return false;
            }
            live.push_back(r);
        }
        if (live.empty())
            return true; // nothing to place; held memory stays for the next plan that needs it
        if (!m.allocator || live.size() > kMaxTransients)
        {
            if (m.logOnce(2))
                DE_LOG_ERROR(LogCategory::Render, "TransientHeap: cannot place {} transients (limit {}, {})", live.size(), kMaxTransients, m.allocator ? "initialized" : "not initialized");
            return false;
        }

        const bool reuse = m.havePlan && m.planKey == graph.planKey() && m.heapBytes == graph.transientHeapBytes() && m.placed.size() == live.size();
        if (!reuse)
        {
            m.retirePlaced();
            const uint64_t needed   = graph.transientHeapBytes();
            const uint64_t capacity = transientCapacity(needed, m.memory.allocation() ? m.memory.bytes() : 0);
            m.fallback              = capacity == 0;
            if (m.fallback)
            {
                if (m.logOnce(4))
                {
                    std::vector<uint16_t> big = live;
                    std::sort(big.begin(), big.end(), [&](uint16_t a, uint16_t b) { return graph.resourceInfo(a).bytes > graph.resourceInfo(b).bytes; });
                    std::string names;
                    for (size_t i = 0; i < big.size() && i < 3; ++i)
                        names += std::string(i ? ", " : "") + graph.resourceInfo(big[i]).name + " " + std::to_string(graph.resourceInfo(big[i]).bytes >> 20) + " MiB";
                    DE_LOG_WARN(LogCategory::Render, "TransientHeap: plan needs {:.1f} MiB, past the {:.0f} MiB cap; committed textures this plan (largest: {})", toMiB(needed), toMiB(kTransientCap),
                                names);
                }
            }
            else if (!m.memory.allocation() || m.memory.bytes() < capacity)
            {
                m.retire(std::move(m.memory));
                if (!m.allocator->allocatePlacementMemory(capacity, L"RG transient heap", m.memory))
                    return false;
                DE_LOG_INFO(LogCategory::Render, "TransientHeap: {:.1f} MiB placement memory for a {:.1f} MiB plan", toMiB(capacity), toMiB(needed));
            }

            for (uint32_t i = 0; i < live.size(); ++i)
            {
                const uint16_t            r    = live[i];
                const Rg::ResourceInfo    info = graph.resourceInfo(r);
                const Rg::TextureDesc     desc = graph.textureDesc(r);
                const D3D12_RESOURCE_DESC rd   = transientResourceDesc(info.width, info.height, desc, info.usage);
                if (rd.Width == 0 || (!m.fallback && info.heapOffset == Rg::kNoHeapOffset))
                {
                    if (m.logOnce(8))
                        DE_LOG_ERROR(LogCategory::Render, "TransientHeap: '{}' has no placeable desc", info.name);
                    m.retirePlaced();
                    return false;
                }
                D3D12_CLEAR_VALUE        clear{};
                const D3D12_CLEAR_VALUE* clearPtr = nullptr;
                if ((info.usage & Rg::kUsageDepthStencil) != 0)
                {
                    clear.Format             = transientDsvFormat(desc);
                    clear.DepthStencil.Depth = desc.clear[0];
                    clearPtr                 = &clear;
                }
                else if ((info.usage & Rg::kUsageRenderTarget) != 0)
                {
                    clear.Format = static_cast<DXGI_FORMAT>(desc.viewFormat != 0 ? desc.viewFormat : desc.format);
                    std::copy(desc.clear, desc.clear + 4, clear.Color);
                    clearPtr = &clear;
                }
                const std::string  narrow = std::string("RG transient ") + info.name;
                const std::wstring name(narrow.begin(), narrow.end());
                Impl::Placed       p;
                p.resource      = r;
                const bool made = m.fallback ? m.allocator->createTexture(rd, D3D12_RESOURCE_STATE_COMMON, clearPtr, name.c_str(), p.texture)
                                             : m.allocator->createPlacedTexture(m.memory, info.heapOffset, rd, clearPtr, name.c_str(), p.texture);
                if (!made)
                {
                    m.retirePlaced();
                    return false;
                }
                p.views = m.makeViews(i, p.texture.get(), desc, info.usage);
                m.placed.push_back(std::move(p));
            }
            m.planKey   = graph.planKey();
            m.heapBytes = graph.transientHeapBytes();
            m.havePlan  = true;
            ++m.rebuilds;
            const GpuMemoryStats s = m.allocator->stats();
            if (transientBudgetExceeded(s.localUsage, s.localBudget) && m.logOnce(16))
                DE_LOG_WARN(LogCategory::Render, "TransientHeap: local memory {:.1f} of {:.1f} MiB budget (above {}%)", toMiB(s.localUsage), toMiB(s.localBudget), kTransientBudgetWarnPercent);
        }
        for (Impl::Placed& p : m.placed)
        {
            if (!graph.placeTransient(p.resource, p.texture.get(), p.views))
                return false;
        }
        return true;
    }

    void TransientHeap::retireAfter(uint64_t fence)
    {
        m_impl->retireFence = fence;
    }

    void TransientHeap::collect(uint64_t completedFence)
    {
        std::erase_if(m_impl->retired, [&](const Impl::Retired& r) { return r.fence <= completedFence; });
    }

    uint64_t TransientHeap::capacity() const
    {
        return m_impl->memory.allocation() ? m_impl->memory.bytes() : 0;
    }
    uint32_t TransientHeap::placedCount() const
    {
        return static_cast<uint32_t>(m_impl->placed.size());
    }
    uint32_t TransientHeap::rebuilds() const
    {
        return m_impl->rebuilds;
    }
    bool TransientHeap::committedFallback() const
    {
        return m_impl->fallback && !m_impl->placed.empty();
    }
    uint32_t TransientHeap::retiredCount() const
    {
        return static_cast<uint32_t>(m_impl->retired.size());
    }

} // namespace Dark
