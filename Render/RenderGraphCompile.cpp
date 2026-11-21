#include "Render/RenderGraph.h"

#include "Core/Log.h"
#include "Render/RenderGraphBarriers.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

// RenderGraph::compile and the schedule queries (plan §3.5, §3.7, §3.8, §3.11).

namespace Dark::Rg
{

    namespace
    {
        constexpr uint32_t kQ(Queue q)
        {
            return static_cast<uint32_t>(q);
        }
        constexpr Queue otherQueue(Queue q)
        {
            return q == Queue::Graphics ? Queue::AsyncCompute : Queue::Graphics;
        }

        struct Fnv
        {
            uint64_t h = 1469598103934665603ull;
            void     bytes(const void* p, size_t n)
            {
                const auto* b = static_cast<const unsigned char*>(p);
                for (size_t i = 0; i < n; ++i)
                {
                    h ^= b[i];
                    h *= 1099511628211ull;
                }
            }
            template <class T> void value(const T& v)
            {
                bytes(&v, sizeof(v));
            }
            void text(const char* s)
            {
                for (; s && *s; ++s)
                    value(*s);
                value('\0');
            }
        };

        UseState unionState(const UseState& a, const UseState& b)
        {
            return { a.sync | b.sync, a.access | b.access, a.layout };
        }

        UseState noAccess(Layout layout)
        {
            return { Sync::None, Access::NoAccess, layout };
        }

        // One access of a resource by a live pass (uses merged per pass).
        struct ResAccess
        {
            uint32_t pass  = kNoPass;
            Queue    queue = Queue::Graphics;
            UseState state;
            bool     write = false;
        };

        // A run of accesses served by one layout: a single write, or consecutive reads.
        struct Segment
        {
            std::vector<uint32_t> accesses; // indices into the resource's access list
            UseState              perQueue[kQueueCount];
            bool                  hasQueue[kQueueCount] = { false, false };
            uint32_t              first[kQueueCount]    = { kNoPass, kNoPass }; // first pass per queue
            uint32_t              last[kQueueCount]     = { kNoPass, kNoPass };
            Layout                layout                = Layout::Undefined;
            UseState              combined; // union over queues, for layout merging
            bool                  readOnly = true;
        };
    } // namespace

    struct RenderGraph::Impl
    {
        RenderGraph&                               g;
        std::vector<std::vector<uint32_t>>         dataPreds;
        std::vector<std::vector<ResAccess>>        resAccess;
        std::vector<std::pair<uint32_t, uint32_t>> edges;        // live dependency edges incl. barrier edges
        std::vector<uint16_t>                      aliasedAfter; // previous occupant of the alias slot
        std::vector<bool>                          opaqueRes;    // touched by an OwnBarriers pass: no barrier records

        explicit Impl(RenderGraph& graph) :
            g(graph)
        {
        }

        Queue itemQueue(uint32_t item) const
        {
            return item == kPrologue || item == kEpilogue ? Queue::Graphics : g.m_cPass[item].queue;
        }

        uint32_t itemPos(uint32_t item) const
        {
            if (item == kPrologue)
                return 0;
            if (item == kEpilogue)
                return static_cast<uint32_t>(g.m_queueOrder[kQ(Queue::Graphics)].size()) + 1;
            return g.m_cPass[item].queuePos;
        }

        std::string passText(uint32_t p) const
        {
            return std::string("'") + g.passName(p) + "'";
        }

        // ---- 1. queue checks -------------------------------------------------
        bool checkQueues()
        {
            bool ok = true;
            for (uint32_t p = 0; p < g.m_passes.size(); ++p)
            {
                const PassNode& node = g.m_passes[p];
                if (hasFlag(node.flags, PassFlags::OwnBarriers) && node.declaredQueue == Queue::AsyncCompute)
                {
                    g.addError(ErrorCode::OwnBarriersOnCompute, p, kInvalidIndex, "pass " + passText(p) + " is OwnBarriers on AsyncCompute; opaque passes stay on Graphics");
                    ok = false;
                }
                // Checked against the declared queue so a compute-illegal use shows up while
                // asyncCompute is still off (§3.5: the model is tested before it is used).
                for (const AccessRec& a : node.accesses)
                {
                    if (!useLegalOnQueue(a.use, node.declaredQueue))
                    {
                        g.addError(ErrorCode::IllegalOnQueue, p, a.resource,
                                   "pass " + passText(p) + " on " + queueName(node.declaredQueue) + " uses '" + g.m_resources[a.resource].name + "' as " + useName(a.use) +
                                       ", which that queue can't do");
                        ok = false;
                    }
                }
            }
            return ok;
        }

