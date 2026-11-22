#pragma once

#include "Render/RenderGraph.h"
#include "Render/RenderTargetPool.h"

#include <cstdint>
#include <vector>

namespace Dark::Rg
{

    // This frame's pair of persistent targets behind one RenderGraph history (render-graph plan
    // Phase 5). prev is always a real, bindable target; its contents are meaningful only when
    // prevValid (false after a resize, a format change, invalidateHistory or a skipped write).
    struct HistoryTargets
    {
        TargetId    prev;
        TargetId    curr;
        bool        prevValid = false;
        HistoryPair pair; // the graph handles (FeatureFrame::setHistory)

        bool valid() const
        {
            return prev.valid() && curr.valid();
        }
    };

    // Binds RenderGraph::history slot ids to RenderTargetPool targets. The graph owns the ping-pong
    // and validity; this owns the memory: one pool target per live slot id, registered on first use
    // (named "DE.History.<name>#<slot>" for PIX) and unregistered by collect() once the graph drops
    // the id (history not requested for a frame, resized or re-described). CPU only.
    class HistoryTargetSet
    {
    public:
        // After g.beginFrame. desc may be output-relative: it is resolved against the pool's output
        // size and requested from the graph as an Absolute texture of that size.
        HistoryTargets request(RenderGraph& g, RenderTargetPool& pool, const char* name, const TargetDesc& desc);
        // After the frame's history() calls: frees the targets of slot ids the graph no longer owns.
        void collect(const RenderGraph& g, RenderTargetPool& pool);
        // Unregisters everything (owner teardown).
        void     clear(RenderTargetPool& pool);
        uint32_t count() const
        {
            return static_cast<uint32_t>(m_slots.size());
        }

    private:
        struct Slot
        {
            uint32_t id = kNoPass;
            TargetId target;
        };

        TargetId slotTarget(RenderTargetPool& pool, const char* name, uint32_t id, const TargetDesc& desc);

        std::vector<Slot>     m_slots;
        std::vector<uint32_t> m_live; // scratch for collect()
    };

} // namespace Dark::Rg
