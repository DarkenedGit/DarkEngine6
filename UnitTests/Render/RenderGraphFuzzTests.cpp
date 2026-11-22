#include "RenderGraphTestUtil.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Randomized graphs checked against an independent model (plan Phase 3 exit: "the compiled plan
// is legal"). Every seed builds a frame of random passes on both queues over imported, transient
// and back-buffer resources, compiles it, and then checks, without trusting the compiler:
//   - culling matches reachability from the sinks; order respects every data and WAR edge;
//   - replaying the barriers in execution order, every access finds its resource in a layout that
//     allows it, every barrier's before-layout is the resource's current layout, and imports end in
//     their final layout;
//   - every conflicting pair of touches on different queues is ordered by a sync point, and same-
//     queue write hazards have a barrier between them;
//   - aliased transients never overlap; the plan cache replays an identical plan.
// Cycles (from reads of old versions) must fail with ErrorCode::Cycle and nothing else.

using namespace Dark::Rg;
using namespace Dark::Rg::Test;

namespace
{
    struct Rng
    {
        uint64_t s;
        explicit Rng(uint64_t seed) :
            s(seed * 0x9E3779B97F4A7C15ull + 1)
        {
        }
        uint32_t next()
        {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
            return static_cast<uint32_t>(s >> 11);
        }
        uint32_t below(uint32_t n)
        {
            return n ? next() % n : 0;
        }
        bool chance(uint32_t percent)
        {
            return below(100) < percent;
        }
    };

    enum class Op : uint8_t
    {
        Read,
        Write,
        ReadWrite,
        Create
    };

    struct AccessSpec
    {
        uint32_t     res     = 0; // model resource
        uint16_t     version = 0; // version read / overwritten
        Op           op      = Op::Read;
        Use          use     = Use::PixelSrv;
        ResourceKind kind    = ResourceKind::Texture;
    };

    struct PassSpec
    {
        Queue                   queue = Queue::Graphics;
        PassFlags               flags = PassFlags::None;
        std::vector<AccessSpec> accesses;
    };

    struct ResSpec
    {
        ResourceKind kind     = ResourceKind::Texture;
        bool         imported = false;
        bool         back     = false;
        bool         history  = false; // a history pair's curr (first frame: UNDEFINED -> SHADER_RESOURCE)
        Layout       initial  = Layout::Undefined;
        Layout       final    = Layout::Undefined;
        TextureDesc  tdesc;
        BufferDesc   bdesc;
        std::string  name;
    };

    struct Frame
    {
        CompileOptions        options;
        uint32_t              width = 0, height = 0;
        std::vector<ResSpec>  resources; // imports first, then transients in creation order
        std::vector<PassSpec> passes;
    };

    const Use    kTexReads[]  = { Use::PixelSrv, Use::ComputeSrv, Use::AnySrv, Use::DepthRead, Use::CopySrc };
    const Use    kTexWrites[] = { Use::RenderTarget, Use::DepthWrite, Use::Uav, Use::CopyDst };
    const Use    kBufReads[]  = { Use::PixelSrv, Use::ComputeSrv, Use::AnySrv, Use::CopySrc, Use::IndirectArgs, Use::VertexIndex };
    const Use    kBufWrites[] = { Use::Uav, Use::CopyDst };
    const Layout kLayouts[]   = { Layout::Common,           Layout::GenericRead,    Layout::RenderTarget, Layout::UnorderedAccess, Layout::DepthStencilWrite,
                                  Layout::DepthStencilRead, Layout::ShaderResource, Layout::CopySource,   Layout::CopyDest,
                                  Layout::DirectQueueGenericRead, Layout::DirectQueueGenericReadComputeQueueAccessible };

    template <size_t N> Use pickUse(Rng& rng, const Use (&uses)[N], Queue q)
    {
        for (int tries = 0; tries < 16; ++tries)
        {
            const Use u = uses[rng.below(N)];
            if (useLegalOnQueue(u, q))
                return u;
        }
        for (const Use u : uses)
            if (useLegalOnQueue(u, q))
                return u;
        return uses[0];
    }