        // ---- 2. cull -------------------------------------------------------
        void cull()
        {
            const uint32_t n = static_cast<uint32_t>(g.m_passes.size());
            dataPreds.assign(n, {});
            std::vector<uint8_t> root(n, 0);
            for (uint32_t p = 0; p < n; ++p)
            {
                const PassNode& node = g.m_passes[p];
                if (hasFlag(node.flags, PassFlags::SideEffect) || hasFlag(node.flags, PassFlags::NeverCull))
                    root[p] = 1;
                for (const AccessRec& a : node.accesses)
                {
                    const ResourceNode& r = g.m_resources[a.resource];
                    if (a.write && r.lifetime != Lifetime::Transient)
                        root[p] = 1; // back buffer, imported targets and history writes are sinks
                    const uint32_t w = r.versions[a.version].writer;
                    if (w < n && w != p)
                        dataPreds[p].push_back(w);
                }
            }
            std::vector<uint32_t> stack;
            for (uint32_t p = 0; p < n; ++p)
            {
                g.m_cPass[p].live = g.m_options.noCull || root[p];
                if (g.m_cPass[p].live)
                    stack.push_back(p);
            }
            while (!stack.empty())
            {
                const uint32_t p = stack.back();
                stack.pop_back();
                for (const uint32_t w : dataPreds[p])
                {
                    if (!g.m_cPass[w].live)
                    {
                        g.m_cPass[w].live = true;
                        stack.push_back(w);
                    }
                }
            }
        }

        // ---- 3. order --------------------------------------------------------
        bool order()
        {
            const uint32_t n = static_cast<uint32_t>(g.m_passes.size());
            for (uint32_t p = 0; p < n; ++p)
            {
                if (!g.m_cPass[p].live)
                    continue;
                for (const uint32_t w : dataPreds[p])
                    edges.push_back({ w, p });
                for (const AccessRec& a : g.m_passes[p].accesses)
                {
                    if (!a.write)
                        continue;
                    // Write-after-read: every reader of the version this write replaces runs first.
                    for (const uint32_t r : g.m_resources[a.resource].versions[a.version].readers)
                        if (r != p && g.m_cPass[r].live)
                            edges.push_back({ r, p });
                }
            }
            std::sort(edges.begin(), edges.end());
            edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

            std::vector<std::vector<uint32_t>> succ(n);
            std::vector<uint32_t>              indegree(n, 0);
            for (const auto& [from, to] : edges)
            {
                succ[from].push_back(to);
                ++indegree[to];
            }
            std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> ready;
            uint32_t                                                                     liveCount = 0;
            for (uint32_t p = 0; p < n; ++p)
            {
                if (!g.m_cPass[p].live)
                    continue;
                ++liveCount;
                if (indegree[p] == 0)
                    ready.push(p);
            }
            g.m_order.clear();
            while (!ready.empty())
            {
                const uint32_t p = ready.top();
                ready.pop();
                g.m_order.push_back(p);
                for (const uint32_t s : succ[p])
                    if (--indegree[s] == 0)
                        ready.push(s);
            }
            if (g.m_order.size() != liveCount)
            {
                reportCycle(indegree);
                return false;
            }
            for (Queue q : { Queue::Graphics, Queue::AsyncCompute })
                g.m_queueOrder[kQ(q)].clear();
            for (uint32_t i = 0; i < g.m_order.size(); ++i)
            {
                const uint32_t p   = g.m_order[i];
                CompiledPass&  c   = g.m_cPass[p];
                auto&          seq = g.m_queueOrder[kQ(c.queue)];
                c.order            = i;
                c.queuePos         = static_cast<uint32_t>(seq.size()) + (c.queue == Queue::Graphics ? 1u : 0u);
                seq.push_back(p);
            }
            return true;
        }

        void reportCycle(const std::vector<uint32_t>& indegree)
        {
            // Kahn left only passes with an unsorted predecessor, so walking predecessors among them
            // must revisit one: that loop is the cycle.
            const uint32_t                     n = static_cast<uint32_t>(g.m_passes.size());
            std::vector<std::vector<uint32_t>> pred(n);
            for (const auto& [from, to] : edges)
                pred[to].push_back(from);
            const auto remaining = [&](uint32_t p) { return g.m_cPass[p].live && indegree[p] > 0; };
            uint32_t   start     = kNoPass;
            for (uint32_t p = 0; p < n && start == kNoPass; ++p)
                if (remaining(p))
                    start = p;
            std::vector<uint32_t> seenAt(n, kNoPass);
            std::vector<uint32_t> path;
            uint32_t              cur = start;
            while (cur != kNoPass && seenAt[cur] == kNoPass)
            {
                seenAt[cur] = static_cast<uint32_t>(path.size());
                path.push_back(cur);
                uint32_t next = kNoPass;
                for (const uint32_t q : pred[cur])
                {
                    if (remaining(q))
                    {
                        next = q;
                        break;
                    }
                }
                cur = next;
            }
            std::string text = "passes form a cycle: ";
            if (cur != kNoPass)
            {
                // path runs backwards along the edges; print it forwards.
                for (size_t i = path.size(); i-- > seenAt[cur];)
                    text += passText(path[i]) + " -> ";
                text += passText(path.back());
            }
            else
            {
                text += "(unresolved)";
            }
            text += " (a read of an older version must run before the pass that overwrote it)";
            g.addError(ErrorCode::Cycle, cur == kNoPass ? start : cur, kInvalidIndex, text);
        }

