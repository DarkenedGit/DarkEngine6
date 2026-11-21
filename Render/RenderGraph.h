#pragma once

#include "Render/RenderGraphTypes.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// CPU render graph core (render-graph plan Phase 3, §3.3, §3.5-§3.7, §3.11).
//
// Per frame: beginFrame(output size) -> addPass/import/history -> compile() -> execute(executor).
// compile() culls from the sinks, orders the live passes (topological, insertion order preferred),
// assigns queues (everything on Graphics unless CompileOptions::asyncCompute), computes the minimal
// cross-queue sync points, plans enhanced barriers as data (queue-legal, merged per pass), resource
// lifetimes and a transient alias plan. Errors are returned as false plus errors() and logged once;
// nothing throws. No D3D12 calls: the rhi executor (Phase 4) turns the records into
// ID3D12GraphicsCommandList7::Barrier and fence Signal/Wait calls. The graph does not drive the
// frame yet (Phase 4).

namespace Dark::Rg
{

    class RenderGraph;
    class PassContext;

    // Per-frame linear allocator for pass data, execute lambdas and names. Objects with a destructor
    // are destroyed (in reverse order) by reset().
    class Arena
    {
    public:
        Arena() = default;
        // Test hook: every block's usable base sits at (address % 64) == baseRemainder (a multiple of
        // 16), so tests see a 16-aligned allocator on any platform. 0 = use the allocation as is.
        explicit Arena(size_t baseRemainder) :
            m_baseRemainder(baseRemainder)
        {
        }
        ~Arena()
        {
            reset();
        }
        Arena(const Arena&)            = delete;
        Arena& operator=(const Arena&) = delete;

        void*                                allocate(size_t bytes, size_t align);
        template <class T, class... Args> T* create(Args&&... args)
        {
            void* mem = allocate(sizeof(T), alignof(T));
            T*    obj = ::new (mem) T(std::forward<Args>(args)...);
            if constexpr (!std::is_trivially_destructible_v<T>)
                m_destructors.push_back({ obj, [](void* p) { static_cast<T*>(p)->~T(); } });
            return obj;
        }
        const char* copyString(const char* text);
        void        reset();
        size_t      bytesUsed() const
        {
            return m_used;
        }

    private:
        struct Block
        {
            std::unique_ptr<std::byte[]> data;
            std::byte*                   base = nullptr; // start of the usable bytes (data, or skewed by the test hook)
            size_t                       size = 0;       // usable bytes from base
            size_t                       used = 0;       // bytes used from base
        };
        struct Destructor
        {
            void* object;
            void (*destroy)(void*);
        };
        std::vector<Block>      m_blocks;
        std::vector<Destructor> m_destructors;
        size_t                  m_current       = 0;
        size_t                  m_used          = 0;
        size_t                  m_baseRemainder = 0;
    };

    // Typed slots features share within a frame (§3.3). Values are copied into the frame arena, so T
    // must be trivially copyable (handles and plain structs).
    class Blackboard
    {
    public:
        template <class T> void set(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>, "Blackboard values must be trivially copyable");
            for (Slot& s : m_slots)
            {
                if (s.key == key<T>())
                {
                    *static_cast<T*>(s.data) = value;
                    return;
                }
            }
            m_slots.push_back({ key<T>(), m_arena->create<T>(value) });
        }
        template <class T> const T* tryGet() const
        {
            for (const Slot& s : m_slots)
                if (s.key == key<T>())
                    return static_cast<const T*>(s.data);
            return nullptr;
        }
        template <class T> bool has() const
        {
            return tryGet<T>() != nullptr;
        }
        void reset()
        {
            m_slots.clear();
        }

