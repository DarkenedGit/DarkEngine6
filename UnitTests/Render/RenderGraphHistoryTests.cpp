#include "RenderGraphTestUtil.h"

#include <algorithm>
#include <string>
#include <vector>

// History pairs (plan §3.6) and transient alias planning (plan §3.9 data, computed in Phase 3).

using namespace Dark::Rg;
using namespace Dark::Rg::Test;

namespace
{
    // One TAA-style frame: resolve writes curr (reading prev when valid) and the back buffer copy reads it.
    HistoryPair taaFrame(RenderGraph& g, uint32_t w = 640, uint32_t h = 360, bool execute = true, TextureDesc desc = tex())
    {
        g.beginFrame(w, h);
        const TextureHandle bb   = g.importBackBuffer(nullptr, w, h, kBgra8);
        const HistoryPair   pair = g.history("TAA", desc);
        TextureHandle       curr;
        pass(g, "Resolve",
             [&](PassBuilder& b)
             {
                 if (pair.prev.valid())
                     b.read(pair.prev, Use::PixelSrv);
                 curr = b.write(pair.curr, Use::RenderTarget);
             });
        pass(g, "Present",
             [&](PassBuilder& b)
             {
                 b.read(curr, Use::CopySrc);
                 b.write(bb, Use::CopyDst);
             });
        EXPECT_TRUE(compiles(g));
        if (execute)
        {
            EXPECT_TRUE(g.execute());
        }
        return pair;
    }

    uint32_t idOf(const RenderGraph& g, TextureHandle h)
    {
        return h.valid() ? g.resourceInfo(h.index).historyId : kNoPass;
    }
} // namespace

// ---------------------------------------------------------------- history

TEST(RenderGraphHistory, PrevIsInvalidOnFirstUseThenSwaps)
{
    RenderGraph       g;
    const HistoryPair f1 = taaFrame(g);
    EXPECT_FALSE(f1.prev.valid());
    ASSERT_TRUE(f1.curr.valid());
    const uint32_t id1 = idOf(g, f1.curr);
    EXPECT_EQ(g.resourceInfo(f1.curr.index).lifetime, Lifetime::History);
    EXPECT_STREQ(g.resourceInfo(f1.curr.index).name, "TAA.curr");

    const HistoryPair f2 = taaFrame(g);
    ASSERT_TRUE(f2.prev.valid());
    EXPECT_STREQ(g.resourceInfo(f2.prev.index).name, "TAA.prev");
    EXPECT_EQ(idOf(g, f2.prev), id1); // last frame's curr is this frame's prev
    const uint32_t id2 = idOf(g, f2.curr);
    EXPECT_NE(id2, id1);

    const HistoryPair f3 = taaFrame(g);
    EXPECT_EQ(idOf(g, f3.prev), id2);
    EXPECT_EQ(idOf(g, f3.curr), id1); // ping-pong
    EXPECT_EQ(g.historyCount(), 1u);
}

TEST(RenderGraphHistory, PassContextSeesTheHistoryId)
{
    RenderGraph g;
    taaFrame(g);
    g.beginFrame(64, 64);
    const HistoryPair pair = g.history("TAA", tex());
    uint32_t          seen = kNoPass;
    struct Data
    {
        TextureHandle curr;
    };
    g.addPass<Data>("Resolve", [&](PassBuilder& b, Data& d) { d.curr = b.write(pair.curr); }, [&](PassContext& ctx, const Data& d) { seen = ctx.historyId(d.curr); });
    ASSERT_TRUE(compiles(g));
    ASSERT_TRUE(g.execute());
    EXPECT_EQ(seen, idOf(g, pair.curr));
    EXPECT_NE(seen, kNoPass);
}

TEST(RenderGraphHistory, ReadingCurrBeforeWritingItIsAnError)
{
    RenderGraph g;
    g.beginFrame(64, 64);
    const HistoryPair pair = g.history("TAA", tex());
    pass(g, "Bad",
         [&](PassBuilder& b)
         {
             b.read(pair.curr);
             b.flags(PassFlags::SideEffect);
         });
    EXPECT_FALSE(g.compile());
    ASSERT_TRUE(g.hasError(ErrorCode::ReadBeforeWrite));
    EXPECT_NE(g.errors()[0].message.find("read prev instead"), std::string::npos) << g.errors()[0].message;
}