        // ---- 4. per-pass merged uses + lifetimes ---------------------------
        bool mergeUses()
        {
            bool ok = true;
            resAccess.assign(g.m_resources.size(), {});
            for (const uint32_t p : g.m_order)
            {
                const PassNode& node  = g.m_passes[p];
                const Queue     queue = g.m_cPass[p].queue;
                struct Merged
                {
                    uint16_t resource;
                    UseState state;
                    bool     write;
                    Use      use;
                };
                std::vector<Merged> merged;
                for (const AccessRec& a : node.accesses)
                {
                    const ResourceNode& r  = g.m_resources[a.resource];
                    const UseState      st = useState(a.use, r.kind, queue);
                    auto                it = std::find_if(merged.begin(), merged.end(), [&](const Merged& m) { return m.resource == a.resource; });
                    if (it == merged.end())
                    {
                        merged.push_back({ a.resource, st, a.write, a.use });
                        continue;
                    }
                    UseState combined;
                    if (it->state == st)
                        it->write = it->write || a.write;
                    else if (!it->write && !a.write && combineReadStates(it->state, st, r.kind, combined))
                        it->state = combined;
                    else
                    {
                        g.addError(ErrorCode::IncompatibleUses, p, a.resource,
                                   "pass " + passText(p) + " uses '" + r.name + "' as " + useName(it->use) + " and " + useName(a.use) +
                                       " in one pass (no single layout serves both; split the pass or use readWrite)");
                        ok = false;
                    }
                }
                for (const Merged& m : merged)
                    resAccess[m.resource].push_back({ p, queue, m.state, m.write });
            }
            // OwnBarriers passes keep their state transitions in their own code (Phases 4-6), so the graph
            // plans no barriers for a resource they touch. A resource shared with a declared pass would
            // need a barrier at the boundary, which arrives with the Phase 6 emitter.
            opaqueRes.assign(g.m_resources.size(), false);
            for (size_t r = 0; r < g.m_resources.size(); ++r)
            {
                uint32_t opaquePass = kNoPass, declaredPass = kNoPass;
                for (const ResAccess& a : resAccess[r])
                {
                    if (hasFlag(g.m_passes[a.pass].flags, PassFlags::OwnBarriers))
                        opaquePass = a.pass;
                    else
                        declaredPass = a.pass;
                }
                opaqueRes[r] = opaquePass != kNoPass;
                if (opaquePass != kNoPass && declaredPass != kNoPass)
                {
                    g.addError(ErrorCode::MixedOpaqueUse, declaredPass, static_cast<uint16_t>(r),
                               "'" + std::string(g.m_resources[r].name) + "' is used by OwnBarriers pass " + passText(opaquePass) + " and declared pass " + passText(declaredPass) +
                                   "; mixing needs boundary barriers (Phase 6)");
                    ok = false;
                }
            }
            g.m_cRes.assign(g.m_resources.size(), {});
            for (size_t r = 0; r < g.m_resources.size(); ++r)
            {
                CompiledResource&   c    = g.m_cRes[r];
                const ResourceNode& node = g.m_resources[r];
                c.bytes                  = node.kind == ResourceKind::Buffer ? node.buf.size : estimateTextureBytes(node.width, node.height, node.tex);
                const auto& acc          = resAccess[r];
                if (acc.empty())
                    continue;
                c.used      = true;
                c.firstPass = acc.front().pass;
                c.lastPass  = acc.back().pass;
                for (const ResAccess& a : acc)
                {
                    const uint32_t q = kQ(a.queue);
                    c.queueMask      = static_cast<uint8_t>(c.queueMask | (1u << q));
                    if (c.firstOn[q] == kNoPass)
                        c.firstOn[q] = a.pass;
                    c.lastOn[q] = a.pass;
                }
            }
            return ok;
        }

        // ---- 5. barriers ---------------------------------------------------
        void emit(const BarrierRec& rec)
        {
            BarrierRec r = rec;
            if (g.m_options.fullBarriers)
            {
                if (r.before.sync != Sync::None)
                    r.before.sync = Sync::All;
                if (r.after.sync != Sync::None)
                    r.after.sync = Sync::All;
            }
            g.m_barriers.push_back(r);
        }

        BarrierRec makeRec(uint16_t res, Queue q, BarrierPlacement placement, uint32_t pass, const UseState& before, const UseState& after) const
        {
            BarrierRec rec;
            rec.resource  = res;
            rec.kind      = g.m_resources[res].kind;
            rec.queue     = q;
            rec.placement = placement;
            rec.pass      = pass;
            rec.before    = before;
            rec.after     = after;
            return rec;
        }