    private:
        friend class RenderGraph;
        template <class T> struct Tag
        {
            static constexpr char id = 0;
        };
        template <class T> static const void* key()
        {
            return &Tag<T>::id;
        }
        struct Slot
        {
            const void* key;
            void*       data;
        };
        std::vector<Slot> m_slots;
        Arena*            m_arena = nullptr;
    };

    struct HistoryPair
    {
        TextureHandle prev; // invalid on first use, after a resize, invalidateHistory() or a skipped frame
        TextureHandle curr; // write it this frame; it becomes next frame's prev
    };

    struct CompileOptions
    {
        bool asyncCompute = false; // rg.asyncCompute: off = every pass forced onto Graphics (default until Phase 10)
        bool noCull       = false; // rg.noCull
        bool fullBarriers = false; // rg.fullBarriers: SYNC_ALL barrier at every access, no read merging
        bool noAliasing   = false; // rg.noAliasing
        bool validate     = false; // rg.validate: validateCompiled() after every compile, logs problems

        bool operator==(const CompileOptions&) const = default;
    };

    struct NoData
    {
    };

    class PassBuilder
    {
    public:
        TextureHandle create(const char* name, const TextureDesc& desc); // transient
        BufferHandle  create(const char* name, const BufferDesc& desc);  // transient
        TextureHandle read(TextureHandle h, Use use = Use::PixelSrv, uint16_t mip = kAllMips);
        TextureHandle write(TextureHandle h, Use use = Use::RenderTarget); // returns the new version
        TextureHandle readWrite(TextureHandle h, Use use = Use::Uav);      // returns the new version
        BufferHandle  read(BufferHandle h, Use use);
        BufferHandle  write(BufferHandle h, Use use);
        BufferHandle  readWrite(BufferHandle h, Use use = Use::Uav);
        void          queue(Queue q);
        void          flags(PassFlags f);
        void          scope(const char* gpuScopeName, uint32_t color);

    private:
        friend class RenderGraph;
        PassBuilder(RenderGraph& graph, uint32_t pass) :
            m_graph(graph),
            m_pass(pass)
        {
        }
        RenderGraph& m_graph;
        uint32_t     m_pass;
    };

    // Hooks the executor implements (Phase 4: the D3D12 one; tests: a recorder). Called in compiled
    // global order: prologue barriers, then per pass waits -> BeforePass barriers -> beginPass ->
    // execute lambda -> endPass -> AfterPass barriers -> signals, then epilogue waits and barriers.
    class Executor
    {
    public:
        virtual ~Executor() = default;
        virtual void barriers(const BarrierBatch& batch, const BarrierRec* records)
        {
            (void)batch, (void)records;
        }
        virtual void signal(Queue queue, uint32_t value)
        {
            (void)queue, (void)value;
        }
        virtual void wait(Queue waitQueue, Queue signalQueue, uint32_t value)
        {
            (void)waitQueue, (void)signalQueue, (void)value;
        }
        virtual void beginPass(const PassContext& ctx)
        {
            (void)ctx;
        }
        virtual void endPass(const PassContext& ctx)
        {
            (void)ctx;
        }
    };

    // Execute-time view of a pass (§3.3). Phase 4 adds the command list and descriptor helpers.
    class PassContext
    {
    public:
        uint32_t passIndex() const
        {
            return m_pass;
        }
        const char* passName() const;
        Queue       queue() const;
        const char* scopeName() const;
        uint32_t    scopeColor() const;
        uint64_t    frameIndex() const;
        Executor*   executor() const
        {
            return m_executor;
        }

        uint32_t           width(TextureHandle h) const;
        uint32_t           height(TextureHandle h) const;
        const TextureDesc* desc(TextureHandle h) const;
        void*              native(TextureHandle h) const; // imported resources only
        void*              native(BufferHandle h) const;
        uint32_t           aliasSlot(TextureHandle h) const; // transient memory slot (kNoPass when not aliased)
        uint32_t           aliasSlot(BufferHandle h) const;
        uint32_t           historyId(TextureHandle h) const; // persistent slot id of a history texture

    private:
        friend class RenderGraph;
        PassContext(const RenderGraph& graph, uint32_t pass, Executor* executor) :
            m_graph(graph),
            m_pass(pass),
            m_executor(executor)
        {
        }
        const RenderGraph& m_graph;
        uint32_t           m_pass;
        Executor*          m_executor;
    };

    // Compiled facts about one resource.
    struct ResourceInfo
    {
        const char*  name      = "";
        ResourceKind kind      = ResourceKind::Texture;
        Lifetime     lifetime  = Lifetime::Transient;
        uint32_t     width     = 0; // resolved (textures)
        uint32_t     height    = 0;
        uint64_t     bytes     = 0;       // estimate for textures, BufferDesc::size for buffers
        bool         used      = false;   // touched by a live pass
        uint32_t     firstPass = kNoPass; // live passes, by global order
        uint32_t     lastPass  = kNoPass;
        uint8_t      queueMask = 0;       // bit per Queue that touches it
        uint32_t     aliasSlot = kNoPass; // transients only
        uint16_t     versions  = 0;
        uint32_t     historyId = kNoPass; // persistent slot id (history textures)
    };

    struct AliasSlot
    {
        uint64_t              bytes = 0;
        std::vector<uint16_t> resources; // in order of use
    };

    class RenderGraph
    {
    public:
        RenderGraph();
        ~RenderGraph();
        RenderGraph(const RenderGraph&)            = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

        // Ends the previous frame (history swap if it executed) and starts a new one.
        void     beginFrame(uint32_t outputWidth, uint32_t outputHeight);
        uint64_t frameIndex() const
        {
            return m_frameIndex;
        }
        uint32_t outputWidth() const
        {
            return m_outputWidth;
        }
        uint32_t outputHeight() const
        {
            return m_outputHeight;
        }

        void setOptions(const CompileOptions& options)
        {
            m_options = options;
        }
        const CompileOptions& options() const
        {
            return m_options;
        }

        // Setup runs now; exec is stored in the frame arena and runs from execute().
        // Data is filled in setup and read in execute; it lives in the frame arena (trivially destructible).
        template <class Data, class Setup, class Exec> Data& addPass(const char* name, Setup&& setup, Exec&& exec)
        {
            static_assert(std::is_trivially_destructible_v<Data>, "pass Data lives in the frame arena and is never destroyed");
            static_assert(std::is_default_constructible_v<Data>, "pass Data must be default constructible");
            using ExecT          = std::decay_t<Exec>;
            const uint32_t index = beginPassRecord(name);
            Data*          data  = m_arena.create<Data>();
            ExecT*         fn    = m_arena.create<ExecT>(std::forward<Exec>(exec));
            PassBuilder    builder(*this, index);
            setup(builder, *data);
            setPassThunk(index, data, fn, [](void* f, const void* d, PassContext& ctx) { (*static_cast<ExecT*>(f))(ctx, *static_cast<const Data*>(d)); });
            return *data;
        }

        TextureHandle importTexture(const char* name, const TextureDesc& desc, void* native, Layout initial, Layout finalLayout);
        BufferHandle  importBuffer(const char* name, const BufferDesc& desc, void* native);
        // Imported every frame; starts and ends in PRESENT. Writing it makes the writer a sink.
        TextureHandle importBackBuffer(void* native, uint32_t width, uint32_t height, uint32_t dxgiFormat);

        HistoryPair history(const char* name, const TextureDesc& desc); // §3.6
        void        invalidateHistory();                                // camera cut / scene change; affects history() calls after it
        uint32_t    historyCount() const
        {
            return static_cast<uint32_t>(m_history.size());
        }

        Blackboard& blackboard()
        {
            return m_blackboard;
        }

        bool compile();
        bool execute(Executor* executor = nullptr);

        // --- results (valid after a successful compile) ---
        const std::vector<Error>& errors() const
        {
            return m_errors;
        }
        bool hasError(ErrorCode code) const;
        bool compiledOk() const
        {
            return m_compiledOk;
        }
        bool compiledFromCache() const
        {
            return m_fromCache;
        }
        uint64_t planKey() const
        {
            return m_planKey;
        }

        uint32_t passCount() const
        {
            return static_cast<uint32_t>(m_passes.size());
        }
        uint32_t    findPass(const char* name) const; // kNoPass if missing
        const char* passName(uint32_t pass) const;
        bool        passCulled(uint32_t pass) const;
        Queue       passQueue(uint32_t pass) const; // after forcing
        uint32_t    passOrder(uint32_t pass) const; // global position, kNoPass when culled

        const std::vector<uint32_t>& order() const
        {
            return m_order;
        }
        const std::vector<uint32_t>& queueOrder(Queue q) const
        {
            return m_queueOrder[static_cast<uint32_t>(q)];
        }
        const std::vector<SyncPoint>& syncPoints() const
        {
            return m_syncs;
        }
        const std::vector<BarrierRec>& barriers() const
        {
            return m_barriers;
        }
        const std::vector<BarrierBatch>& barrierBatches() const
        {
            return m_batches;
        }

        uint32_t resourceCount() const
        {
            return static_cast<uint32_t>(m_resources.size());
        }
        uint16_t                      findResource(const char* name) const; // kInvalidIndex if missing
        ResourceInfo                  resourceInfo(uint16_t resource) const;
        const std::vector<AliasSlot>& aliasSlots() const
        {
            return m_aliasSlots;
        }
        uint64_t transientBytesUnaliased() const
        {
            return m_transientBytesUnaliased;
        }
        uint64_t transientBytesAliased() const
        {
            return m_transientBytesAliased;
        }

        // Pass a's work is complete before pass b starts (same-queue order or a sync point chain).
        // kPrologue / kEpilogue name the graphics prologue/epilogue.
        bool happensBefore(uint32_t a, uint32_t b) const;

        // Re-checks a compiled plan: dependencies covered by the schedule, no wait without a matching
        // earlier signal (deadlock), every barrier valid on its queue, aliased transients disjoint in time.
        bool validateCompiled(std::string* why = nullptr) const;

        void dumpDot(std::string& out) const;
        void dumpJson(std::string& out) const;

    private:
        friend class PassBuilder;
        friend class PassContext;
        struct Impl;

        using ThunkFn = void (*)(void* exec, const void* data, PassContext& ctx);

        uint32_t beginPassRecord(const char* name);
        void     setPassThunk(uint32_t pass, const void* data, void* exec, ThunkFn fn);

        struct AccessRec
        {
            uint16_t resource = kInvalidIndex;
            uint16_t version  = 0; // read version, or the base version a write builds on
            Use      use      = Use::PixelSrv;
            bool     read     = false;
            bool     write    = false;
            uint16_t mip      = kAllMips;
        };
        struct PassNode
        {
            const char*            name          = "";
            PassFlags              flags         = PassFlags::None;
            Queue                  declaredQueue = Queue::Graphics;
            Queue                  queue         = Queue::Graphics;
            const char*            scopeName     = nullptr;
            uint32_t               scopeColor    = 0;
            std::vector<AccessRec> accesses;
            const void*            data  = nullptr;
            void*                  exec  = nullptr;
            ThunkFn                thunk = nullptr;
        };
        struct VersionNode
        {
            uint32_t              writer = kNoPass; // kExternalWriter for imported / valid history
            std::vector<uint32_t> readers;
        };
        struct ResourceNode
        {
            const char*              name     = "";
            ResourceKind             kind     = ResourceKind::Texture;
            Lifetime                 lifetime = Lifetime::Transient;
            TextureDesc              tex;
            BufferDesc               buf;
            uint32_t                 width = 0, height = 0;
            void*                    native      = nullptr;
            Layout                   initial     = Layout::Undefined;
            Layout                   finalLayout = Layout::Undefined;
            bool                     hasFinal    = false;
            uint32_t                 creator     = kNoPass;
            uint32_t                 historyId   = kNoPass;
            std::vector<VersionNode> versions;
        };
        // Compiled per pass / per resource; kept across frames so a cached plan is reused as is.
        struct CompiledPass
        {
            bool     live     = false;
            Queue    queue    = Queue::Graphics;
            uint32_t order    = kNoPass; // global position
            uint32_t queuePos = kNoPass; // position in its queue's item list (graphics: prologue is 0)
        };
        struct CompiledResource
        {
            bool     used                 = false;
            uint64_t bytes                = 0;
            uint32_t firstPass            = kNoPass;
            uint32_t lastPass             = kNoPass;
            uint8_t  queueMask            = 0;
            uint32_t aliasSlot            = kNoPass;
            uint32_t firstOn[kQueueCount] = { kNoPass, kNoPass }; // first / last pass per queue
            uint32_t lastOn[kQueueCount]  = { kNoPass, kNoPass };
        };
        struct HistoryEntry
        {
            std::string name;
            TextureDesc desc;
            uint32_t    width = 0, height = 0;
            uint32_t    ids[2]         = { kNoPass, kNoPass };
            bool        initialized[2] = { false, false };
            uint8_t     cur            = 0;
            bool        prevValid      = false;
            bool        requested      = false;
            uint16_t    prevRes        = kInvalidIndex;
            uint16_t    currRes        = kInvalidIndex;
        };

        void                addError(ErrorCode code, uint32_t pass, uint16_t resource, std::string message);
        void                logErrorsOnce();
        uint16_t            addResource(ResourceNode&& node, uint32_t pass);
        bool                checkHandle(uint32_t pass, uint16_t index, uint16_t version, ResourceKind kind, const char* what);
        uint16_t            recordRead(uint32_t pass, uint16_t index, uint16_t version, ResourceKind kind, Use use, uint16_t mip);
        uint16_t            recordWrite(uint32_t pass, uint16_t index, uint16_t version, ResourceKind kind, Use use, bool alsoRead);
        void                resolveSize(ResourceNode& node) const;
        uint64_t            computePlanKey() const;
        bool                resourceFollows(uint16_t earlier, uint16_t later) const;
        const ResourceNode* resourceAt(uint16_t index, ResourceKind kind) const;

        Arena                     m_arena;
        Blackboard                m_blackboard;
        CompileOptions            m_options;
        std::vector<PassNode>     m_passes;
        std::vector<ResourceNode> m_resources;
        std::vector<HistoryEntry> m_history;
        std::vector<Error>        m_errors;
        std::vector<uint64_t>     m_loggedErrors;
        uint64_t                  m_frameIndex    = 0;
        uint32_t                  m_outputWidth   = 0;
        uint32_t                  m_outputHeight  = 0;
        uint32_t                  m_nextHistoryId = 0;
        bool                      m_executed      = false;
        bool                      m_compiledOk    = false;
        bool                      m_fromCache     = false;
        uint64_t                  m_planKey       = 0;
        uint64_t                  m_lastGoodKey   = 0;
        bool                      m_haveLastGood  = false;

        // compiled plan
        std::vector<CompiledPass>                  m_cPass;
        std::vector<CompiledResource>              m_cRes;
        std::vector<uint32_t>                      m_order;
        std::vector<uint32_t>                      m_queueOrder[kQueueCount];
        std::vector<SyncPoint>                     m_syncs;
        std::vector<BarrierRec>                    m_barriers;
        std::vector<BarrierBatch>                  m_batches;
        std::vector<AliasSlot>                     m_aliasSlots;
        std::vector<std::pair<uint32_t, uint32_t>> m_edges; // live dependency edges (from, to), incl. barrier edges
        uint64_t                                   m_transientBytesUnaliased = 0;
        uint64_t                                   m_transientBytesAliased   = 0;
    };

} // namespace Dark::Rg