TEST(RenderGraphHistory, AFrameThatDidNotExecuteLeavesNoPrev)
{
    RenderGraph g;
    taaFrame(g);
    taaFrame(g, 640, 360, /*execute*/ false); // compiled, never executed (device lost, minimized...)
    const HistoryPair f3 = taaFrame(g);
    EXPECT_FALSE(f3.prev.valid());
}

TEST(RenderGraphHistory, AFrameThatDidNotWriteCurrLeavesNoPrev)
{
    RenderGraph g;
    taaFrame(g);
    g.beginFrame(640, 360);
    g.history("TAA", tex()); // requested, not written
    ASSERT_TRUE(compiles(g));
    ASSERT_TRUE(g.execute());
    const HistoryPair f3 = taaFrame(g);
    EXPECT_FALSE(f3.prev.valid());
    const HistoryPair f4 = taaFrame(g);
    EXPECT_TRUE(f4.prev.valid());
}

TEST(RenderGraphHistory, InvalidateHistoryDropsPrevForOneFrame)
{
    RenderGraph g;
    taaFrame(g);
    g.beginFrame(640, 360);
    g.invalidateHistory(); // camera cut
    const HistoryPair cut = g.history("TAA", tex());
    EXPECT_FALSE(cut.prev.valid());
    TextureHandle curr;
    pass(g, "Resolve", [&](PassBuilder& b) { curr = b.write(cut.curr); });
    ASSERT_TRUE(compiles(g));
    ASSERT_TRUE(g.execute());
    const HistoryPair next = taaFrame(g);
    EXPECT_TRUE(next.prev.valid());
}

TEST(RenderGraphHistory, ResizeInvalidatesAndReallocates)
{
    RenderGraph       g;
    const HistoryPair f1  = taaFrame(g, 640, 360);
    const uint32_t    id1 = idOf(g, f1.curr);
    taaFrame(g, 640, 360);
    const HistoryPair r = taaFrame(g, 800, 600);
    EXPECT_FALSE(r.prev.valid());
    EXPECT_GT(idOf(g, r.curr), id1 + 1); // new allocation pair
    EXPECT_EQ(g.resourceInfo(r.curr.index).width, 800u);
    const HistoryPair after = taaFrame(g, 800, 600);
    EXPECT_TRUE(after.prev.valid());
}

TEST(RenderGraphHistory, AbsoluteHistoryKeepsItsSlotsAcrossAResizeButDropsPrev)
{
    RenderGraph       g;
    const TextureDesc fixed = absTex(64, 64, kR32F);
    const HistoryPair f1    = taaFrame(g, 640, 360, true, fixed);
    const uint32_t    id1   = idOf(g, f1.curr);
    const HistoryPair f2    = taaFrame(g, 1280, 720, true, fixed);
    EXPECT_FALSE(f2.prev.valid());        // any output resize invalidates prev (§3.6)
    EXPECT_LE(idOf(g, f2.curr), id1 + 1); // same pair of slots
}

TEST(RenderGraphHistory, DescChangeReallocates)
{
    RenderGraph       g;
    const HistoryPair f1  = taaFrame(g, 640, 360, true, tex(kRgba16F));
    const uint32_t    id1 = idOf(g, f1.curr);
    taaFrame(g, 640, 360, true, tex(kRgba16F));
    const HistoryPair f3 = taaFrame(g, 640, 360, true, tex(kR11G11B10));
    EXPECT_FALSE(f3.prev.valid());
    EXPECT_GT(idOf(g, f3.curr), id1 + 1);
}

TEST(RenderGraphHistory, UnrequestedHistoryIsEvicted)
{
    RenderGraph g;
    g.beginFrame(64, 64);
    g.history("A", tex());
    g.history("B", tex());
    EXPECT_EQ(g.historyCount(), 2u);
    ASSERT_TRUE(compiles(g));
    ASSERT_TRUE(g.execute());

    g.beginFrame(64, 64);
    EXPECT_EQ(g.historyCount(), 2u);
    g.history("A", tex()); // B's feature was switched off
    ASSERT_TRUE(compiles(g));
    ASSERT_TRUE(g.execute());

    g.beginFrame(64, 64);
    EXPECT_EQ(g.historyCount(), 1u);
    const HistoryPair b = g.history("B", tex());
    EXPECT_FALSE(b.prev.valid());
    EXPECT_EQ(g.historyCount(), 2u);
}