        std::vector<Segment> buildSegments(uint16_t res) const
        {
            const ResourceNode&  node = g.m_resources[res];
            const auto&          acc  = resAccess[res];
            std::vector<Segment> segs;
            for (uint32_t i = 0; i < acc.size(); ++i)
            {
                const ResAccess& a        = acc[i];
                const bool       readOnly = !a.write;
                UseState         combined;
                bool             merge = false;
                if (!segs.empty() && !g.m_options.fullBarriers && segs.back().readOnly && readOnly)
                {
                    merge = combineReadStates(segs.back().combined, a.state, node.kind, combined);
                    // The shared layout must be legal on every queue in the group (no DEPTH_STENCIL_READ
                    // for a compute reader).
                    const Segment& s = segs.back();
                    for (uint32_t q = 0; q < kQueueCount && merge; ++q)
                        if ((s.hasQueue[q] || kQ(a.queue) == q) && node.kind == ResourceKind::Texture)
                            merge = layoutLegalOnQueue(combined.layout, static_cast<Queue>(q));
                }
                if (!merge)
                {
                    segs.emplace_back();
                    segs.back().combined = a.state;
                    segs.back().readOnly = readOnly;
                }
                else
                {
                    segs.back().combined = combined;
                }
                Segment&       s = segs.back();
                const uint32_t q = kQ(a.queue);
                s.accesses.push_back(i);
                s.perQueue[q] = s.hasQueue[q] ? unionState(s.perQueue[q], a.state) : a.state;
                if (!s.hasQueue[q])
                    s.first[q] = a.pass;
                s.hasQueue[q] = true;
                s.last[q]     = a.pass;
            }
            for (Segment& s : segs)
            {
                s.layout = node.kind == ResourceKind::Texture ? s.combined.layout : Layout::Undefined;
                for (uint32_t q = 0; q < kQueueCount; ++q)
                    s.perQueue[q].layout = s.layout;
            }
            return segs;
        }

        // Accesses on one queue since that queue's last barrier on the resource: what the next barrier
        // there must wait for. Work on the other queue is ordered by a sync point instead (a fence
        // signal completes every earlier command on its queue).
        struct Pending
        {
            bool     has      = false;
            bool     wrote    = false;
            uint32_t lastPass = kNoPass;
            UseState state;
        };

        UseState pendingBefore(const Pending& p, Layout layout) const
        {
            if (!p.has)
                return noAccess(layout);
            UseState s = p.state;
            s.layout   = layout;
            return s;
        }

        void planBarriers()
        {
            g.m_barriers.clear();
            for (uint16_t res = 0; res < g.m_resources.size(); ++res)
            {
                if (opaqueRes[res])
                    continue;
                const ResourceNode&  node = g.m_resources[res];
                std::vector<Segment> segs = buildSegments(res);

                Segment start; // what the resource holds before this frame's first access
                start.layout   = node.kind == ResourceKind::Texture ? node.initial : Layout::Undefined;
                start.readOnly = node.lifetime != Lifetime::Transient;

                Pending pend[kQueueCount];
                for (size_t i = 0; i < segs.size(); ++i)
                {
                    const Segment& prev = i == 0 ? start : segs[i - 1];
                    const Segment& seg  = segs[i];
                    if (node.kind == ResourceKind::Buffer)
                        syncOnlyTransition(res, Layout::Undefined, seg, pend);
                    else
                        textureTransition(res, prev, seg, pend);
                    for (uint32_t q = 0; q < kQueueCount; ++q)
                    {
                        if (!seg.hasQueue[q])
                            continue;
                        Pending& p = pend[q];
                        p.state    = p.has ? unionState(p.state, seg.perQueue[q]) : seg.perQueue[q];
                        p.wrote    = p.wrote || !isReadOnlyState(seg.perQueue[q]);
                        p.lastPass = seg.last[q];
                        p.has      = true;
                    }
                }
                // Whatever reads a persistent resource after this frame (present, readback, next frame)
                // is ordered after the graphics epilogue, so the epilogue waits for its last compute use.
                const Pending& pc = pend[kQ(Queue::AsyncCompute)];
                if (node.lifetime != Lifetime::Transient && pc.has)
                    edges.push_back({ pc.lastPass, kEpilogue });
                if (node.kind == ResourceKind::Texture && node.hasFinal)
                    finalTransition(res, segs.empty() ? start : segs.back(), segs.empty(), pend);
            }
        }

        void issue(uint16_t res, Queue q, BarrierPlacement placement, uint32_t pass, Pending* pend, Layout before, const UseState& after, bool discard)
        {
            BarrierRec rec = makeRec(res, q, placement, pass, pendingBefore(pend[kQ(q)], before), after);
            rec.discard    = discard;
            if (discard)
                rec.before.access = Access::NoAccess; // nothing to flush in memory being discarded
            emit(rec);
            pend[kQ(q)] = {};
        }

        // No layout change: a sync/access barrier on each queue of the new segment whose pending work
        // conflicts with it (a write on either side, or every access in fullBarriers mode).
        void syncOnlyTransition(uint16_t res, Layout layout, const Segment& seg, Pending* pend)
        {
            for (Queue q : { Queue::Graphics, Queue::AsyncCompute })
            {
                const Pending& p = pend[kQ(q)];
                if (seg.hasQueue[kQ(q)] && p.has && (p.wrote || !seg.readOnly || g.m_options.fullBarriers))
                    issue(res, q, BarrierPlacement::BeforePass, seg.first[kQ(q)], pend, layout, seg.perQueue[kQ(q)], false);
            }
        }

