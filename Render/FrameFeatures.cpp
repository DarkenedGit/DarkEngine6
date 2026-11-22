#include "Render/FrameFeatures.h"

#include "Core/Log.h"

#include <initializer_list>
#include <string>

namespace Dark
{

    namespace
    {

        FeaturePass makeNode(PassId id, bool optional, std::initializer_list<RenderResource> reads, std::initializer_list<RenderResource> writes)
        {
            FeaturePass node{};
            node.id         = id;
            node.optional   = optional;
            node.readCount  = 0;
            node.writeCount = 0;
            for (RenderResource resource : reads)
            {
                if (node.readCount >= 8)
                    break;
                node.reads[node.readCount++] = resource;
            }
            for (RenderResource resource : writes)
            {
                if (node.writeCount >= 4)
                    break;
                node.writes[node.writeCount++] = resource;
            }
            return node;
        }

        uint32_t g_hostSequenceFailures = 0;

        using R = RenderResource;

        bool isHistory(RenderResource r)
        {
            return r == R::TaaHistory || r == R::GtaoHistory || r == R::SsrHistory;
        }

        // How an opaque pass declares a list resource. Uses only order the graph (OwnBarriers passes
        // get no barriers), but they still pass the graph's per-use checks.
        bool isDepthLike(RenderResource r)
        {
            return r == R::Depth || r == R::Shadow || r == R::LocalShadow;
        }

    } // namespace

    bool featureOrderOk(const FeaturePass* list, uint32_t count, const PassId* played, uint32_t playedCount)
    {
        if ((list == nullptr && count > 0) || (played == nullptr && playedCount > 0))
            return false;

        uint32_t listIndex = 0;
        for (uint32_t playedIndex = 0; playedIndex < playedCount; ++playedIndex)
        {
            bool found = false;
            while (listIndex < count)
            {
                if (list[listIndex].id == played[playedIndex])
                {
                    found = true;
                    ++listIndex;
                    break;
                }
                if (!list[listIndex].optional)
                    return false;
                ++listIndex;
            }
            if (!found)
                return false;
        }
        while (listIndex < count)
        {
            if (!list[listIndex].optional)
                return false;
            ++listIndex;
        }
        return true;
    }

    const char* passName(PassId id)
    {
        static const char* const kNames[] = {
            "Shadows", "LocalShadows", "GBuffer",     "Decals",    "GTAO",       "SSR",  "Lighting", "LocalVolumes", "Sky",          "SsrCapture",    "Water",
            "Clouds",  "Camouflage",   "Translucent", "Particles", "Underwater", "Post", "Overlay",  "EditorGrid",   "EditorGizmos", "DebugOverlays",
        };
        static_assert(sizeof(kNames) / sizeof(kNames[0]) == kPassIdCount, "one name per PassId");
        const auto i = static_cast<uint32_t>(id);
        return i < kPassIdCount ? kNames[i] : "Unknown";
    }

    const char* renderResourceName(RenderResource resource)
    {
        static const char* const kNames[] = { "Swap", "Depth", "Shadow", "LocalShadow", "Albedo", "Attrib", "Velocity", "Ao", "Hdr", "Post", "TaaHistory", "GtaoHistory", "SsrHistory" };
        static_assert(sizeof(kNames) / sizeof(kNames[0]) == kRenderResourceCount, "one name per RenderResource");
        const auto i = static_cast<uint32_t>(resource);
        return i < kRenderResourceCount ? kNames[i] : "Unknown";
    }

    uint32_t hostPassSequenceFailures()
    {
        return g_hostSequenceFailures;
    }

    bool validateHostFeatureOrder(const char* host, const FeaturePass* list, uint32_t count, const PassId* played, uint32_t playedCount)
    {
        if (featureOrderOk(list, count, played, playedCount))
            return true;
        ++g_hostSequenceFailures;
        static const FeaturePass* loggedLists[8]{};
        for (const FeaturePass* seen : loggedLists)
        {
            if (seen == list && seen != nullptr)
                return false;
        }
        for (const FeaturePass*& slot : loggedLists)
        {
            if (!slot)
            {
                slot = list;
                break;
            }
        }
        std::string expected;
        std::string actual;
        for (uint32_t i = 0; list && i < count; ++i)
        {
            if (!expected.empty())
                expected += " -> ";
            expected += passName(list[i].id);
            if (list[i].optional)
                expected += "?";
        }
        for (uint32_t i = 0; played && i < playedCount; ++i)
        {
            if (!actual.empty())
                actual += " -> ";
            actual += passName(played[i]);
        }
        DE_LOG_ERROR(LogCategory::Render, "{}: PASS SEQUENCE FAILED — BASELINE INVALID. Expected (? = optional): {}. Compiled: {}", host ? host : "host", expected, actual);
        return false;
    }