    Frame generate(uint64_t seed)
    {
        Rng   rng(seed);
        Frame f;
        f.options.asyncCompute = rng.chance(70);
        f.options.fullBarriers = rng.chance(20);
        f.options.noAliasing   = rng.chance(20);
        f.options.noCull       = rng.chance(10);
        f.width                = 64 + rng.below(2000);
        f.height               = 64 + rng.below(1200);

        // Model state per resource: number of versions and whether v0 has contents.
        std::vector<uint16_t> versions;
        std::vector<bool>     v0Written;
        const auto            addRes = [&](ResSpec r, bool hasV0)
        {
            r.name = (r.kind == ResourceKind::Texture ? "t" : "b") + std::to_string(f.resources.size());
            f.resources.push_back(r);
            versions.push_back(1);
            v0Written.push_back(hasV0);
        };

        ResSpec back;
        back.kind     = ResourceKind::Texture;
        back.imported = back.back = true;
        back.initial = back.final = Layout::Present;
        addRes(back, true);
        const uint32_t imports = rng.below(4);
        for (uint32_t i = 0; i < imports; ++i)
        {
            ResSpec r;
            r.imported = true;
            if (rng.chance(70))
            {
                r.kind    = ResourceKind::Texture;
                r.tdesc   = absTex(16 + rng.below(512), 16 + rng.below(512), kRgba16F);
                r.initial = kLayouts[rng.below(9)];
                r.final   = rng.chance(20) ? Layout::Undefined : kLayouts[rng.below(9)];
            }
            else
            {
                r.kind  = ResourceKind::Buffer;
                r.bdesc = buf(64 + rng.below(1 << 16));
            }
            addRes(r, true);
        }

        if (rng.chance(40))
        {
            ResSpec h;
            h.kind    = ResourceKind::Texture;
            h.history = true;
            h.tdesc   = tex(kRgba16F, rng.chance(50) ? 1.0f : 0.5f);
            h.initial = Layout::Undefined;
            h.final   = Layout::ShaderResource;
            addRes(h, false);
        }

        const uint32_t passCount = 2 + rng.below(28);
        const uint32_t formats[] = { kRgba16F, kRgba8, kR8, kR32F, kD32, kR11G11B10 };
        for (uint32_t p = 0; p < passCount; ++p)
        {
            PassSpec ps;
            ps.queue = rng.chance(35) ? Queue::AsyncCompute : Queue::Graphics;
            if (rng.chance(12))
                ps.flags = PassFlags::SideEffect;
            std::vector<uint32_t> touched;
            const uint32_t        n = 1 + rng.below(4);
            for (uint32_t a = 0; a < n; ++a)
            {
                AccessSpec as;
                const bool create = f.resources.empty() || rng.chance(30);
                if (create)
                {
                    ResSpec r;
                    r.kind = rng.chance(75) ? ResourceKind::Texture : ResourceKind::Buffer;
                    if (r.kind == ResourceKind::Texture)
                    {
                        r.tdesc = rng.chance(80) ? tex(formats[rng.below(6)], rng.chance(50) ? 1.0f : 0.5f) : absTex(1 + rng.below(256), 1 + rng.below(256), formats[rng.below(6)]);
                        if (rng.chance(10))
                            r.tdesc.mips = static_cast<uint16_t>(1 + rng.below(5));
                    }
                    else
                    {
                        r.bdesc = buf(16 + rng.below(1 << 20), rng.chance(50) ? 16 : 0);
                    }
                    addRes(r, false);
                    as.res     = static_cast<uint32_t>(f.resources.size() - 1);
                    as.kind    = r.kind;
                    as.op      = Op::Create;
                    as.version = 0;
                    as.use     = r.kind == ResourceKind::Texture ? pickUse(rng, kTexWrites, ps.queue) : pickUse(rng, kBufWrites, ps.queue);
                    ps.accesses.push_back(as);
                    touched.push_back(as.res);
                    ++versions[as.res];
                    continue;
                }
                const uint32_t res = rng.below(static_cast<uint32_t>(f.resources.size()));
                if (std::find(touched.begin(), touched.end(), res) != touched.end())
                    continue; // one access per resource per pass keeps the model simple
                as.res                    = res;
                as.kind                   = f.resources[res].kind;
                const uint16_t latest     = static_cast<uint16_t>(versions[res] - 1);
                const bool     hasContent = latest > 0 || v0Written[res];
                if (!hasContent || rng.chance(45))
                {
                    as.op      = hasContent && rng.chance(30) ? Op::ReadWrite : Op::Write;
                    as.version = latest;
                    as.use     = as.op == Op::ReadWrite ? Use::Uav : (as.kind == ResourceKind::Texture ? pickUse(rng, kTexWrites, ps.queue) : pickUse(rng, kBufWrites, ps.queue));
                    if (as.op == Op::ReadWrite && !useLegalOnQueue(Use::Uav, ps.queue))
                        continue;
                    ++versions[res];
                }
                else
                {
                    as.op = Op::Read;
                    // Mostly the latest version; sometimes an older one (exercises WAR edges and cycles).
                    as.version = latest;
                    if (latest > 0 && rng.chance(15))
                    {
                        const uint16_t lowest = v0Written[res] ? 0 : 1;
                        as.version            = static_cast<uint16_t>(lowest + rng.below(latest - lowest + 1));
                    }
                    as.use = as.kind == ResourceKind::Texture ? pickUse(rng, kTexReads, ps.queue) : pickUse(rng, kBufReads, ps.queue);
                }
                ps.accesses.push_back(as);
                touched.push_back(res);
            }
            f.passes.push_back(std::move(ps));
        }
        return f;
    }

