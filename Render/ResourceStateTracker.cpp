#include "Render/ResourceStateTracker.h"

#include "Render/RenderGraphBarriers.h"

namespace Dark
{

    void ResourceStateTracker::track(const void* resource, Rg::ResourceKind kind, const Rg::UseState& initial)
    {
        if (!resource)
            return;
        Entry& e = m_entries[resource];
        e.kind   = kind;
        e.state  = initial;
        if (kind == Rg::ResourceKind::Buffer)
            e.state.layout = Rg::Layout::Undefined;
    }

    void ResourceStateTracker::forget(const void* resource)
    {
        m_entries.erase(resource);
    }

    const Rg::UseState* ResourceStateTracker::current(const void* resource) const
    {
        const auto it = m_entries.find(resource);
        return it == m_entries.end() ? nullptr : &it->second.state;
    }

    Rg::ResourceKind ResourceStateTracker::kind(const void* resource) const
    {
        const auto it = m_entries.find(resource);
        return it == m_entries.end() ? Rg::ResourceKind::Texture : it->second.kind;
    }

    bool ResourceStateTracker::needsBarrier(const Rg::UseState& before, const Rg::UseState& after, Rg::ResourceKind kind)
    {
        if (kind == Rg::ResourceKind::Texture && before.layout != after.layout)
            return true;
        if (before == after)
            return false;
        // NO_ACCESS deactivates a resource until a barrier with NO_ACCESS before re-activates it.
        if (before.access == Rg::Access::NoAccess || after.access == Rg::Access::NoAccess)
            return true;
        // Same layout (or a buffer): only a hazard needs a barrier.
        return !Rg::isReadOnlyState(before) || !Rg::isReadOnlyState(after);
    }

    bool ResourceStateTracker::transition(const void* resource, const Rg::UseState& after, Rg::UseState& before)
    {
        const auto it = m_entries.find(resource);
        if (it == m_entries.end())
            return false;
        Entry&       e    = it->second;
        Rg::UseState want = after;
        if (e.kind == Rg::ResourceKind::Buffer)
            want.layout = Rg::Layout::Undefined;
        if (!needsBarrier(e.state, want, e.kind))
        {
            if (e.state != want)
            {
                // Two reads in one layout: keep both so the next barrier waits for every reader.
                e.state.sync   = e.state.sync | want.sync;
                e.state.access = e.state.access | want.access;
            }
            return false;
        }
        before  = e.state;
        e.state = want;
        return true;
    }

    void ResourceStateTracker::set(const void* resource, const Rg::UseState& after)
    {
        const auto it = m_entries.find(resource);
        if (it == m_entries.end())
            return;
        it->second.state = after;
        if (it->second.kind == Rg::ResourceKind::Buffer)
            it->second.state.layout = Rg::Layout::Undefined;
    }

} // namespace Dark
