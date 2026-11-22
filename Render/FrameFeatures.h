#pragma once

#include "Render/RenderGraph.h"

#include <cstdint>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <vector>

// Feature lists and the per-frame pass builder (render-graph plan Phase 4).
//
// A feature list is a host's frame: the passes it may run, in order, with the persistent resources
// each one reads and writes. FeatureFrame turns one frame of it into graph passes: the host sets an
// execute callback per PassId, and every set entry becomes an opaque pass (OwnBarriers | SideEffect)
// in list order, declaring the list's reads and writes on the imported resources. The compiled
// order is checked against the list (the graph's replacement for the old passSequenceOk table check).
// CPU only: FrameComposer adds the D3D12 imports and the executor.

namespace Dark
{

    // Persistent resources the passes touch, imported into the graph every frame.
    enum class RenderResource : uint8_t
    {
        Swap = 0, // the back buffer (import starts and ends in Present)
        Depth,
        Shadow,
        LocalShadow,
        Albedo,
        Attrib,
        Velocity,
        Ao,
        Hdr,
        Post,
        // History pairs (RenderGraph::history, Phase 5). The handle is this frame's curr; a pass that
        // writes one also reads its prev when prev is valid. Not imported: FrameComposer::history.
        TaaHistory,
        GtaoHistory,
        SsrHistory,
        Count,
    };
    inline constexpr uint32_t kRenderResourceCount = static_cast<uint32_t>(RenderResource::Count);

    enum class PassId : uint8_t
    {
        Shadows, // frame GPU cull + directional cascades
        LocalShadows,
        GBuffer,
        Decals, // decals, then the HDR bind and clear
        Gtao,
        Ssr,
        Lighting,
        LocalVolumes,
        Sky,
        SsrCapture,
        Water,
        Clouds,
        Camouflage,
        Translucent,
        Particles,
        Underwater,
        Post,
        Overlay, // debug tiles on the back buffer
        EditorGrid,
        EditorGizmos,
        DebugOverlays, // path debug, walk / trail / skeleton overlays, decal volumes (into HDR)
        Count,
    };
    inline constexpr uint32_t kPassIdCount = static_cast<uint32_t>(PassId::Count);

    struct FeaturePass
    {
        PassId         id;
        bool           optional;
        uint8_t        readCount;
        uint8_t        writeCount;
        RenderResource reads[8];
        RenderResource writes[4];
    };

    const FeaturePass* sandboxFeatures(uint32_t& count);
    const FeaturePass* editorFeatures(uint32_t& count);
    const char*        passName(PassId id);
    const char*        renderResourceName(RenderResource resource);

    // `played` must be `list` in list order, skipping only optional entries.
    bool featureOrderOk(const FeaturePass* list, uint32_t count, const PassId* played, uint32_t playedCount);
    // Host check: a failure latches the run as unsuitable for a baseline and logs the host, the list
    // and the played order (once per list). The counter counts frames with a failed check.
    bool     validateHostFeatureOrder(const char* host, const FeaturePass* list, uint32_t count, const PassId* played, uint32_t playedCount);
    uint32_t hostPassSequenceFailures();

    class FeatureFrame;

    // What a converted feature (Phase 6) sees while its list entry is added: the current version of
    // every list resource, and passes it adds with declared uses (no OwnBarriers, so the graph plans
    // their barriers). Every pass it adds counts as its PassId for the order check. Setup time only.
    class FeatureBuilder
    {
    public:
        Rg::RenderGraph& graph()
        {
            return m_graph;
        }
        PassId id() const
        {
            return m_id;
        }
        // This frame's handle (current version) of a list resource; for a history resource, curr.
        Rg::TextureHandle handle(RenderResource resource) const;
        // A history resource's prev (invalid when there is none to read).
        Rg::TextureHandle prev(RenderResource resource) const;
        // Records the new version a pass wrote, so later passes (and later list entries) see it.
        void set(RenderResource resource, Rg::TextureHandle h);

        // A pass with declared uses. Same contract as RenderGraph::addPass.
        template <class Data, class Setup, class Exec> Data& addPass(const char* name, Setup&& setup, Exec&& exec)
        {
            Data& data = m_graph.addPass<Data>(name, std::forward<Setup>(setup), std::forward<Exec>(exec));
            notePass();
            return data;
        }
        // An opaque pass (OwnBarriers | SideEffect) for work that still records its own barriers,
        // declaring `reads` and `writes` the way the list entries do. `fn(PassContext&)`.
        template <class Fn> void opaque(const char* name, std::initializer_list<RenderResource> reads, std::initializer_list<RenderResource> writes, Fn&& fn);

    private:
        friend class FeatureFrame;
        FeatureBuilder(FeatureFrame& frame, Rg::RenderGraph& graph, PassId id) :
            m_frame(frame),
            m_graph(graph),
            m_id(id)
        {
        }
        void notePass();

        FeatureFrame&    m_frame;
        Rg::RenderGraph& m_graph;
        PassId           m_id;
    };

    class FeatureFrame
    {
    public:
        FeatureFrame()                               = default;
        FeatureFrame(const FeatureFrame&)            = delete;
        FeatureFrame& operator=(const FeatureFrame&) = delete;

        // Starts a frame on `list`: drops last frame's callbacks and imports.
        void begin(const FeaturePass* list, uint32_t count);