TEST(RenderGraphHistory, RequestingTwiceInAFrameReturnsTheSamePair)
{
    RenderGraph       g;
    const HistoryPair f1 = taaFrame(g);
    (void)f1;
    g.beginFrame(640, 360);
    const HistoryPair a = g.history("TAA", tex());
    const HistoryPair b = g.history("TAA", tex());
    EXPECT_TRUE(a.curr == b.curr);
    EXPECT_TRUE(a.prev == b.prev);
    EXPECT_TRUE(a.prev.valid());
    const HistoryPair c = g.history("TAA", tex(kRgba8));
    EXPECT_FALSE(c.curr.valid());
    EXPECT_TRUE(g.hasError(ErrorCode::HistoryDescMismatch));
    EXPECT_FALSE(g.compile());
}

TEST(RenderGraphHistory, BarriersDiscardUninitializedSlotsAndRestInShaderResource)
{
    RenderGraph g;
    for (int frame = 0; frame < 3; ++frame)
    {
        const HistoryPair             p  = taaFrame(g, 640, 360, false);
        const std::vector<BarrierRec> cb = barriersFor(g, "TAA.curr");
        ASSERT_GE(cb.size(), 2u) << frame;
        // Frames 1 and 2 write slots that were never written: UNDEFINED + DISCARD. Frame 3 reuses slot 0.
        if (frame < 2)
        {
            EXPECT_TRUE(cb[0].discard) << frame;
            EXPECT_EQ(cb[0].before.layout, Layout::Undefined);
        }
        else
        {
            EXPECT_FALSE(cb[0].discard);
            EXPECT_EQ(cb[0].before.layout, Layout::ShaderResource);
        }
        EXPECT_EQ(cb.back().after.layout, Layout::ShaderResource);
        EXPECT_EQ(cb.back().placement, BarrierPlacement::AfterPass);
        // prev rests in SHADER_RESOURCE and is sampled there: no barrier.
        if (p.prev.valid())
        {
            EXPECT_TRUE(barriersFor(g, "TAA.prev").empty());
        }
        ASSERT_TRUE(g.execute());
    }
}

TEST(RenderGraphHistory, CurrWrittenOnComputeIsJoinedBeforeTheEpilogue)
{
    RenderGraph    g;
    CompileOptions o;
    o.asyncCompute = true;
    g.setOptions(o);
    g.beginFrame(128, 128);
    const HistoryPair pair = g.history("Lum", absTex(1, 1, kR32F));
    pass(g, "Adapt",
         [&](PassBuilder& b)
         {
             b.queue(Queue::AsyncCompute);
             b.write(pair.curr, Use::Uav);
         });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierRec> cb = barriersFor(g, "Lum.curr");
    ASSERT_EQ(cb.size(), 2u);
    EXPECT_EQ(cb[1].queue, Queue::AsyncCompute);
    EXPECT_EQ(cb[1].after.layout, Layout::ShaderResource);
    bool joined = false;
    for (const SyncPoint& s : g.syncPoints())
        joined = joined || (s.signalQueue == Queue::AsyncCompute && s.beforePass == kEpilogue);
    EXPECT_TRUE(joined);
}

// ---------------------------------------------------------------- aliasing

namespace
{
    // T1 [A,B], T2 [B,C], T3 [C,D]: T1 and T3 never overlap.
    void chain(RenderGraph& g, uint32_t format = kRgba16F)
    {
        g.beginFrame(512, 512);
        const TextureHandle bb = g.importBackBuffer(nullptr, 512, 512, kBgra8);
        TextureHandle       t1, t2, t3;
        pass(g, "A", [&](PassBuilder& b) { t1 = b.write(b.create("T1", tex(format))); });
        pass(g, "B",
             [&](PassBuilder& b)
             {
                 b.read(t1);
                 t2 = b.write(b.create("T2", tex(format)));
             });
        pass(g, "C",
             [&](PassBuilder& b)
             {
                 b.read(t2);
                 t3 = b.write(b.create("T3", tex(format)));
             });
        pass(g, "D",
             [&](PassBuilder& b)
             {
                 b.read(t3);
                 b.write(bb);
             });
    }
} // namespace