    // Builds the frame into the graph; returns false when a builder call unexpectedly failed.
    bool build(RenderGraph& g, const Frame& f)
    {
        g.setOptions(f.options);
        g.beginFrame(f.width, f.height);
        g.invalidateHistory(); // replays see a first-use history again (same structure, same plan)
        std::vector<std::vector<TextureHandle>> tv(f.resources.size());
        std::vector<std::vector<BufferHandle>>  bv(f.resources.size());
        for (size_t r = 0; r < f.resources.size(); ++r)
        {
            const ResSpec& rs = f.resources[r];
            if (rs.history)
            {
                tv[r].push_back(g.history(rs.name.c_str(), rs.tdesc).curr);
                continue;
            }
            if (!rs.imported)
                continue;
            if (rs.back)
                tv[r].push_back(g.importBackBuffer(nullptr, f.width, f.height, kBgra8));
            else if (rs.kind == ResourceKind::Texture)
                tv[r].push_back(g.importTexture(rs.name.c_str(), rs.tdesc, nullptr, rs.initial, rs.final));
            else
                bv[r].push_back(g.importBuffer(rs.name.c_str(), rs.bdesc, nullptr));
        }
        bool ok = true;
        for (size_t p = 0; p < f.passes.size(); ++p)
        {
            const PassSpec&   ps   = f.passes[p];
            const std::string name = "p" + std::to_string(p);
            pass(g, name.c_str(),
                 [&](PassBuilder& b)
                 {
                     b.queue(ps.queue);
                     b.flags(ps.flags);
                     for (const AccessSpec& a : ps.accesses)
                     {
                         const ResSpec& rs = f.resources[a.res];
                         if (a.kind == ResourceKind::Texture)
                         {
                             if (a.op == Op::Create)
                                 tv[a.res].push_back(b.create(rs.name.c_str(), rs.tdesc));
                             const TextureHandle h = tv[a.res][a.version];
                             if (a.op == Op::Read)
                                 ok &= b.read(h, a.use).valid();
                             else
                             {
                                 const TextureHandle n = a.op == Op::ReadWrite ? b.readWrite(h, a.use) : b.write(h, a.use);
                                 ok &= n.valid();
                                 tv[a.res].push_back(n);
                             }
                         }
                         else
                         {
                             if (a.op == Op::Create)
                                 bv[a.res].push_back(b.create(rs.name.c_str(), rs.bdesc));
                             const BufferHandle h = bv[a.res][a.version];
                             if (a.op == Op::Read)
                                 ok &= b.read(h, a.use).valid();
                             else
                             {
                                 const BufferHandle n = a.op == Op::ReadWrite ? b.readWrite(h, a.use) : b.write(h, a.use);
                                 ok &= n.valid();
                                 bv[a.res].push_back(n);
                             }
                         }
                     }
                 });
        }
        return ok;
    }

    // ---- independent checks -----------------------------------------------------------------