    const FeaturePass* sandboxFeatures(uint32_t& count)
    {
        static const FeaturePass kList[] = {
            makeNode(PassId::Shadows, true, {}, { R::Shadow }),
            makeNode(PassId::LocalShadows, true, {}, { R::LocalShadow }),
            makeNode(PassId::GBuffer, false, {}, { R::Albedo, R::Attrib, R::Velocity, R::Depth }),
            makeNode(PassId::Decals, false, { R::Depth }, { R::Albedo, R::Attrib, R::Hdr }),
            makeNode(PassId::Gtao, false, { R::Depth, R::Attrib }, { R::Ao, R::GtaoHistory }),
            makeNode(PassId::Ssr, false, { R::Depth, R::Albedo }, { R::SsrHistory }),
            makeNode(PassId::Lighting, false, { R::Albedo, R::Attrib, R::Depth, R::Shadow }, { R::Hdr }),
            makeNode(PassId::LocalVolumes, false, { R::Hdr }, { R::Hdr }),
            makeNode(PassId::Sky, true, { R::Depth }, { R::Hdr }),
            makeNode(PassId::SsrCapture, true, { R::Hdr }, { R::Post }),
            makeNode(PassId::Water, true, { R::Depth }, { R::Hdr }),
            makeNode(PassId::Clouds, true, {}, { R::Hdr }),
            makeNode(PassId::Camouflage, true, {}, { R::Hdr }),
            makeNode(PassId::Translucent, false, { R::Depth }, { R::Hdr }),
            makeNode(PassId::DebugOverlays, true, { R::Depth }, { R::Hdr }),
            // GPU particles read this frame's Depth and, on Bounce, Attrib; the non-deferred blood splats run at the end.
            makeNode(PassId::Particles, true, {}, { R::Hdr }),
            makeNode(PassId::Underwater, true, { R::Hdr, R::Depth }, { R::Hdr }),
            makeNode(PassId::Post, false, { R::Hdr }, { R::Swap, R::TaaHistory }),
            makeNode(PassId::Overlay, true, {}, { R::Swap }),
        };
        count = static_cast<uint32_t>(sizeof(kList) / sizeof(kList[0]));
        return kList;
    }

    const FeaturePass* editorFeatures(uint32_t& count)
    {
        static const FeaturePass kList[] = {
            makeNode(PassId::Shadows, true, {}, { R::Shadow }),
            makeNode(PassId::LocalShadows, true, {}, { R::LocalShadow }),
            makeNode(PassId::GBuffer, false, {}, { R::Albedo, R::Attrib, R::Velocity, R::Depth }),
            makeNode(PassId::Decals, false, { R::Depth }, { R::Albedo, R::Attrib, R::Hdr }),
            makeNode(PassId::Gtao, false, { R::Depth, R::Attrib }, { R::Ao, R::GtaoHistory }),
            makeNode(PassId::Ssr, false, { R::Depth, R::Albedo }, { R::SsrHistory }),
            makeNode(PassId::Lighting, false, { R::Albedo, R::Attrib, R::Depth, R::Shadow }, { R::Hdr }),
            makeNode(PassId::LocalVolumes, false, { R::Hdr }, { R::Hdr }),
            makeNode(PassId::Sky, true, { R::Depth }, { R::Hdr }),
            makeNode(PassId::SsrCapture, false, { R::Hdr }, { R::Post }),
            makeNode(PassId::Water, true, { R::Depth }, { R::Hdr }),
            makeNode(PassId::Clouds, true, {}, { R::Hdr }),
            makeNode(PassId::EditorGrid, true, {}, { R::Hdr }),
            makeNode(PassId::EditorGizmos, true, {}, { R::Hdr }),
            makeNode(PassId::DebugOverlays, true, { R::Depth }, { R::Hdr }),
            makeNode(PassId::Translucent, false, { R::Depth }, { R::Hdr }),
            // GPU particles read this frame's Depth and, on Bounce, Attrib; the non-deferred blood splats run at the end.
            makeNode(PassId::Particles, true, {}, { R::Hdr }),
            makeNode(PassId::Underwater, true, { R::Hdr, R::Depth }, { R::Hdr }),
            makeNode(PassId::Post, false, { R::Hdr }, { R::Swap, R::TaaHistory }),
            makeNode(PassId::Overlay, true, {}, { R::Swap }),
        };
        count = static_cast<uint32_t>(sizeof(kList) / sizeof(kList[0]));
        return kList;
    }

    // ---------------------------------------------------------------- FeatureFrame

    void FeatureFrame::begin(const FeaturePass* list, uint32_t count)
    {
        for (Slot& s : m_slots)
            s = {};
        for (Rg::TextureHandle& h : m_handles)
            h = {};
        for (Rg::TextureHandle& h : m_prevHandles)
            h = {};
        m_arena.reset();
        m_list  = list;
        m_count = list ? count : 0;
        m_graphIds.clear();
        m_compiled.clear();
    }