TEST(RenderGraphAlias, DisjointLifetimesShareMemory)
{
    RenderGraph g;
    chain(g);
    ASSERT_TRUE(compiles(g));
    const ResourceInfo t1 = g.resourceInfo(g.findResource("T1"));
    const ResourceInfo t2 = g.resourceInfo(g.findResource("T2"));
    const ResourceInfo t3 = g.resourceInfo(g.findResource("T3"));
    EXPECT_EQ(t1.bytes, 512u * 512u * 8u);
    EXPECT_EQ(t1.aliasSlot, t3.aliasSlot);
    EXPECT_NE(t1.aliasSlot, t2.aliasSlot);
    EXPECT_EQ(g.aliasSlots().size(), 2u);
    EXPECT_EQ(g.transientBytesUnaliased(), 3 * t1.bytes);
    EXPECT_EQ(g.transientBytesAliased(), 2 * t1.bytes);
    EXPECT_EQ(g.resourceInfo(g.findResource("backbuffer")).aliasSlot, kNoPass);
    // T3's first barrier waits for T1's last work in that memory.
    const std::vector<BarrierRec> b3 = barriersFor(g, "T3");
    ASSERT_FALSE(b3.empty());
    EXPECT_TRUE(b3[0].aliased);
    EXPECT_TRUE(b3[0].discard);
    EXPECT_TRUE(any(b3[0].before.sync & Sync::PixelShading)); // B sampled T1
    EXPECT_TRUE(any(b3[0].before.sync & Sync::RenderTarget)); // A rendered T1
    EXPECT_EQ(b3[0].before.access, Access::NoAccess);
    EXPECT_FALSE(barriersFor(g, "T1")[0].aliased);
}

TEST(RenderGraphAlias, NoAliasingGivesEachTransientItsOwnSlot)
{
    RenderGraph    g;
    CompileOptions o;
    o.noAliasing = true;
    g.setOptions(o);
    chain(g);
    ASSERT_TRUE(compiles(g));
    EXPECT_EQ(g.aliasSlots().size(), 3u);
    EXPECT_EQ(g.transientBytesAliased(), g.transientBytesUnaliased());
    for (const BarrierRec& b : g.barriers())
        EXPECT_FALSE(b.aliased);
}

TEST(RenderGraphAlias, UnknownSizesAreNeverShared)
{
    RenderGraph g;
    chain(g, /*format*/ 0);
    ASSERT_TRUE(compiles(g));
    EXPECT_EQ(g.aliasSlots().size(), 3u);
    EXPECT_EQ(g.transientBytesUnaliased(), 0u);
}

TEST(RenderGraphAlias, BestFitPicksTheSmallestSlotThatFits)
{
    RenderGraph g;
    g.beginFrame(1024, 1024);
    const TextureHandle bb = g.importBackBuffer(nullptr, 1024, 1024, kBgra8);
    TextureHandle       big, small, mid, late;
    pass(g, "Big", [&](PassBuilder& b) { big = b.write(b.create("Big", tex(kRgba16F))); });
    pass(g, "Small", [&](PassBuilder& b) { small = b.write(b.create("Small", tex(kR8, 0.5f))); });
    pass(g, "Mid",
         [&](PassBuilder& b)
         {
             b.read(big);
             b.read(small);
             mid = b.write(b.create("Mid", tex(kRgba8)));
         });
    pass(g, "Late",
         [&](PassBuilder& b)
         {
             b.read(mid);
             late = b.write(b.create("Late", tex(kR8, 0.25f)));
         });
    pass(g, "Out",
         [&](PassBuilder& b)
         {
             b.read(late);
             b.write(bb);
         });
    ASSERT_TRUE(compiles(g));
    EXPECT_EQ(g.resourceInfo(late.index).aliasSlot, g.resourceInfo(small.index).aliasSlot);
    EXPECT_NE(g.resourceInfo(mid.index).aliasSlot, g.resourceInfo(big.index).aliasSlot); // overlaps at Mid
    // Slot sizes are the max of their occupants.
    for (const AliasSlot& s : g.aliasSlots())
        for (const uint16_t r : s.resources)
            EXPECT_GE(s.bytes, g.resourceInfo(r).bytes);
}