    struct Model
    {
        // writer[res][version] = pass (kPrologue for imported v0, kNoPass for unwritten transient v0)
        std::vector<std::vector<uint32_t>>              writer;
        std::vector<std::vector<std::vector<uint32_t>>> readers;
        std::vector<std::vector<uint32_t>>              deps; // per pass: passes it needs (data + overwrite)
    };

    Model buildModel(const Frame& f)
    {
        Model m;
        m.writer.resize(f.resources.size());
        m.readers.resize(f.resources.size());
        for (size_t r = 0; r < f.resources.size(); ++r)
        {
            m.writer[r].push_back(f.resources[r].imported ? kPrologue : kNoPass);
            m.readers[r].emplace_back();
        }
        m.deps.resize(f.passes.size());
        for (uint32_t p = 0; p < f.passes.size(); ++p)
            for (const AccessSpec& a : f.passes[p].accesses)
            {
                const uint32_t w = m.writer[a.res][a.version];
                if (w < f.passes.size())
                    m.deps[p].push_back(w);
                if (a.op == Op::Read)
                    m.readers[a.res][a.version].push_back(p);
                else
                {
                    m.writer[a.res].push_back(p);
                    m.readers[a.res].emplace_back();
                }
            }
        return m;
    }

    std::vector<bool> expectedLive(const Frame& f, const Model& m)
    {
        std::vector<bool>     live(f.passes.size(), false);
        std::vector<uint32_t> stack;
        for (uint32_t p = 0; p < f.passes.size(); ++p)
        {
            bool root = f.options.noCull || hasFlag(f.passes[p].flags, PassFlags::SideEffect);
            for (const AccessSpec& a : f.passes[p].accesses)
                root = root || (a.op != Op::Read && (f.resources[a.res].imported || f.resources[a.res].history));
            if (root)
            {
                live[p] = true;
                stack.push_back(p);
            }
        }
        while (!stack.empty())
        {
            const uint32_t p = stack.back();
            stack.pop_back();
            for (const uint32_t d : m.deps[p])
                if (!live[d])
                {
                    live[d] = true;
                    stack.push_back(d);
                }
        }
        return live;
    }

    struct Touch
    {
        uint32_t item    = kNoPass; // pass, kPrologue or kEpilogue
        Queue    queue   = Queue::Graphics;
        bool     barrier = false;
        bool     write   = false;
    };