        void textureTransition(uint16_t res, const Segment& prev, const Segment& seg, Pending* pend)
        {
            const ResAccess& firstAccess = resAccess[res][seg.accesses.front()];
            const uint32_t   pn          = firstAccess.pass;
            const Queue      qn          = firstAccess.queue;
            const Queue      other       = otherQueue(qn);

            if (prev.layout == seg.layout && prev.layout != Layout::Undefined)
            {
                syncOnlyTransition(res, prev.layout, seg, pend);
                return;
            }

            if (layoutLegalOnQueue(prev.layout, qn))
            {
                // Transition on the queue of the first new access, right before it. The other queue's
                // pending work finishes first (sync point), and its later accesses follow the transition.
                issue(res, qn, BarrierPlacement::BeforePass, pn, pend, prev.layout, seg.perQueue[kQ(qn)], prev.layout == Layout::Undefined);
                if (pend[kQ(other)].has)
                    edges.push_back({ pend[kQ(other)].lastPass, pn });
                pend[kQ(other)] = {};
                if (seg.hasQueue[kQ(other)])
                    edges.push_back({ pn, seg.first[kQ(other)] });
                return;
            }

            // The old layout is graphics-only (render target / depth) and the first new access is on
            // compute: graphics changes the layout before it signals (§3.11 queue-legal placement).
            const UseState after = seg.hasQueue[kQ(Queue::Graphics)] ? seg.perQueue[kQ(Queue::Graphics)] : noAccess(seg.layout);
            const Pending& pg    = pend[kQ(Queue::Graphics)];
            if (pg.has)
            {
                const uint32_t lastG = pg.lastPass;
                issue(res, Queue::Graphics, BarrierPlacement::AfterPass, lastG, pend, prev.layout, after, false);
                edges.push_back({ lastG, pn });
            }
            else
            {
                issue(res, Queue::Graphics, BarrierPlacement::Prologue, kPrologue, pend, prev.layout, after, false);
                edges.push_back({ kPrologue, pn });
            }
            if (pend[kQ(Queue::AsyncCompute)].has)
                edges.push_back({ pend[kQ(Queue::AsyncCompute)].lastPass, pn });
            pend[kQ(Queue::AsyncCompute)] = {};
        }

        void finalTransition(uint16_t res, const Segment& last, bool untouched, Pending* pend)
        {
            const ResourceNode& node = g.m_resources[res];
            if (last.layout == node.finalLayout)
                return;
            if (untouched)
            {
                if (last.layout != Layout::Undefined)
                    issue(res, Queue::Graphics, BarrierPlacement::Epilogue, kEpilogue, pend, last.layout, noAccess(node.finalLayout), false);
                return;
            }
            const bool onG = pend[kQ(Queue::Graphics)].has;
            const bool onC = pend[kQ(Queue::AsyncCompute)].has;
            if (onG != onC)
            {
                // Only one queue still has work on it: change the layout there, after its last use.
                const Queue q = onG ? Queue::Graphics : Queue::AsyncCompute;
                if (layoutLegalOnQueue(node.finalLayout, q))
                {
                    issue(res, q, BarrierPlacement::AfterPass, pend[kQ(q)].lastPass, pend, last.layout, noAccess(node.finalLayout), false);
                    return;
                }
            }
            // Both queues (or a graphics-only final layout after compute): graphics, in the epilogue,
            // after the compute queue's last use (planBarriers adds that edge for persistent resources).
            issue(res, Queue::Graphics, BarrierPlacement::Epilogue, kEpilogue, pend, last.layout, noAccess(node.finalLayout), false);
        }

        // ---- 6. sync points --------------------------------------------------
        void buildSyncs()
        {
            std::sort(edges.begin(), edges.end());
            edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
            g.m_syncs.clear();
            for (Queue w : { Queue::Graphics, Queue::AsyncCompute })
            {
                // Items of the waiting queue in order, with the furthest item of the other queue each needs.
                std::vector<uint32_t> items;
                if (w == Queue::Graphics)
                    items.push_back(kPrologue);
                for (const uint32_t p : g.m_queueOrder[kQ(w)])
                    items.push_back(p);
                if (w == Queue::Graphics)
                    items.push_back(kEpilogue);
                const Queue s = otherQueue(w);
                // need[pos in items] = (max position on s, that item)
                std::vector<std::pair<int64_t, uint32_t>> need(items.size(), { -1, kNoPass });
                for (const auto& [from, to] : edges)
                {
                    if (itemQueue(from) != s || itemQueue(to) != w)
                        continue;
                    const uint32_t toPos   = itemPos(to); // == index in items
                    const int64_t  fromPos = itemPos(from);
                    if (fromPos > need[toPos].first)
                        need[toPos] = { fromPos, from };
                }
                int64_t covered = -1;
                for (uint32_t i = 0; i < items.size(); ++i)
                {
                    if (need[i].first > covered)
                    {
                        SyncPoint sp;
                        sp.signalQueue = s;
                        sp.afterPass   = need[i].second;
                        sp.waitQueue   = w;
                        sp.beforePass  = items[i];
                        g.m_syncs.push_back(sp);
                        covered = need[i].first;
                    }
                }
            }
            // Signal values: one per distinct signalling item, increasing along the queue.
            for (Queue s : { Queue::Graphics, Queue::AsyncCompute })
            {
                std::vector<uint32_t> positions;
                for (const SyncPoint& sp : g.m_syncs)
                    if (sp.signalQueue == s)
                        positions.push_back(itemPos(sp.afterPass));
                std::sort(positions.begin(), positions.end());
                positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
                for (SyncPoint& sp : g.m_syncs)
                    if (sp.signalQueue == s)
                        sp.signalValue = static_cast<uint32_t>(std::lower_bound(positions.begin(), positions.end(), itemPos(sp.afterPass)) - positions.begin()) + 1;
            }
            std::sort(g.m_syncs.begin(), g.m_syncs.end(),
                      [](const SyncPoint& a, const SyncPoint& b)
                      {
                          if (a.signalQueue != b.signalQueue)
                              return a.signalQueue < b.signalQueue;
                          if (a.signalValue != b.signalValue)
                              return a.signalValue < b.signalValue;
                          return a.waitQueue < b.waitQueue;
                      });
        }

