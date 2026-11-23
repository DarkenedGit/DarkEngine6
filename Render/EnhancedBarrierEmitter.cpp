#include "Render/EnhancedBarrierEmitter.h"

#include "Core/Log.h"
#include "Render/RenderGraphBarriers.h"
#include "Render/ResourceStateTracker.h"

#include <thread>
#include <wrl/client.h>

namespace Dark
{

    namespace
    {
        ResourceStateTracker* g_tracker = nullptr;
        std::thread::id       g_trackerThread;

        void logOnce(uint32_t bit, const char* message)
        {
            static uint32_t logged = 0;
            if (logged & (1u << bit))
                return;
            logged |= 1u << bit;
            DE_LOG_ERROR(LogCategory::Render, "Enhanced barriers: {} (logs once)", message);
        }

        // The tracker for a whole-resource barrier recorded on this thread, or null.
        ResourceStateTracker* trackerFor(ID3D12Resource* resource, uint32_t subresource)
        {
            ResourceStateTracker* t = frameStateTracker();
            if (!t || !resource || !t->tracked(resource))
                return nullptr;
            if (subresource != kAllSubresources)
            {
                logOnce(0, "a per-subresource barrier on a tracked resource; the tracker keeps whole resources only, so it was recorded untracked");
                return nullptr;
            }
            return t;
        }

        bool toUseState(D3D12_RESOURCE_STATES state, Rg::ResourceKind kind, Rg::UseState& out)
        {
            if (Rg::legacyStateEquivalent(static_cast<uint32_t>(state), kind, out))
                return true;
            logOnce(1, "a D3D12_RESOURCE_STATES value with no enhanced equivalent (nothing recorded)");
            return false;
        }
    } // namespace

    void setFrameStateTracker(ResourceStateTracker* tracker)
    {
        g_tracker       = tracker;
        g_trackerThread = std::this_thread::get_id();
    }

    ResourceStateTracker* frameStateTracker()
    {
        return g_tracker && std::this_thread::get_id() == g_trackerThread ? g_tracker : nullptr;
    }

    D3D12_BARRIER_SUBRESOURCE_RANGE subresourceRange(uint32_t subresource)
    {
        // NumMipLevels == 0: IndexOrFirstMipLevel is a subresource index, and 0xFFFFFFFF means all.
        D3D12_BARRIER_SUBRESOURCE_RANGE r{};
        r.IndexOrFirstMipLevel = subresource;
        r.NumMipLevels         = 0;
        return r;
    }

    D3D12_TEXTURE_BARRIER makeTextureBarrier(ID3D12Resource* resource, const Rg::UseState& before, const Rg::UseState& after, bool discard, uint32_t subresource)
    {
        D3D12_TEXTURE_BARRIER b{};
        b.SyncBefore   = static_cast<D3D12_BARRIER_SYNC>(before.sync);
        b.SyncAfter    = static_cast<D3D12_BARRIER_SYNC>(after.sync);
        b.AccessBefore = static_cast<D3D12_BARRIER_ACCESS>(before.access);
        b.AccessAfter  = static_cast<D3D12_BARRIER_ACCESS>(after.access);
        b.LayoutBefore = static_cast<D3D12_BARRIER_LAYOUT>(before.layout);
        b.LayoutAfter  = static_cast<D3D12_BARRIER_LAYOUT>(after.layout);
        b.pResource    = resource;
        b.Subresources = subresourceRange(subresource);
        b.Flags        = discard ? D3D12_TEXTURE_BARRIER_FLAG_DISCARD : D3D12_TEXTURE_BARRIER_FLAG_NONE;
        return b;
    }

    D3D12_BUFFER_BARRIER makeBufferBarrier(ID3D12Resource* resource, const Rg::UseState& before, const Rg::UseState& after)
    {
        D3D12_BUFFER_BARRIER b{};
        b.SyncBefore   = static_cast<D3D12_BARRIER_SYNC>(before.sync);
        b.SyncAfter    = static_cast<D3D12_BARRIER_SYNC>(after.sync);
        b.AccessBefore = static_cast<D3D12_BARRIER_ACCESS>(before.access);
        b.AccessAfter  = static_cast<D3D12_BARRIER_ACCESS>(after.access);
        b.pResource    = resource;
        b.Offset       = 0;
        b.Size         = UINT64_MAX; // the whole buffer (the only legal range)
        return b;
    }