    ::testing::AssertionResult checkPlan(const RenderGraph& g, const Frame& f, const Model& m)
    {
        const auto              fail = [](const std::string& s) { return ::testing::AssertionFailure() << s; };
        const std::vector<bool> live = expectedLive(f, m);
        for (uint32_t p = 0; p < f.passes.size(); ++p)
            if (g.passCulled(p) == live[p])
                return fail("pass p" + std::to_string(p) + (live[p] ? " culled but reaches a sink" : " kept but reaches no sink"));

        // Order: data, overwrite and WAR edges.
        for (uint32_t p = 0; p < f.passes.size(); ++p)
        {
            if (!live[p])
                continue;
            for (const uint32_t d : m.deps[p])
                if (g.passOrder(d) >= g.passOrder(p))
                    return fail("p" + std::to_string(p) + " runs before its producer p" + std::to_string(d));
        }
        for (size_t r = 0; r < f.resources.size(); ++r)
            for (size_t v = 0; v + 1 < m.writer[r].size(); ++v)
            {
                const uint32_t over = m.writer[r][v + 1];
                if (!live[over])
                    continue;
                for (const uint32_t rd : m.readers[r][v])
                    if (live[rd] && rd != over && g.passOrder(rd) >= g.passOrder(over))
                        return fail("reader p" + std::to_string(rd) + " of an old version runs after the overwrite p" + std::to_string(over));
            }

        // Queue assignment.
        for (uint32_t p = 0; p < f.passes.size(); ++p)
        {
            if (!live[p])
                continue;
            const Queue want = f.options.asyncCompute ? f.passes[p].queue : Queue::Graphics;
            if (g.passQueue(p) != want)
                return fail("p" + std::to_string(p) + " on the wrong queue");
        }
        if (!f.options.asyncCompute && !g.syncPoints().empty())
            return fail("sync points with async compute off");

        // Replay barriers and accesses in execution order.
        const size_t                    nRes = f.resources.size();
        std::vector<Layout>             layout(nRes, Layout::Undefined);
        std::vector<std::vector<Touch>> touches(nRes);
        for (size_t r = 0; r < nRes; ++r)
            if (f.resources[r].imported || f.resources[r].history)
                layout[r] = f.resources[r].initial;
        // Map model resources to graph indices by name (imports keep their names; the back buffer is "backbuffer").
        std::vector<uint16_t> gi(nRes);
        for (size_t r = 0; r < nRes; ++r)
            gi[r] = g.findResource(f.resources[r].back ? "backbuffer" : f.resources[r].history ? (f.resources[r].name + ".curr").c_str() : f.resources[r].name.c_str());
        std::vector<int> modelOf(g.resourceCount(), -1);
        for (size_t r = 0; r < nRes; ++r)
        {
            if (gi[r] == kInvalidIndex)
                return fail("resource " + f.resources[r].name + " missing");
            modelOf[gi[r]] = static_cast<int>(r);
        }

        const std::vector<BarrierBatch>& batches = g.barrierBatches();
        size_t                           bi      = 0;
        std::string                      err;
        const auto                       runBatches = [&](BarrierPlacement placement, uint32_t item) -> bool
        {
            while (bi < batches.size() && batches[bi].placement == placement && batches[bi].pass == item)
            {
                for (uint32_t k = 0; k < batches[bi].count; ++k)
                {
                    const BarrierRec& b = g.barriers()[batches[bi].first + k];
                    const int         r = modelOf[b.resource];
                    if (r < 0)
                        return err = "barrier on an unknown resource", false;
                    if (b.kind == ResourceKind::Texture)
                    {
                        const bool okBefore = b.before.layout == layout[r] || (b.discard && b.before.layout == Layout::Undefined);
                        if (!okBefore)
                            return err = "barrier on " + f.resources[r].name + " expects " + layoutName(b.before.layout) + " but it is " + layoutName(layout[r]), false;
                        layout[r] = b.after.layout;
                    }
                    // A layout change conflicts with any use on the other queue; a sync-only barrier
                    // (buffers, same layout) only orders its own queue.
                    const bool changesLayout = b.kind == ResourceKind::Texture && (b.before.layout != b.after.layout || b.discard);
                    touches[r].push_back({ item, b.queue, true, changesLayout });
                }
                ++bi;
            }
            return true;
        };

        if (!runBatches(BarrierPlacement::Prologue, kPrologue))
            return fail(err);
        for (const uint32_t p : g.order())
        {
            if (!runBatches(BarrierPlacement::BeforePass, p))
                return fail(err);
            const Queue q = g.passQueue(p);
            for (const AccessSpec& a : f.passes[p].accesses)
            {
                if (!useLegalOnQueue(a.use, q))
                    return fail("illegal use on queue survived compile");
                const UseState s = useState(a.use, a.kind, q);
                if (a.kind == ResourceKind::Texture)
                {
                    const Layout l = layout[a.res];
                    if (!accessAllowedInLayout(s.access, l) || !layoutLegalOnQueue(l, q))
                        return fail("p" + std::to_string(p) + " uses " + f.resources[a.res].name + " as " + useName(a.use) + " but it is in " + layoutName(l));
                }
                touches[a.res].push_back({ p, q, false, a.op != Op::Read });
            }
            if (!runBatches(BarrierPlacement::AfterPass, p))
                return fail(err);
        }
        if (!runBatches(BarrierPlacement::Epilogue, kEpilogue))
            return fail(err);
        if (bi != batches.size())
            return fail("barrier batches out of execution order");
        for (size_t r = 0; r < nRes; ++r)
        {
            const ResSpec& rs = f.resources[r];
            // An unwritten history curr stays UNDEFINED (next frame has no prev); everything else ends in its final layout.
            const bool unwrittenHistory = rs.history && touches[r].empty();
            if ((rs.imported || rs.history) && !unwrittenHistory && rs.kind == ResourceKind::Texture && rs.final != Layout::Undefined && layout[r] != rs.final)
                return fail(rs.name + " ends in " + layoutName(layout[r]) + ", not its final " + layoutName(rs.final));
        }

        // Cross-queue ordering and same-queue hazards.
        for (size_t r = 0; r < nRes; ++r)
        {
            const std::vector<Touch>& t = touches[r];
            for (size_t i = 1; i < t.size(); ++i)
            {
                const Touch& a = t[i - 1];
                const Touch& b = t[i];
                if (a.queue != b.queue && (a.write || b.write) && a.item != b.item && !g.happensBefore(a.item, b.item))
                    return fail(f.resources[r].name + ": touches on two queues with no sync point between items " + std::to_string(a.item) + " and " + std::to_string(b.item));
            }
            // Same queue: a write and a later access (or an access and a later write) need a barrier in between.
            for (size_t i = 0; i < t.size(); ++i)
            {
                if (t[i].barrier)
                    continue;
                bool roundTrip = false; // a fence out to the other queue and back also flushes
                for (size_t j = i + 1; j < t.size(); ++j)
                {
                    if (t[j].barrier)
                        break; // a barrier separates them
                    if (t[j].queue != t[i].queue)
                    {
                        roundTrip = roundTrip || g.happensBefore(t[i].item, t[j].item);
                        continue;
                    }
                    if (roundTrip)
                    {
                        // ...provided the way back is ordered too.
                        bool back = false;
                        for (size_t k = i + 1; k < j; ++k)
                            back = back || (t[k].queue != t[i].queue && g.happensBefore(t[i].item, t[k].item) && g.happensBefore(t[k].item, t[j].item));
                        if (back)
                            break;
                    }
                    if ((t[i].write || t[j].write) && t[i].item != t[j].item)
                        return fail(f.resources[r].name + ": same-queue hazard with no barrier between p" + std::to_string(t[i].item) + " and p" + std::to_string(t[j].item));
                    break;
                }
            }
        }

        // Aliasing: occupants of one slot never overlap.
        for (const AliasSlot& slot : g.aliasSlots())
            for (size_t i = 0; i < slot.resources.size(); ++i)
                for (size_t j = i + 1; j < slot.resources.size(); ++j)
                {
                    const int a = modelOf[slot.resources[i]], b = modelOf[slot.resources[j]];
                    if (a < 0 || b < 0)
                        return fail("alias slot holds an unknown resource");
                    for (const Touch& ta : touches[a])
                        for (const Touch& tb : touches[b])
                        {
                            const bool ordered = ta.item == tb.item ? false : g.happensBefore(ta.item, tb.item);
                            if (!ordered)
                                return fail("aliased " + f.resources[a].name + " and " + f.resources[b].name + " overlap");
                        }
                }
        return ::testing::AssertionSuccess();
    }

