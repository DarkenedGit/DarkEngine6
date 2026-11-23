#pragma once

#include "Render/RenderGraphTypes.h"

#include <cstdint>
#include <unordered_map>

// One table of the current enhanced-barrier state of every persistent and imported resource
// (render-graph plan §3.7, Phase 6). Pure C++: resources are keyed by their native pointer as an
// opaque key, so the table is unit-tested without D3D12.
//
// It is the single source of truth while the frame records: owners that still record their own
// barriers (opaque passes) and the graph executor both ask it for the "before" side of a barrier
// and tell it the "after" side, so every barrier on a tracked resource is an enhanced barrier with
// a before-state that matches what the GPU timeline holds at that point of the command stream
// (D13: no legacy barriers, so no legacy/enhanced mixing on one subresource). Whole resources
// only: per-subresource owners (the shadow cascades, the local shadow array) keep their own
// state and are not tracked here. Not thread safe: the main recording thread only.

namespace Dark
{

    class ResourceStateTracker
    {
    public:
        // Starts tracking `resource` in `initial` (its creation layout). Re-tracking replaces the entry.
        void track(const void* resource, Rg::ResourceKind kind, const Rg::UseState& initial);
        // Stops tracking (call before the resource is released, so a new resource at the same address
        // never inherits the old state).
        void forget(const void* resource);
        void clear()
        {
            m_entries.clear();
        }

        bool tracked(const void* resource) const
        {
            return m_entries.find(resource) != m_entries.end();
        }
        // Null when untracked.
        const Rg::UseState* current(const void* resource) const;
        Rg::ResourceKind    kind(const void* resource) const;
        uint32_t            count() const
        {
            return static_cast<uint32_t>(m_entries.size());
        }

        // Moves a tracked resource to `after`. Returns true and fills `before` when a barrier is
        // needed: a layout change, or a write on either side. A read after a read in the same layout
        // needs none; the tracked state then becomes the union of both (so the next write waits for
        // every reader). The same write state twice (render target to render target, UAV to UAV)
        // needs no transition either; UAV ordering is the owner's explicit UAV barrier, as before.
        // False when untracked (before untouched).
        bool transition(const void* resource, const Rg::UseState& after, Rg::UseState& before);

        // Records that a barrier the caller already emitted left the resource in `after`.
        void set(const void* resource, const Rg::UseState& after);

        // The rule transition() uses, exposed for tests and the executor.
        static bool needsBarrier(const Rg::UseState& before, const Rg::UseState& after, Rg::ResourceKind kind);

        // The before-state of a planned graph barrier on a tracked resource. Declared accesses that
        // need no barrier never reach the table (a frame-start write, a read after a read), so when
        // the plan's before-state has an access in the layout the table holds, the barrier also waits
        // for it: the plan's state replaces a no-access entry and is merged into any other. A plan
        // state in another layout or without access (an import's placeholder) leaves the table's.
        static Rg::UseState planBefore(const Rg::UseState& tracked, const Rg::UseState& plan)
        {
            if (plan.access == Rg::Access::NoAccess || plan.layout != tracked.layout)
                return tracked;
            if (tracked.access == Rg::Access::NoAccess)
                return plan;
            Rg::UseState merged = tracked;
            merged.sync         = (tracked.sync == Rg::Sync::All || plan.sync == Rg::Sync::All) ? Rg::Sync::All : tracked.sync | plan.sync; // ALL alone
            merged.access       = merged.access | plan.access;
            return merged;
        }

    private:
        struct Entry
        {
            Rg::ResourceKind kind = Rg::ResourceKind::Texture;
            Rg::UseState     state;
        };
        std::unordered_map<const void*, Entry> m_entries;
    };

} // namespace Dark