        // Imports one persistent resource into `g` (after g.beginFrame). Swap is the back buffer.
        // `initial` is the resource's layout when the frame starts (the ResourceStateTracker's, Phase 6;
        // Undefined when unknown). No import states a final layout: the owners and Renderer::endFrame
        // (back buffer RT -> PRESENT after ImGui and the movie) move them on through the tracker.
        // `views` are the resource's CPU views for converted passes (PassContext::rtv/srvTable, Phase 6).
        void importResource(Rg::RenderGraph& g, RenderResource resource, ID3D12Resource* native, const Rg::TextureDesc& desc, Rg::Layout initial = Rg::Layout::Undefined,
                            const Rg::TextureViews& views = {});
        // Binds a history pair (g.history) to one of the *History resources for this frame's passes.
        void setHistory(RenderResource resource, const Rg::HistoryPair& pair);

        // The callback runs as the pass's execute, with the pass's PassContext. It is stored until the
        // next begin(), so it may capture the caller's locals by reference only if they outlive
        // compileFrame + RenderGraph::execute. False (logged once) when `id` is not in the list.
        template <class Fn> bool set(PassId id, Fn&& fn)
        {
            using FnT = std::decay_t<Fn>;
            if (!acceptSet(id))
                return false;
            FnT* stored            = m_arena.create<FnT>(std::forward<Fn>(fn));
            m_slots[slotIndex(id)] = { stored, [](void* f, Rg::PassContext& ctx) { (*static_cast<FnT*>(f))(ctx); }, nullptr };
            return true;
        }
        // A converted entry (Phase 6): instead of one opaque pass, `fn(FeatureBuilder&)` runs when the
        // entry is added (in list order, at compileFrame) and adds the entry's passes with declared
        // uses. It replaces a set() callback for the same id and vice versa. Same lifetime rules as set().
        template <class Fn> bool declare(PassId id, Fn&& fn)
        {
            using FnT = std::decay_t<Fn>;
            if (!acceptSet(id))
                return false;
            FnT* stored            = m_arena.create<FnT>(std::forward<Fn>(fn));
            m_slots[slotIndex(id)] = { stored, nullptr, [](void* f, FeatureBuilder& b) { (*static_cast<FnT*>(f))(b); } };
            return true;
        }
        bool isSet(PassId id) const
        {
            const Slot& s = m_slots[slotIndex(id)];
            return s.call != nullptr || s.declare != nullptr;
        }
        bool isDeclared(PassId id) const
        {
            return m_slots[slotIndex(id)].declare != nullptr;
        }

        // Adds the set passes in list order, compiles `g` and checks the compiled order against the
        // list (validateHostFeatureOrder: logged and counted, see orderOk()). False only when compile
        // fails (logged by the graph and counted as a failed frame); the frame may then not execute.
        bool compileFrame(Rg::RenderGraph& g, const char* host);
        // The last compileFrame's order check. A failure still lets the frame execute, as the old
        // table check did: it marks the run unsuitable for a baseline.
        bool orderOk() const
        {
            return m_orderOk;
        }

        // The compiled order as PassIds (valid after compileFrame).
        const std::vector<PassId>& compiledOrder() const
        {
            return m_compiled;
        }
        Rg::TextureHandle handle(RenderResource resource) const
        {
            return m_handles[static_cast<uint32_t>(resource)];
        }
        Rg::TextureHandle prevHandle(RenderResource resource) const
        {
            return m_prevHandles[static_cast<uint32_t>(resource)];
        }

    private:
        friend class FeatureBuilder;
        using CallFn    = void (*)(void* fn, Rg::PassContext& ctx);
        using DeclareFn = void (*)(void* fn, FeatureBuilder& builder);
        struct Slot
        {
            void*     fn      = nullptr;
            CallFn    call    = nullptr;
            DeclareFn declare = nullptr;
        };
        static uint32_t slotIndex(PassId id)
        {
            const uint32_t i = static_cast<uint32_t>(id);
            return i < kPassIdCount ? i : kPassIdCount - 1;
        }
        bool acceptSet(PassId id);
        void addPasses(Rg::RenderGraph& g);
        // One opaque pass for `node`-style reads/writes running `slot`.
        void addOpaque(Rg::RenderGraph& g, const char* name, PassId id, const RenderResource* reads, uint32_t readCount, const RenderResource* writes, uint32_t writeCount,
                       Slot slot);

        Rg::Arena           m_arena;
        const FeaturePass*  m_list  = nullptr;
        uint32_t            m_count = 0;
        Slot                m_slots[kPassIdCount];
        Rg::TextureHandle   m_handles[kRenderResourceCount];
        Rg::TextureHandle   m_prevHandles[kRenderResourceCount]; // history resources only
        std::vector<PassId> m_graphIds; // PassId per graph pass index (from m_firstPass)
        uint32_t            m_firstPass = 0;
        std::vector<PassId> m_compiled;
        bool                m_orderOk = false;
    };

    template <class Fn>
    void FeatureBuilder::opaque(const char* name, std::initializer_list<RenderResource> reads, std::initializer_list<RenderResource> writes, Fn&& fn)
    {
        using FnT                = std::decay_t<Fn>;
        FnT*                stored = m_frame.m_arena.create<FnT>(std::forward<Fn>(fn));
        FeatureFrame::Slot  slot{ stored, [](void* f, Rg::PassContext& ctx) { (*static_cast<FnT*>(f))(ctx); }, nullptr };
        m_frame.addOpaque(m_graph, name, m_id, reads.begin(), static_cast<uint32_t>(reads.size()), writes.begin(), static_cast<uint32_t>(writes.size()), slot);
    }

} // namespace Dark