    void FeatureFrame::importResource(Rg::RenderGraph& g, RenderResource resource, ID3D12Resource* native, const Rg::TextureDesc& desc)
    {
        const uint32_t i = static_cast<uint32_t>(resource);
        if (i >= kRenderResourceCount || isHistory(resource))
            return; // histories come from setHistory (RenderGraph::history), never an import
        // Phase 4: the opaque passes and the legacy trackers own every state change, so only the back
        // buffer states its layouts (Present is its final layout, not a pass use).
        m_handles[i] = resource == RenderResource::Swap ? g.importBackBuffer(native, desc.width, desc.height, desc.format)
                                                        : g.importTexture(renderResourceName(resource), desc, native, Rg::Layout::Undefined, Rg::Layout::Undefined);
    }

    void FeatureFrame::setHistory(RenderResource resource, const Rg::HistoryPair& pair)
    {
        if (!isHistory(resource))
            return;
        const uint32_t i = static_cast<uint32_t>(resource);
        m_handles[i]     = pair.curr;
        m_prevHandles[i] = pair.prev;
    }

    bool FeatureFrame::acceptSet(PassId id)
    {
        bool inList = false;
        for (uint32_t i = 0; i < m_count && !inList; ++i)
            inList = m_list[i].id == id;
        if (inList)
        {
            static bool warned = false;
            if (m_slots[slotIndex(id)].call && !warned)
            {
                warned = true;
                DE_LOG_WARN(LogCategory::Render, "FeatureFrame: pass {} set twice in one frame; the last callback runs (warns once)", passName(id));
            }
            return true;
        }
        static uint64_t logged = 0; // one bit per PassId
        const uint32_t  bit    = static_cast<uint32_t>(id) < 64 ? static_cast<uint32_t>(id) : 63;
        if (!(logged & (uint64_t{ 1 } << bit)))
        {
            logged |= uint64_t{ 1 } << bit;
            DE_LOG_ERROR(LogCategory::Render, "FeatureFrame: pass {} is not in this host's feature list; it does not run", passName(id));
        }
        return false;
    }

    void FeatureFrame::addPasses(Rg::RenderGraph& g)
    {
        for (uint32_t i = 0; i < m_count; ++i)
        {
            const FeaturePass& node = m_list[i];
            const Slot         slot = m_slots[slotIndex(node.id)];
            if (!slot.call)
                continue;
            g.addPass<Rg::NoData>(
                passName(node.id),
                [&](Rg::PassBuilder& b, Rg::NoData&)
                {
                    b.flags(Rg::PassFlags::OwnBarriers | Rg::PassFlags::SideEffect);
                    bool written[kRenderResourceCount] = {};
                    for (uint32_t w = 0; w < node.writeCount; ++w)
                        written[static_cast<uint32_t>(node.writes[w])] = true;
                    for (uint32_t r = 0; r < node.readCount; ++r)
                    {
                        const RenderResource res = node.reads[r];
                        Rg::TextureHandle&   h   = m_handles[static_cast<uint32_t>(res)];
                        if (h.valid() && !written[static_cast<uint32_t>(res)])
                            b.read(h, isDepthLike(res) ? Rg::Use::DepthRead : Rg::Use::PixelSrv);
                    }
                    for (uint32_t w = 0; w < node.writeCount; ++w)
                    {
                        const RenderResource res = node.writes[w];
                        Rg::TextureHandle&   h   = m_handles[static_cast<uint32_t>(res)];
                        if (!h.valid())
                            continue;
                        if (isHistory(res))
                        {
                            // Reads last frame's contents (when they are valid) and writes this frame's.
                            const Rg::TextureHandle prev = m_prevHandles[static_cast<uint32_t>(res)];
                            if (prev.valid())
                                b.read(prev, Rg::Use::PixelSrv);
                            h = b.write(h, Rg::Use::RenderTarget);
                            continue;
                        }
                        bool alsoRead = false;
                        for (uint32_t r = 0; r < node.readCount; ++r)
                            alsoRead = alsoRead || node.reads[r] == res;
                        const Rg::Use use = isDepthLike(res) ? Rg::Use::DepthWrite : Rg::Use::RenderTarget;
                        h                 = alsoRead ? b.readWrite(h, use) : b.write(h, use);
                    }
                },
                [slot](Rg::PassContext& ctx, const Rg::NoData&) { slot.call(slot.fn, ctx); });
            m_graphIds.push_back(node.id);
        }
    }

    bool FeatureFrame::compileFrame(Rg::RenderGraph& g, const char* host)
    {
        m_graphIds.clear();
        m_compiled.clear();
        m_orderOk                = false;
        const uint32_t firstPass = g.passCount();
        addPasses(g);
        if (!g.compile())
        {
            ++g_hostSequenceFailures;
            return false;
        }
        for (const uint32_t p : g.order())
        {
            if (p >= firstPass && p - firstPass < m_graphIds.size())
                m_compiled.push_back(m_graphIds[p - firstPass]);
        }
        m_orderOk = validateHostFeatureOrder(host, m_list, m_count, m_compiled.data(), static_cast<uint32_t>(m_compiled.size()));
        return true;
    }

} // namespace Dark