        // ---- 7. alias plan ---------------------------------------------------
        void planAliasing()
        {
            g.m_aliasSlots.clear();
            g.m_transientBytesUnaliased = 0;
            g.m_transientBytesAliased   = 0;
            aliasedAfter.assign(g.m_resources.size(), kInvalidIndex);

            std::vector<uint16_t> transients;
            for (uint16_t r = 0; r < g.m_resources.size(); ++r)
                if (g.m_resources[r].lifetime == Lifetime::Transient && g.m_cRes[r].used)
                    transients.push_back(r);
            std::sort(transients.begin(), transients.end(), [&](uint16_t a, uint16_t b) { return g.m_cPass[g.m_cRes[a].firstPass].order < g.m_cPass[g.m_cRes[b].firstPass].order; });

            std::vector<uint8_t> shareable;
            for (const uint16_t r : transients)
            {
                const uint64_t bytes = g.m_cRes[r].bytes;
                g.m_transientBytesUnaliased += bytes;
                uint32_t best = kNoPass;
                if (!g.m_options.noAliasing && bytes > 0)
                {
                    for (uint32_t s = 0; s < g.m_aliasSlots.size(); ++s)
                    {
                        if (!shareable[s] || !g.resourceFollows(g.m_aliasSlots[s].resources.back(), r))
                            continue;
                        const uint64_t have = g.m_aliasSlots[s].bytes;
                        if (best == kNoPass)
                        {
                            best = s;
                            continue;
                        }
                        const uint64_t bestHave = g.m_aliasSlots[best].bytes;
                        // Smallest slot that already fits; otherwise the largest (least growth).
                        const bool fits = have >= bytes, bestFits = bestHave >= bytes;
                        if ((fits && (!bestFits || have < bestHave)) || (!fits && !bestFits && have > bestHave))
                            best = s;
                    }
                }
                if (best == kNoPass)
                {
                    g.m_aliasSlots.emplace_back();
                    shareable.push_back(bytes > 0 && !g.m_options.noAliasing);
                    best = static_cast<uint32_t>(g.m_aliasSlots.size() - 1);
                }
                else
                {
                    aliasedAfter[r] = g.m_aliasSlots[best].resources.back();
                }
                AliasSlot& slot = g.m_aliasSlots[best];
                slot.bytes      = std::max(slot.bytes, bytes);
                slot.resources.push_back(r);
                g.m_cRes[r].aliasSlot = best;
            }
            for (const AliasSlot& s : g.m_aliasSlots)
                g.m_transientBytesAliased += s.bytes;
        }

        // The first barrier of a transient that reuses memory waits for the previous occupant's work on
        // the same queue (the other queue is already ordered by a sync point).
        void aliasFixups()
        {
            for (uint16_t r = 0; r < g.m_resources.size(); ++r)
            {
                const uint16_t m = aliasedAfter[r];
                if (m == kInvalidIndex)
                    continue;
                const ResAccess& first = resAccess[r].front();
                Sync             prior = Sync::None;
                for (const ResAccess& a : resAccess[m])
                    if (a.queue == first.queue)
                        prior = prior | a.state.sync;
                if (g.m_options.fullBarriers && prior != Sync::None)
                    prior = Sync::All;
                bool patched = false;
                for (BarrierRec& b : g.m_barriers)
                {
                    if (b.resource == r && b.placement == BarrierPlacement::BeforePass && b.pass == first.pass && b.queue == first.queue)
                    {
                        b.before.sync = b.before.sync | prior;
                        b.aliased     = true;
                        patched       = true;
                    }
                }
                if (!patched)
                {
                    // Buffers get no first-use barrier of their own; add one when the memory is reused.
                    UseState after = first.state;
                    if (g.m_options.fullBarriers)
                        after.sync = Sync::All;
                    BarrierRec rec = makeRec(r, first.queue, BarrierPlacement::BeforePass, first.pass, { prior, Access::NoAccess, Layout::Undefined }, after);
                    rec.aliased    = true;
                    if (prior != Sync::None || g.m_resources[r].kind == ResourceKind::Texture)
                        g.m_barriers.push_back(rec);
                }
            }
        }