TEST(RenderGraphAlias, CrossQueueReuseNeedsAFence)
{
    // Graphics T1 [G1,G2]; compute C1 writes T2 with no dependency on G2: no sharing.
    // With C1 reading G2's output (a fence after G2) T2 may reuse T1's memory.
    for (bool dependent : { false, true })
    {
        RenderGraph    g;
        CompileOptions o;
        o.asyncCompute = true;
        g.setOptions(o);
        g.beginFrame(256, 256);
        const BufferHandle sink  = g.importBuffer("Sink", buf(64), nullptr);
        const BufferHandle sink2 = g.importBuffer("Sink2", buf(64), nullptr);
        TextureHandle      t1, g2out, t2;
        pass(g, "G1", [&](PassBuilder& b) { t1 = b.write(b.create("T1", tex()), Use::Uav); });
        pass(g, "G2",
             [&](PassBuilder& b)
             {
                 b.read(t1, Use::ComputeSrv);
                 g2out = b.write(b.create("G2out", tex(kR8, 0.25f)), Use::Uav);
             });
        pass(g, "C1",
             [&](PassBuilder& b)
             {
                 b.queue(Queue::AsyncCompute);
                 if (dependent)
                     b.read(g2out, Use::ComputeSrv);
                 t2 = b.write(b.create("T2", tex()), Use::Uav);
             });
        pass(g, "C2",
             [&](PassBuilder& b)
             {
                 b.queue(Queue::AsyncCompute);
                 b.read(t2, Use::ComputeSrv);
                 b.write(sink, Use::Uav);
             });
        if (!dependent)
        {
            pass(g, "G3",
                 [&](PassBuilder& b)
                 {
                     b.read(g2out, Use::PixelSrv);
                     b.write(sink2, Use::CopyDst);
                 });
        }
        ASSERT_TRUE(compiles(g)) << dependent;
        const bool shared = g.resourceInfo(t1.index).aliasSlot == g.resourceInfo(t2.index).aliasSlot;
        EXPECT_EQ(shared, dependent);
        if (dependent)
        {
            // The reuse is on another queue: the fence orders it, and T2's first barrier is on compute.
            const std::vector<BarrierRec> b2 = barriersFor(g, "T2");
            ASSERT_FALSE(b2.empty());
            EXPECT_EQ(b2[0].queue, Queue::AsyncCompute);
            EXPECT_TRUE(b2[0].aliased);
            EXPECT_TRUE(syncLegalOnQueue(b2[0].before.sync, Queue::AsyncCompute));
        }
    }
}

TEST(RenderGraphAlias, PersistentResourcesAreNeverAliased)
{
    RenderGraph g;
    g.beginFrame(64, 64);
    const TextureHandle imp  = g.importTexture("Imp", absTex(64, 64), nullptr, Layout::ShaderResource, Layout::ShaderResource);
    const HistoryPair   hist = g.history("H", tex());
    TextureHandle       t;
    pass(g, "A", [&](PassBuilder& b) { t = b.write(b.create("T", tex())); });
    pass(g, "B",
         [&](PassBuilder& b)
         {
             b.read(t);
             b.write(imp);
         });
    pass(g, "C", [&](PassBuilder& b) { b.write(hist.curr); });
    ASSERT_TRUE(compiles(g));
    EXPECT_EQ(g.resourceInfo(imp.index).aliasSlot, kNoPass);
    EXPECT_EQ(g.resourceInfo(hist.curr.index).aliasSlot, kNoPass);
    EXPECT_NE(g.resourceInfo(t.index).aliasSlot, kNoPass);
    for (const AliasSlot& s : g.aliasSlots())
        for (const uint16_t r : s.resources)
            EXPECT_EQ(g.resourceInfo(r).lifetime, Lifetime::Transient);
}