    // Readable recipe of one frame, printed with a failing seed.
    std::string describe(const RenderGraph& g, const Frame& f)
    {
        std::string s = std::string("async=") + (f.options.asyncCompute ? "1" : "0") + " full=" + (f.options.fullBarriers ? "1" : "0") + " noAlias=" + (f.options.noAliasing ? "1" : "0") +
                        " noCull=" + (f.options.noCull ? "1" : "0") + "\n";
        for (const ResSpec& r : f.resources)
            if (r.imported)
                s += "  import " + (r.back ? std::string("backbuffer") : r.name) + " " + layoutName(r.initial) + "->" + layoutName(r.final) + "\n";
        for (size_t p = 0; p < f.passes.size(); ++p)
        {
            s += "  p" + std::to_string(p) + (f.passes[p].queue == Queue::AsyncCompute ? " [C]" : " [G]") + (g.passCulled(static_cast<uint32_t>(p)) ? " culled" : "") +
                 " order=" + std::to_string(g.passOrder(static_cast<uint32_t>(p))) + ":";
            for (const AccessSpec& a : f.passes[p].accesses)
            {
                const char* op = a.op == Op::Read ? "read" : a.op == Op::Write ? "write" : a.op == Op::ReadWrite ? "rw" : "create";
                s += std::string(" ") + op + " " + f.resources[a.res].name + "v" + std::to_string(a.version) + " " + useName(a.use) + ";";
            }
            s += "\n";
        }
        for (const SyncPoint& sp : g.syncPoints())
            s += "  sync " + std::string(queueName(sp.signalQueue)) + " after " + std::to_string(sp.afterPass) + " -> " + queueName(sp.waitQueue) + " before " + std::to_string(sp.beforePass) + " v" +
                 std::to_string(sp.signalValue) + "\n";
        for (const BarrierRec& b : g.barriers())
            s += "  barrier " + std::string(g.resourceInfo(b.resource).name) + " " + queueName(b.queue) + " " + placementText(b.placement) + " " + std::to_string(b.pass) + " " +
                 layoutName(b.before.layout) + "->" + layoutName(b.after.layout) + " sync " + std::to_string(static_cast<uint32_t>(b.before.sync)) + "->" +
                 std::to_string(static_cast<uint32_t>(b.after.sync)) + "\n";
        return s;
    }