        // ---- 8. batches -----------------------------------------------------
        void buildBatches()
        {
            const auto rank = [&](const BarrierRec& b) -> uint64_t
            {
                switch (b.placement)
                {
                case BarrierPlacement::Prologue:
                    return 0;
                case BarrierPlacement::Epilogue:
                    return ~uint64_t{ 0 };
                case BarrierPlacement::BeforePass:
                    return 1 + uint64_t{ g.m_cPass[b.pass].order } * 2;
                case BarrierPlacement::AfterPass:
                    return 2 + uint64_t{ g.m_cPass[b.pass].order } * 2;
                }
                return 0;
            };
            std::stable_sort(g.m_barriers.begin(), g.m_barriers.end(),
                             [&](const BarrierRec& a, const BarrierRec& b)
                             {
                                 const uint64_t ra = rank(a), rb = rank(b);
                                 if (ra != rb)
                                     return ra < rb;
                                 return a.resource < b.resource;
                             });
            g.m_batches.clear();
            for (uint32_t i = 0; i < g.m_barriers.size(); ++i)
            {
                const BarrierRec& b = g.m_barriers[i];
                if (!g.m_batches.empty())
                {
                    BarrierBatch& last = g.m_batches.back();
                    if (last.placement == b.placement && last.pass == b.pass && last.queue == b.queue)
                    {
                        ++last.count;
                        continue;
                    }
                }
                g.m_batches.push_back({ b.placement, b.pass, b.queue, i, 1 });
            }
        }

        bool run()
        {
            const uint32_t n = static_cast<uint32_t>(g.m_passes.size());
            g.m_cPass.assign(n, {});
            for (uint32_t p = 0; p < n; ++p)
                g.m_cPass[p].queue = g.m_options.asyncCompute ? g.m_passes[p].declaredQueue : Queue::Graphics;
            cull();
            if (!order())
                return false;
            if (!mergeUses())
                return false;
            planBarriers();
            buildSyncs();
            g.m_edges = edges;
            planAliasing();
            aliasFixups();
            buildBatches();
            return true;
        }
    };

    uint64_t RenderGraph::computePlanKey() const
    {
        Fnv f;
        f.value(m_options);
        f.value(m_outputWidth);
        f.value(m_outputHeight);
        f.value(m_passes.size());
        for (const PassNode& p : m_passes)
        {
            f.text(p.name);
            f.value(p.flags);
            f.value(p.declaredQueue);
            f.value(p.accesses.size());
            for (const AccessRec& a : p.accesses)
            {
                f.value(a.resource);
                f.value(a.version);
                f.value(a.use);
                f.value(a.read);
                f.value(a.write);
            }
        }
        f.value(m_resources.size());
        for (const ResourceNode& r : m_resources)
        {
            f.text(r.name);
            f.value(r.kind);
            f.value(r.lifetime);
            f.value(r.tex.sizeMode);
            f.value(r.tex.scale);
            f.value(r.width);
            f.value(r.height);
            f.value(r.tex.mips);
            f.value(r.tex.arraySize);
            f.value(r.tex.format);
            f.value(r.buf.size);
            f.value(r.initial);
            f.value(r.finalLayout);
            f.value(r.hasFinal);
            f.value(r.versions.size());
            for (const VersionNode& v : r.versions)
                f.value(v.writer);
        }
        return f.h;
    }

    bool RenderGraph::compile()
    {
        m_compiledOk = false;
        m_fromCache  = false;
        if (!m_errors.empty())
        {
            m_haveLastGood = false;
            logErrorsOnce();
            return false;
        }
        Impl impl(*this);
        if (!impl.checkQueues())
        {
            m_haveLastGood = false;
            logErrorsOnce();
            return false;
        }
        m_planKey = computePlanKey();
        if (m_haveLastGood && m_planKey == m_lastGoodKey)
        {
            // Same passes, accesses, descs, size and options as the last good compile: reuse its plan (§3.5).
            m_fromCache  = true;
            m_compiledOk = true;
            return true;
        }
        m_haveLastGood = false;
        if (!impl.run())
        {
            logErrorsOnce();
            return false;
        }
        m_compiledOk   = true;
        m_lastGoodKey  = m_planKey;
        m_haveLastGood = true;
        if (m_options.validate)
        {
            std::string why;
            if (!validateCompiled(&why))
                DE_LOG_ERROR(LogCategory::Render, "RenderGraph: rg.validate: {}", why);
        }
        return true;
    }

    bool RenderGraph::happensBefore(uint32_t a, uint32_t b) const
    {
        if (a == b)
            return false;
        const auto queueOf = [&](uint32_t item) { return item == kPrologue || item == kEpilogue ? Queue::Graphics : m_cPass[item].queue; };
        const auto posOf   = [&](uint32_t item) -> uint32_t
        {
            if (item == kPrologue)
                return 0;
            if (item == kEpilogue)
                return static_cast<uint32_t>(m_queueOrder[0].size()) + 1;
            return m_cPass[item].queuePos;
        };
        const auto valid = [&](uint32_t item) { return item == kPrologue || item == kEpilogue || (item < m_cPass.size() && m_cPass[item].live); };
        if (!valid(a) || !valid(b))
            return false;
        const Queue qa = queueOf(a), qb = queueOf(b);
        if (qa == qb)
            return posOf(a) < posOf(b);
        // Two queues: a chain of sync points reduces to one direct sync from qa to qb.
        for (const SyncPoint& s : m_syncs)
            if (s.signalQueue == qa && s.waitQueue == qb && posOf(s.afterPass) >= posOf(a) && posOf(s.beforePass) <= posOf(b))
                return true;
        return false;
    }