TEST(RenderGraphAlias, ReusedBufferMemoryGetsAnAliasBarrier)
{
    RenderGraph g;
    g.beginFrame(64, 64);
    const BufferHandle sinkB = g.importBuffer("SinkB", buf(64), nullptr);
    const BufferHandle sinkD = g.importBuffer("SinkD", buf(64), nullptr);
    BufferHandle       b1, b2;
    pass(g, "A", [&](PassBuilder& b) { b1 = b.write(b.create("B1", buf(4096)), Use::Uav); });
    pass(g, "B",
         [&](PassBuilder& b)
         {
             b.read(b1, Use::ComputeSrv);
             b.write(sinkB, Use::CopyDst);
         });
    pass(g, "C", [&](PassBuilder& b) { b2 = b.write(b.create("B2", buf(2048)), Use::Uav); });
    pass(g, "D",
         [&](PassBuilder& b)
         {
             b.read(b2, Use::ComputeSrv);
             b.write(sinkD, Use::CopyDst);
         });
    ASSERT_TRUE(compiles(g));
    EXPECT_EQ(g.resourceInfo(b1.index).aliasSlot, g.resourceInfo(b2.index).aliasSlot);
    EXPECT_TRUE(barriersFor(g, "B1").size() == 1u); // UAV -> SRV only, no first-use barrier
    const std::vector<BarrierRec> bb2 = barriersFor(g, "B2");
    ASSERT_FALSE(bb2.empty());
    EXPECT_TRUE(bb2[0].aliased);
    EXPECT_EQ(bb2[0].pass, g.findPass("C"));
    EXPECT_TRUE(any(bb2[0].before.sync & (Sync::AllShading | Sync::ComputeShading)));
    EXPECT_EQ(g.aliasSlots()[g.resourceInfo(b1.index).aliasSlot].bytes, 4096u);
}

// ---------------------------------------------------------------- history slots (Phase 5)

TEST(RenderGraphHistory, SlotsNameTheTargetsBehindThePair)
{
    RenderGraph g;
    EXPECT_EQ(g.historySlots("TAA").curr, kNoPass); // never requested
    const HistoryPair f1 = taaFrame(g, 640, 360, false);
    const HistorySlots s1 = g.historySlots("TAA");
    EXPECT_EQ(s1.curr, idOf(g, f1.curr));
    EXPECT_NE(s1.prev, kNoPass); // prev is a real slot even on first use
    EXPECT_NE(s1.prev, s1.curr);
    EXPECT_FALSE(s1.prevValid);
    EXPECT_TRUE(g.execute());

    const HistoryPair  f2 = taaFrame(g, 640, 360, false);
    const HistorySlots s2 = g.historySlots("TAA");
    EXPECT_TRUE(s2.prevValid);
    EXPECT_EQ(s2.prev, s1.curr);
    EXPECT_EQ(s2.curr, s1.prev);
    EXPECT_EQ(s2.prev, idOf(g, f2.prev));
    EXPECT_EQ(g.historySlots(nullptr).curr, kNoPass);
    EXPECT_EQ(g.historySlots("Other").curr, kNoPass);
}

TEST(RenderGraphHistory, MarkUnwrittenDropsNextFramesPrevOnly)
{
    RenderGraph g;
    taaFrame(g);
    taaFrame(g);
    g.markHistoryUnwritten("TAA"); // after execute: the pass skipped its write
    EXPECT_FALSE(taaFrame(g).prev.valid());
    EXPECT_TRUE(taaFrame(g).prev.valid());
    g.markHistoryUnwritten("Nope"); // unknown names are ignored
    g.markHistoryUnwritten(nullptr);
    EXPECT_TRUE(taaFrame(g).prev.valid());
}

TEST(RenderGraphHistory, LiveSlotsFollowEntriesAndResizeIssuesNewIds)
{
    RenderGraph           g;
    std::vector<uint32_t> live;
    g.liveHistorySlots(live);
    EXPECT_TRUE(live.empty());

    taaFrame(g, 640, 360);
    const HistorySlots small = g.historySlots("TAA");
    g.liveHistorySlots(live);
    ASSERT_EQ(live.size(), 2u);
    EXPECT_NE(std::find(live.begin(), live.end(), small.curr), live.end());
    EXPECT_NE(std::find(live.begin(), live.end(), small.prev), live.end());

    // A new output size is a new allocation: new ids, the old ones are no longer live.
    taaFrame(g, 800, 600);
    const HistorySlots big = g.historySlots("TAA");
    EXPECT_FALSE(big.prevValid);
    g.liveHistorySlots(live);
    ASSERT_EQ(live.size(), 2u);
    EXPECT_EQ(std::find(live.begin(), live.end(), small.curr), live.end());
    EXPECT_EQ(std::find(live.begin(), live.end(), small.prev), live.end());

    // Not requested for a frame: the entry goes at the following beginFrame.
    g.beginFrame(800, 600);
    g.beginFrame(800, 600);
    g.liveHistorySlots(live);
    EXPECT_TRUE(live.empty());
    EXPECT_EQ(g.historyCount(), 0u);
}