    // Build with -DRG_FUZZ_SEEDS=<n> for a longer local soak; the default keeps the suite fast.
#ifndef RG_FUZZ_SEEDS
#define RG_FUZZ_SEEDS 600
#endif
    constexpr uint32_t fuzzSeeds()
    {
        return RG_FUZZ_SEEDS;
    }
} // namespace

TEST(RenderGraphFuzz, RandomGraphsCompileToLegalPlans)
{
    uint32_t compiled = 0, cycles = 0, asyncFrames = 0, crossQueueSyncs = 0, aliased = 0;
    for (uint64_t seed = 1; seed <= fuzzSeeds(); ++seed)
    {
        SCOPED_TRACE("seed " + std::to_string(seed));
        const Frame f = generate(seed);
        RenderGraph g;
        ASSERT_TRUE(build(g, f)) << errorText(g);
        if (!g.compile())
        {
            for (const Error& e : g.errors())
            {
                ASSERT_EQ(e.code, ErrorCode::Cycle) << errorText(g);
                ASSERT_EQ(e.message.find("(unresolved)"), std::string::npos) << e.message;
            }
            ++cycles;
            continue;
        }
        ++compiled;
        std::string why;
        ASSERT_TRUE(g.validateCompiled(&why)) << why;
        const Model model = buildModel(f);
        ASSERT_TRUE(checkPlan(g, f, model)) << describe(g, f);

        asyncFrames += g.queueOrder(Queue::AsyncCompute).empty() ? 0u : 1u;
        crossQueueSyncs += static_cast<uint32_t>(g.syncPoints().size());
        for (const BarrierRec& b : g.barriers())
            aliased += b.aliased ? 1u : 0u;
        for (const BarrierRec& b : g.barriers())
        {
            ASSERT_TRUE(validateBarrier(b, &why)) << why;
        }

        // Execute: each live pass runs once, in order, and each signal value is signalled once.
        Recorder rec(g);
        ASSERT_TRUE(g.execute(&rec));
        uint32_t ran = 0;
        for (const std::string& e : rec.events)
            ran += e.rfind("P ", 0) == 0 ? 1u : 0u;
        ASSERT_EQ(ran, g.order().size());

        // Same frame again: served from the plan cache, identical plan.
        const std::vector<BarrierRec> barriers = g.barriers();
        const std::vector<SyncPoint>  syncs    = g.syncPoints();
        ASSERT_TRUE(build(g, f));
        ASSERT_TRUE(g.compile());
        ASSERT_TRUE(g.compiledFromCache());
        ASSERT_EQ(g.barriers().size(), barriers.size());
        ASSERT_EQ(g.syncPoints().size(), syncs.size());
        for (size_t i = 0; i < syncs.size(); ++i)
        {
            ASSERT_EQ(g.syncPoints()[i].afterPass, syncs[i].afterPass);
            ASSERT_EQ(g.syncPoints()[i].beforePass, syncs[i].beforePass);
            ASSERT_EQ(g.syncPoints()[i].signalValue, syncs[i].signalValue);
        }
        ASSERT_TRUE(checkPlan(g, f, model));
    }
    // The generator must actually exercise the interesting paths.
    EXPECT_GT(compiled, fuzzSeeds() / 2);
    EXPECT_GT(asyncFrames, fuzzSeeds() / 4);
    EXPECT_GT(crossQueueSyncs, fuzzSeeds());
    EXPECT_GT(aliased, 50u);
    EXPECT_GT(cycles, 0u);
    std::printf("[ fuzz     ] %u compiled, %u cycles, %u async frames, %u syncs, %u alias barriers\n", compiled, cycles, asyncFrames, crossQueueSyncs, aliased);
}