    bool legacyEquivalent(D3D12_RESOURCE_STATES state, Rg::ResourceKind kind, Rg::UseState& out)
    {
        return toUseState(state, kind, out);
    }

    Rg::ResourceKind resourceKind(ID3D12Resource* resource)
    {
        if (!resource)
            return Rg::ResourceKind::Texture;
        const D3D12_RESOURCE_DESC desc = resource->GetDesc();
        return desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? Rg::ResourceKind::Buffer : Rg::ResourceKind::Texture;
    }

    // ------------------------------------------------------------------ EnhancedBarrierList

    void EnhancedBarrierList::texture(const D3D12_TEXTURE_BARRIER& barrier)
    {
        if (m_textureCount == kCapacity)
            record();
        m_textures[m_textureCount++] = barrier;
    }

    void EnhancedBarrierList::buffer(const D3D12_BUFFER_BARRIER& barrier)
    {
        if (m_bufferCount == kCapacity)
            record();
        m_buffers[m_bufferCount++] = barrier;
    }

    void trackResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES initial)
    {
        ResourceStateTracker* tracker = frameStateTracker();
        if (!tracker || !resource)
            return;
        const Rg::ResourceKind kind = resourceKind(resource);
        Rg::UseState           state;
        if (!legacyEquivalent(initial, kind, state))
            return;
        // A creation state is not a barrier: nothing is pending on the resource yet.
        tracker->track(resource, kind, state);
    }

    void forgetResource(ID3D12Resource* resource)
    {
        if (ResourceStateTracker* tracker = frameStateTracker())
            tracker->forget(resource);
    }

    bool trackedInState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state)
    {
        if (!resource)
            return true;
        const ResourceStateTracker* tracker = frameStateTracker();
        const Rg::UseState*         now     = tracker ? tracker->current(resource) : nullptr;
        Rg::UseState                want;
        if (!now || !Rg::legacyStateEquivalent(static_cast<uint32_t>(state), tracker->kind(resource), want))
            return false;
        return tracker->kind(resource) == Rg::ResourceKind::Buffer ? now->access == want.access : now->layout == want.layout;
    }

    bool createNeedsLayout(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initial)
    {
        if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER || desc.Dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN)
            return false;
        constexpr D3D12_RESOURCE_STATES kLegacyLayoutBits = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                                            D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE;
        return (initial & kLegacyLayoutBits) != 0;
    }

    D3D12_RESOURCE_DESC1 resourceDesc1(const D3D12_RESOURCE_DESC& desc)
    {
        D3D12_RESOURCE_DESC1 d{};
        d.Dimension                = desc.Dimension;
        d.Alignment                = desc.Alignment;
        d.Width                    = desc.Width;
        d.Height                   = desc.Height;
        d.DepthOrArraySize         = desc.DepthOrArraySize;
        d.MipLevels                = desc.MipLevels;
        d.Format                   = desc.Format;
        d.SampleDesc               = desc.SampleDesc;
        d.Layout                   = desc.Layout;
        d.Flags                    = desc.Flags;
        d.SamplerFeedbackMipRegion = {};
        return d;
    }

    D3D12_BARRIER_LAYOUT initialLayout(D3D12_RESOURCE_STATES initial)
    {
        Rg::UseState state;
        if (!legacyEquivalent(initial, Rg::ResourceKind::Texture, state))
            return D3D12_BARRIER_LAYOUT_COMMON;
        return static_cast<D3D12_BARRIER_LAYOUT>(state.layout);
    }

    HRESULT createCommittedResource(ID3D12Device* device, const D3D12_HEAP_PROPERTIES& heap, D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC& desc,
                                    D3D12_RESOURCE_STATES initial, const D3D12_CLEAR_VALUE* clear, REFIID riid, void** out)
    {
        if (!device)
            return E_INVALIDARG;
        if (!createNeedsLayout(desc, initial))
            return device->CreateCommittedResource(&heap, heapFlags, &desc, initial, clear, riid, out);
        Microsoft::WRL::ComPtr<ID3D12Device10> device10;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device10))) || !device10)
        {
            logOnce(7, "ID3D12Device10 unavailable: a texture cannot be created with an enhanced initial layout (the startup gate requires it)");
            return E_NOINTERFACE;
        }
        const D3D12_RESOURCE_DESC1 desc1 = resourceDesc1(desc);
        return device10->CreateCommittedResource3(&heap, heapFlags, &desc1, initialLayout(initial), clear, nullptr, 0, nullptr, riid, out);
    }

    bool EnhancedBarrierList::transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, uint32_t subresource)
    {
        if (!resource)
            return false;
        return transition(resource, resourceKind(resource), before, after, subresource);
    }

    bool EnhancedBarrierList::transition(ID3D12Resource* resource, Rg::ResourceKind kind, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, uint32_t subresource)
    {
        if (!resource)
            return false;
        Rg::UseState a;
        if (!toUseState(after, kind, a))
            return false;
        if (ResourceStateTracker* t = trackerFor(resource, subresource))
        {
            Rg::UseState b;
            if (t->transition(resource, a, b))
            {
                if (kind == Rg::ResourceKind::Buffer)
                    buffer(makeBufferBarrier(resource, b, a));
                else
                    texture(makeTextureBarrier(resource, b, a));
            }
            return true;
        }
        if (before == after)
            return true; // the legacy helpers never recorded a same-state transition either
        Rg::UseState b;
        if (!toUseState(before, kind, b))
            return false;
        if (kind == Rg::ResourceKind::Buffer)
            buffer(makeBufferBarrier(resource, b, a));
        else
            texture(makeTextureBarrier(resource, b, a, false, subresource));
        return true;
    }

    bool EnhancedBarrierList::transitionTo(ID3D12Resource* resource, D3D12_RESOURCE_STATES after)
    {
        if (!resource)
            return false;
        ResourceStateTracker* t = trackerFor(resource, kAllSubresources);
        Rg::UseState          a;
        if (!toUseState(after, t ? t->kind(resource) : resourceKind(resource), a))
            return false;
        return transitionTo(resource, a);
    }

    bool EnhancedBarrierList::transitionTo(ID3D12Resource* resource, const Rg::UseState& after)
    {
        ResourceStateTracker* t = trackerFor(resource, kAllSubresources);
        if (!t)
        {
            if (resource)
                logOnce(2, "transitionTo on a resource the frame tracker does not know (nothing recorded)");
            return false;
        }
        Rg::UseState b;
        if (t->transition(resource, after, b))
        {
            if (t->kind(resource) == Rg::ResourceKind::Buffer)
                buffer(makeBufferBarrier(resource, b, after));
            else
                texture(makeTextureBarrier(resource, b, after));
        }
        return true;
    }

    void EnhancedBarrierList::uav(ID3D12Resource* resource)
    {
        if (resource)
            uav(resource, resourceKind(resource));
    }

    void EnhancedBarrierList::uav(ID3D12Resource* resource, Rg::ResourceKind kind)
    {
        if (!resource)
            return;
        const Rg::UseState s{ Rg::Sync::AllShading, Rg::Access::UnorderedAccess, Rg::Layout::UnorderedAccess };
        if (kind == Rg::ResourceKind::Buffer)
            buffer(makeBufferBarrier(resource, s, s));
        else
            texture(makeTextureBarrier(resource, s, s));
    }

    void EnhancedBarrierList::record()
    {
        if (m_textureCount == 0 && m_bufferCount == 0)
            return;
        if (!m_cmd)
        {
            logOnce(3, "barriers collected without a command list (dropped)");
            m_textureCount = m_bufferCount = 0;
            return;
        }
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> list7;
        if (FAILED(m_cmd->QueryInterface(IID_PPV_ARGS(&list7))))
        {
            logOnce(4, "the command list has no ID3D12GraphicsCommandList7 (the startup gate requires enhanced barriers); barriers dropped");
            m_textureCount = m_bufferCount = 0;
            return;
        }
        D3D12_BARRIER_GROUP groups[2]{};
        UINT32              n = 0;
        if (m_bufferCount)
        {
            groups[n].Type            = D3D12_BARRIER_TYPE_BUFFER;
            groups[n].NumBarriers     = m_bufferCount;
            groups[n].pBufferBarriers = m_buffers;
            ++n;
        }
        if (m_textureCount)
        {
            groups[n].Type             = D3D12_BARRIER_TYPE_TEXTURE;
            groups[n].NumBarriers      = m_textureCount;
            groups[n].pTextureBarriers = m_textures;
            ++n;
        }
        list7->Barrier(n, groups);
        m_textureCount = m_bufferCount = 0;
    }

    bool recordTransition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, uint32_t subresource)
    {
        EnhancedBarrierList list(cmd);
        return list.transition(resource, before, after, subresource);
    }

    bool recordTransitionTo(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES after)
    {
        EnhancedBarrierList list(cmd);
        return list.transitionTo(resource, after);
    }

    void recordUavBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource)
    {
        EnhancedBarrierList list(cmd);
        list.uav(resource);
    }

    // ------------------------------------------------------------------ EnhancedBarrierEmitter

    void EnhancedBarrierEmitter::record(ID3D12GraphicsCommandList* cmd, const Rg::BarrierRec* records, uint32_t count, ResolveFn resolve, const void* context, bool validate)
    {
        EnhancedBarrierList list(cmd);
        collect(list, records, count, resolve, context, validate);
        list.record();
    }

    void EnhancedBarrierEmitter::collect(EnhancedBarrierList& list, const Rg::BarrierRec* records, uint32_t count, ResolveFn resolve, const void* context, bool validate)
    {
        ResourceStateTracker* tracker = frameStateTracker();
        for (uint32_t i = 0; i < count; ++i)
        {
            const Rg::BarrierRec& rec = records[i];
            ID3D12Resource*       res = resolve ? resolve(context, rec.resource) : nullptr;
            if (!res)
            {
                logOnce(5, "a graph barrier on a resource with no native resource (transients get memory in Phase 9); skipped");
                continue;
            }
            const bool tracked = tracker && tracker->tracked(res);
            if (tracked)
            {
                const Rg::UseState* now = tracker->current(res);
                // Layouts only: the sync/access scopes legitimately differ (the tracker keeps the union of
                // the reads an opaque owner made), and an Undefined plan layout is an import whose
                // contents the plan did not need to know.
                if (validate && !rec.tracked && now && rec.kind == Rg::ResourceKind::Texture && rec.before.layout != Rg::Layout::Undefined && now->layout != rec.before.layout)
                {
                    ++m_mismatches;
                    static uint32_t loggedMismatch = 0;
                    if (loggedMismatch < 8)
                    {
                        ++loggedMismatch;
                        DE_LOG_WARN(LogCategory::Render, "rg.validate: graph resource {} is in layout {} (sync 0x{:X} access 0x{:X}); the plan expected {} (sync 0x{:X} access 0x{:X})",
                                    rec.resource, Rg::layoutName(now->layout), static_cast<uint32_t>(now->sync), static_cast<uint32_t>(now->access), Rg::layoutName(rec.before.layout),
                                    static_cast<uint32_t>(rec.before.sync), static_cast<uint32_t>(rec.before.access));
                    }
                }
                // A planned record also waits for the declared accesses the table never saw.
                Rg::UseState       before;
                const Rg::UseState planned = (!rec.tracked && now) ? ResourceStateTracker::planBefore(*now, rec.before) : Rg::UseState{};
                if (!rec.tracked && now && planned != *now && ResourceStateTracker::needsBarrier(planned, rec.after, rec.kind))
                {
                    before = planned;
                    tracker->set(res, rec.after);
                }
                else if (!tracker->transition(res, rec.after, before))
                {
                    continue;
                }
                if (rec.kind == Rg::ResourceKind::Buffer)
                    list.buffer(makeBufferBarrier(res, before, rec.after));
                else
                    list.texture(makeTextureBarrier(res, before, rec.after));
                ++m_emitted;
                continue;
            }
            if (rec.tracked)
            {
                logOnce(6, "a graph barrier after an opaque pass on a resource the frame tracker does not know; skipped (the owner keeps its own state)");
                continue;
            }
            if (rec.kind == Rg::ResourceKind::Buffer)
                list.buffer(makeBufferBarrier(res, rec.before, rec.after));
            else
                list.texture(makeTextureBarrier(res, rec.before, rec.after, rec.discard));
            ++m_emitted;
        }
    }

} // namespace Dark