    bool RenderGraph::resourceFollows(uint16_t earlier, uint16_t later) const
    {
        // Every use of earlier completes before any use of later starts: per queue, earlier's last
        // use happens before later's first use on each queue.
        const CompiledResource& m = m_cRes[earlier];
        const CompiledResource& r = m_cRes[later];
        for (uint32_t qm = 0; qm < kQueueCount; ++qm)
        {
            if (m.lastOn[qm] == kNoPass)
                continue;
            for (uint32_t qr = 0; qr < kQueueCount; ++qr)
                if (r.firstOn[qr] != kNoPass && !happensBefore(m.lastOn[qm], r.firstOn[qr]))
                    return false;
        }
        return true;
    }

    bool RenderGraph::validateCompiled(std::string* why) const
    {
        const auto fail = [&](std::string text)
        {
            if (why)
                *why = std::move(text);
            return false;
        };
        if (!m_compiledOk)
            return fail("not compiled");
        // Dependencies are covered by the schedule.
        for (const auto& [from, to] : m_edges)
        {
            if (from != kPrologue && to != kEpilogue && m_cPass[from].order >= m_cPass[to].order)
                return fail(std::string("order breaks edge ") + passName(from) + " -> " + passName(to));
            if (!happensBefore(from, to))
                return fail(std::string("no sync covers ") + passName(from) + " -> " + passName(to));
        }
        // Simulate both queues: every wait must name a value already signalled (no deadlock).
        {
            std::vector<uint32_t> items[kQueueCount];
            items[0].push_back(kPrologue);
            for (const uint32_t p : m_queueOrder[0])
                items[0].push_back(p);
            items[0].push_back(kEpilogue);
            for (const uint32_t p : m_queueOrder[1])
                items[1].push_back(p);
            size_t   cursor[kQueueCount]   = { 0, 0 };
            uint32_t signaled[kQueueCount] = { 0, 0 };
            bool     progress              = true;
            while (progress)
            {
                progress = false;
                for (uint32_t q = 0; q < kQueueCount; ++q)
                {
                    while (cursor[q] < items[q].size())
                    {
                        const uint32_t item  = items[q][cursor[q]];
                        bool           ready = true;
                        for (const SyncPoint& s : m_syncs)
                            if (s.beforePass == item && kQ(s.waitQueue) == q && signaled[kQ(s.signalQueue)] < s.signalValue)
                                ready = false;
                        if (!ready)
                            break;
                        for (const SyncPoint& s : m_syncs)
                        {
                            if (s.afterPass == item && kQ(s.signalQueue) == q)
                            {
                                if (s.signalValue < signaled[q])
                                    return fail("signal values not increasing on a queue");
                                signaled[q] = s.signalValue;
                            }
                        }
                        ++cursor[q];
                        progress = true;
                    }
                }
            }
            if (cursor[0] != items[0].size() || cursor[1] != items[1].size())
                return fail("queues deadlock on a wait with no earlier signal");
        }
        for (const SyncPoint& s : m_syncs)
        {
            const Queue sq = s.afterPass == kPrologue ? Queue::Graphics : m_cPass[s.afterPass].queue;
            const Queue wq = s.beforePass == kEpilogue ? Queue::Graphics : m_cPass[s.beforePass].queue;
            if (sq != s.signalQueue || wq != s.waitQueue || sq == wq)
                return fail("sync point on the wrong queue");
        }
        // Barriers: valid, on the right queue, one per resource per batch.
        for (const BarrierRec& b : m_barriers)
        {
            std::string reason;
            if (!validateBarrier(b, &reason))
                return fail(std::string("barrier on '") + m_resources[b.resource].name + "' at " + passName(b.pass) + ": " + reason);
            if ((b.placement == BarrierPlacement::BeforePass || b.placement == BarrierPlacement::AfterPass) && (b.pass >= m_cPass.size() || !m_cPass[b.pass].live || m_cPass[b.pass].queue != b.queue))
                return fail(std::string("barrier on '") + m_resources[b.resource].name + "' placed on a pass of another queue");
        }
        for (const BarrierBatch& batch : m_batches)
            for (uint32_t i = batch.first; i < batch.first + batch.count; ++i)
                for (uint32_t j = i + 1; j < batch.first + batch.count; ++j)
                    if (m_barriers[i].resource == m_barriers[j].resource)
                        return fail(std::string("two barriers for '") + m_resources[m_barriers[i].resource].name + "' in one batch");
        // Aliased transients never overlap in time.
        for (const AliasSlot& slot : m_aliasSlots)
            for (size_t i = 1; i < slot.resources.size(); ++i)
                if (!resourceFollows(slot.resources[i - 1], slot.resources[i]))
                    return fail(std::string("alias slot overlaps '") + m_resources[slot.resources[i - 1]].name + "' and '" + m_resources[slot.resources[i]].name + "'");
        return true;
    }

} // namespace Dark::Rg
