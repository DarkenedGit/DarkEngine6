#include "RenderGraphTestUtil.h"

#include <string>
#include <vector>

// Barrier planning (plan §3.7): enhanced-barrier records only, merged per pass, UNDEFINED+DISCARD
// on first transient use, read groups merged, import/back-buffer initial and final layouts, and the
// queue-legality table the planner relies on.

using namespace Dark::Rg;
using namespace Dark::Rg::Test;

namespace
{
    TextureHandle frame(RenderGraph& g, uint32_t w = 640, uint32_t h = 360)
    {
        g.beginFrame(w, h);
        return g.importBackBuffer(nullptr, w, h, kBgra8);
    }

    bool hasSync(Sync set, Sync bit)
    {
        return any(set & bit);
    }
} // namespace

// ---------------------------------------------------------------- use table

TEST(RenderGraphBarriers, UseStatesMatchTheirLayouts)
{
    for (uint32_t u = 0; u < static_cast<uint32_t>(Use::Present); ++u)
    {
        const Use use = static_cast<Use>(u);
        for (ResourceKind kind : { ResourceKind::Texture, ResourceKind::Buffer })
        {
            if (!useAllowedForKind(use, kind))
                continue;
            for (Queue q : { Queue::Graphics, Queue::AsyncCompute })
            {
                if (!useLegalOnQueue(use, q))
                    continue;
                const UseState s = useState(use, kind, q);
                SCOPED_TRACE(std::string(useName(use)) + " on " + queueName(q));
                EXPECT_TRUE(syncLegalOnQueue(s.sync, q));
                EXPECT_TRUE(accessLegalOnQueue(s.access, q));
                EXPECT_NE(s.sync, Sync::None);
                if (kind == ResourceKind::Texture)
                {
                    EXPECT_TRUE(layoutLegalOnQueue(s.layout, q));
                    EXPECT_TRUE(accessAllowedInLayout(s.access, s.layout));
                }
                else
                {
                    EXPECT_EQ(s.layout, Layout::Undefined);
                }
                EXPECT_EQ(isReadOnlyState(s), !isWriteUse(use));
                // Each use is a valid barrier target from nothing.
                BarrierRec rec;
                rec.kind    = kind;
                rec.queue   = q;
                rec.before  = { Sync::None, Access::NoAccess, kind == ResourceKind::Texture ? Layout::Undefined : Layout::Undefined };
                rec.after   = s;
                rec.discard = kind == ResourceKind::Texture;
                std::string why;
                EXPECT_TRUE(validateBarrier(rec, &why)) << why;
            }
        }
    }
}

TEST(RenderGraphBarriers, ComputeQueueRejectsGraphicsOnlyUses)
{
    EXPECT_FALSE(useLegalOnQueue(Use::RenderTarget, Queue::AsyncCompute));
    EXPECT_FALSE(useLegalOnQueue(Use::DepthWrite, Queue::AsyncCompute));
    EXPECT_FALSE(useLegalOnQueue(Use::DepthRead, Queue::AsyncCompute));
    EXPECT_FALSE(useLegalOnQueue(Use::PixelSrv, Queue::AsyncCompute));
    EXPECT_FALSE(useLegalOnQueue(Use::VertexIndex, Queue::AsyncCompute));
    EXPECT_TRUE(useLegalOnQueue(Use::ComputeSrv, Queue::AsyncCompute));
    EXPECT_TRUE(useLegalOnQueue(Use::Uav, Queue::AsyncCompute));
    EXPECT_TRUE(useLegalOnQueue(Use::CopyDst, Queue::AsyncCompute));
    EXPECT_TRUE(useLegalOnQueue(Use::IndirectArgs, Queue::AsyncCompute));
    EXPECT_FALSE(layoutLegalOnQueue(Layout::RenderTarget, Queue::AsyncCompute));
    EXPECT_FALSE(layoutLegalOnQueue(Layout::DepthStencilWrite, Queue::AsyncCompute));
    EXPECT_FALSE(layoutLegalOnQueue(Layout::DepthStencilRead, Queue::AsyncCompute));
    for (Layout l : { Layout::Common, Layout::GenericRead, Layout::UnorderedAccess, Layout::ShaderResource, Layout::CopySource, Layout::CopyDest })
        EXPECT_TRUE(layoutLegalOnQueue(l, Queue::AsyncCompute)) << layoutName(l);
    EXPECT_FALSE(syncLegalOnQueue(Sync::PixelShading, Queue::AsyncCompute));
    EXPECT_FALSE(syncLegalOnQueue(Sync::RenderTarget, Queue::AsyncCompute));
    EXPECT_TRUE(syncLegalOnQueue(Sync::ComputeShading | Sync::Copy, Queue::AsyncCompute));
    EXPECT_FALSE(accessLegalOnQueue(Access::RenderTarget, Queue::AsyncCompute));
}

TEST(RenderGraphBarriers, CombineReadStates)
{
    UseState       out;
    const UseState srv  = useState(Use::PixelSrv, ResourceKind::Texture, Queue::Graphics);
    const UseState csrv = useState(Use::ComputeSrv, ResourceKind::Texture, Queue::Graphics);
    const UseState copy = useState(Use::CopySrc, ResourceKind::Texture, Queue::Graphics);
    const UseState dr   = useState(Use::DepthRead, ResourceKind::Texture, Queue::Graphics);
    const UseState rt   = useState(Use::RenderTarget, ResourceKind::Texture, Queue::Graphics);
    ASSERT_TRUE(combineReadStates(srv, csrv, ResourceKind::Texture, out));
    EXPECT_EQ(out.layout, Layout::ShaderResource);
    EXPECT_EQ(out.sync, Sync::PixelShading | Sync::ComputeShading);
    ASSERT_TRUE(combineReadStates(srv, copy, ResourceKind::Texture, out));
    EXPECT_EQ(out.layout, Layout::GenericRead);
    ASSERT_TRUE(combineReadStates(dr, srv, ResourceKind::Texture, out));
    EXPECT_EQ(out.layout, Layout::DepthStencilRead);
    EXPECT_FALSE(combineReadStates(dr, copy, ResourceKind::Texture, out));
    EXPECT_FALSE(combineReadStates(rt, srv, ResourceKind::Texture, out));
    // Buffers: any reads combine, no layout.
    ASSERT_TRUE(combineReadStates(useState(Use::IndirectArgs, ResourceKind::Buffer, Queue::Graphics), useState(Use::ComputeSrv, ResourceKind::Buffer, Queue::Graphics), ResourceKind::Buffer, out));
    EXPECT_EQ(out.layout, Layout::Undefined);
    EXPECT_EQ(out.access, Access::IndirectArgument | Access::ShaderResource);
}

TEST(RenderGraphBarriers, ValidateBarrierRejectsBadRecords)
{
    const auto base = []
    {
        BarrierRec r;
        r.kind   = ResourceKind::Texture;
        r.queue  = Queue::Graphics;
        r.before = { Sync::RenderTarget, Access::RenderTarget, Layout::RenderTarget };
        r.after  = { Sync::PixelShading, Access::ShaderResource, Layout::ShaderResource };
        return r;
    };
    std::string why;
    EXPECT_TRUE(validateBarrier(base(), &why)) << why;

    BarrierRec r  = base();
    r.before.sync = Sync::None; // NONE with real access
    EXPECT_FALSE(validateBarrier(r, &why));
    r         = base();
    r.discard = true; // DISCARD needs an UNDEFINED before-layout
    EXPECT_FALSE(validateBarrier(r, &why));
    r              = base();
    r.after.layout = Layout::Undefined;
    EXPECT_FALSE(validateBarrier(r, &why));
    r              = base();
    r.after.access = Access::RenderTarget; // RT access in SR layout
    r.after.sync   = Sync::RenderTarget;
    EXPECT_FALSE(validateBarrier(r, &why));
    r            = base();
    r.after.sync = Sync::Copy; // SR access not covered by COPY
    EXPECT_FALSE(validateBarrier(r, &why));
    r       = base();
    r.queue = Queue::AsyncCompute; // RT layout on compute
    EXPECT_FALSE(validateBarrier(r, &why));
    r           = base();
    r.placement = BarrierPlacement::Epilogue;
    r.queue     = Queue::AsyncCompute;
    r.before    = { Sync::None, Access::NoAccess, Layout::ShaderResource };
    r.after     = { Sync::None, Access::NoAccess, Layout::Common };
    EXPECT_FALSE(validateBarrier(r, &why)); // prologue/epilogue are graphics only
    r.queue = Queue::Graphics;
    EXPECT_TRUE(validateBarrier(r, &why)) << why;
    r               = base();
    r.before.access = Access::NoAccess | Access::RenderTarget;
    EXPECT_FALSE(validateBarrier(r, &why));
    // A real sync with NO_ACCESS is legal (wait, no flush): used by alias fix-ups.
    r         = base();
    r.before  = { Sync::PixelShading, Access::NoAccess, Layout::Undefined };
    r.after   = { Sync::RenderTarget, Access::RenderTarget, Layout::RenderTarget };
    r.discard = true;
    EXPECT_TRUE(validateBarrier(r, &why)) << why;
    // Buffers can't carry texture-only access.
    r        = base();
    r.kind   = ResourceKind::Buffer;
    r.before = { Sync::ComputeShading, Access::UnorderedAccess, Layout::Undefined };
    r.after  = { Sync::RenderTarget, Access::RenderTarget, Layout::Undefined };
    EXPECT_FALSE(validateBarrier(r, &why));
}

TEST(RenderGraphBarriers, TextureSizeEstimates)
{
    EXPECT_EQ(formatBytesPerPixel(kRgba16F), 8u);
    EXPECT_EQ(formatBytesPerPixel(kRgba8), 4u);
    EXPECT_EQ(formatBytesPerPixel(kD32), 4u);
    EXPECT_EQ(formatBytesPerPixel(kR8), 1u);
    EXPECT_EQ(formatBytesPerPixel(0), 0u); // unknown
    TextureDesc d = tex(kRgba8);
    EXPECT_EQ(estimateTextureBytes(100, 50, d), 100u * 50u * 4u);
    d.mips = 3; // 100x50 + 50x25 + 25x12
    EXPECT_EQ(estimateTextureBytes(100, 50, d), (100u * 50u + 50u * 25u + 25u * 12u) * 4u);
    d.mips      = 1;
    d.arraySize = 6;
    EXPECT_EQ(estimateTextureBytes(16, 16, d), 16u * 16u * 4u * 6u);
    d.mips      = kAllMips; // full chain down to 1x1
    d.arraySize = 1;
    EXPECT_EQ(estimateTextureBytes(4, 4, d), (16u + 4u + 1u) * 4u);
}

// ---------------------------------------------------------------- planned barriers

TEST(RenderGraphBarriers, FirstTransientUseDiscardsFromUndefined)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       t;
    pass(g, "A", [&](PassBuilder& b) { t = b.write(b.create("T", tex())); });
    pass(g, "B",
         [&](PassBuilder& b)
         {
             b.read(t);
             b.write(bb);
         });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierRec> tb = barriersFor(g, "T");
    ASSERT_EQ(tb.size(), 2u);
    EXPECT_EQ(tb[0].placement, BarrierPlacement::BeforePass);
    EXPECT_EQ(tb[0].pass, g.findPass("A"));
    EXPECT_TRUE(tb[0].discard);
    EXPECT_EQ(tb[0].before.layout, Layout::Undefined);
    EXPECT_EQ(tb[0].before.sync, Sync::None);
    EXPECT_EQ(tb[0].before.access, Access::NoAccess);
    EXPECT_EQ(tb[0].after.layout, Layout::RenderTarget);
    // RT -> SR before B.
    EXPECT_EQ(tb[1].pass, g.findPass("B"));
    EXPECT_FALSE(tb[1].discard);
    EXPECT_EQ(tb[1].before.sync, Sync::RenderTarget);
    EXPECT_EQ(tb[1].before.access, Access::RenderTarget);
    EXPECT_EQ(tb[1].before.layout, Layout::RenderTarget);
    EXPECT_EQ(tb[1].after.sync, Sync::PixelShading);
    EXPECT_EQ(tb[1].after.access, Access::ShaderResource);
    EXPECT_EQ(tb[1].after.layout, Layout::ShaderResource);
}

TEST(RenderGraphBarriers, BackBufferPresentToRenderTargetAndBack)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       v1;
    pass(g, "Clear", [&](PassBuilder& b) { v1 = b.write(bb, Use::CopyDst); });
    pass(g, "Ui", [&](PassBuilder& b) { b.write(v1, Use::RenderTarget); });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierRec> bbs = barriersFor(g, "backbuffer");
    ASSERT_EQ(bbs.size(), 3u);
    EXPECT_EQ(bbs[0].before.layout, Layout::Present);
    EXPECT_FALSE(bbs[0].discard); // imported contents are kept
    EXPECT_EQ(bbs[0].after.layout, Layout::CopyDest);
    EXPECT_EQ(bbs[1].before.layout, Layout::CopyDest);
    EXPECT_EQ(bbs[1].after.layout, Layout::RenderTarget);
    EXPECT_EQ(bbs[2].placement, BarrierPlacement::AfterPass);
    EXPECT_EQ(bbs[2].pass, g.findPass("Ui"));
    EXPECT_EQ(bbs[2].before.layout, Layout::RenderTarget);
    EXPECT_EQ(bbs[2].after.layout, Layout::Present);
    EXPECT_EQ(bbs[2].after.sync, Sync::None);
    EXPECT_EQ(bbs[2].after.access, Access::NoAccess);
}

TEST(RenderGraphBarriers, ImportAlreadyInTheRightLayoutNeedsNoBarrier)
{
    RenderGraph g;
    frame(g);
    const TextureHandle env = g.importTexture("Env", absTex(32, 32), nullptr, Layout::ShaderResource, Layout::ShaderResource);
    pass(g, "Sky",
         [&](PassBuilder& b)
         {
             b.read(env, Use::PixelSrv);
             b.flags(PassFlags::SideEffect);
         });
    ASSERT_TRUE(compiles(g));
    EXPECT_TRUE(barriersFor(g, "Env").empty());
}

TEST(RenderGraphBarriers, UntouchedImportStillReachesItsFinalLayout)
{
    RenderGraph g;
    g.beginFrame(64, 64);
    g.importTexture("Same", absTex(8, 8), nullptr, Layout::ShaderResource, Layout::ShaderResource);
    g.importTexture("Moves", absTex(8, 8), nullptr, Layout::CopyDest, Layout::ShaderResource);
    g.importTexture("NoFinal", absTex(8, 8), nullptr, Layout::CopyDest, Layout::Undefined);
    ASSERT_TRUE(compiles(g));
    EXPECT_TRUE(barriersFor(g, "Same").empty());
    EXPECT_TRUE(barriersFor(g, "NoFinal").empty());
    const std::vector<BarrierRec> m = barriersFor(g, "Moves");
    ASSERT_EQ(m.size(), 1u);
    EXPECT_EQ(m[0].placement, BarrierPlacement::Epilogue);
    EXPECT_EQ(m[0].queue, Queue::Graphics);
    EXPECT_EQ(m[0].before.layout, Layout::CopyDest);
    EXPECT_EQ(m[0].after.layout, Layout::ShaderResource);
    Recorder rec(g);
    ASSERT_TRUE(g.execute(&rec));
    ASSERT_EQ(rec.events.size(), 1u);
    EXPECT_EQ(rec.events[0], "B epilogue epilogue Graphics [Moves]");
}

TEST(RenderGraphBarriers, ConsecutiveReadsShareOneBarrier)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       t;
    pass(g, "A", [&](PassBuilder& b) { t = b.write(b.create("T", tex())); });
    pass(g, "B",
         [&](PassBuilder& b)
         {
             b.read(t, Use::PixelSrv);
             b.write(b.create("X", tex()));
             b.flags(PassFlags::SideEffect);
         });
    pass(g, "C",
         [&](PassBuilder& b)
         {
             b.read(t, Use::ComputeSrv);
             b.flags(PassFlags::SideEffect);
         });
    pass(g, "D",
         [&](PassBuilder& b)
         {
             b.read(t, Use::PixelSrv);
             b.write(bb);
         });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierRec> tb = barriersFor(g, "T");
    ASSERT_EQ(tb.size(), 2u); // discard->RT before A, RT->SR before B; nothing before C or D
    EXPECT_EQ(tb[1].pass, g.findPass("B"));
    EXPECT_TRUE(hasSync(tb[1].after.sync, Sync::PixelShading));
    EXPECT_TRUE(hasSync(tb[1].after.sync, Sync::ComputeShading)); // the group's union
}

TEST(RenderGraphBarriers, ReadsThatCombineUseGenericRead)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       t;
    pass(g, "A", [&](PassBuilder& b) { t = b.write(b.create("T", tex())); });
    pass(g, "Sample",
         [&](PassBuilder& b)
         {
             b.read(t, Use::PixelSrv);
             b.write(b.create("X", tex()));
             b.flags(PassFlags::SideEffect);
         });
    pass(g, "Copy",
         [&](PassBuilder& b)
         {
             b.read(t, Use::CopySrc);
             b.write(bb, Use::CopyDst);
         });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierRec> tb = barriersFor(g, "T");
    ASSERT_EQ(tb.size(), 2u);
    EXPECT_EQ(tb[1].after.layout, Layout::GenericRead);
    EXPECT_TRUE(any(tb[1].after.access & Access::CopySource));
}

TEST(RenderGraphBarriers, ReadsThatDontCombineSplit)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       d;
    pass(g, "Depth", [&](PassBuilder& b) { d = b.write(b.create("D", tex(kD32)), Use::DepthWrite); });
    pass(g, "Test",
         [&](PassBuilder& b)
         {
             b.read(d, Use::DepthRead);
             b.write(b.create("X", tex()));
             b.flags(PassFlags::SideEffect);
         });
    pass(g, "Copy",
         [&](PassBuilder& b)
         {
             b.read(d, Use::CopySrc);
             b.write(bb, Use::CopyDst);
         });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierRec> db = barriersFor(g, "D");
    ASSERT_EQ(db.size(), 3u);
    EXPECT_EQ(db[1].after.layout, Layout::DepthStencilRead);
    EXPECT_EQ(db[2].before.layout, Layout::DepthStencilRead);
    EXPECT_EQ(db[2].after.layout, Layout::CopySource);
    EXPECT_EQ(db[2].pass, g.findPass("Copy"));
}

TEST(RenderGraphBarriers, UavChainGetsSyncOnlyBarriers)
{
    RenderGraph g;
    frame(g);
    const BufferHandle out = g.importBuffer("Out", buf(64), nullptr);
    TextureHandle      t;
    pass(g, "A", [&](PassBuilder& b) { t = b.write(b.create("T", tex()), Use::Uav); });
    pass(g, "B", [&](PassBuilder& b) { t = b.readWrite(t, Use::Uav); });
    pass(g, "C",
         [&](PassBuilder& b)
         {
             b.read(t, Use::ComputeSrv);
             b.write(out, Use::Uav);
         });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierRec> tb = barriersFor(g, "T");
    ASSERT_EQ(tb.size(), 3u);
    EXPECT_EQ(tb[1].pass, g.findPass("B"));
    EXPECT_EQ(tb[1].before.layout, Layout::UnorderedAccess);
    EXPECT_EQ(tb[1].after.layout, Layout::UnorderedAccess);
    EXPECT_EQ(tb[1].before.access, Access::UnorderedAccess);
    EXPECT_EQ(tb[1].after.access, Access::UnorderedAccess);
    EXPECT_EQ(tb[2].after.layout, Layout::ShaderResource);
}

TEST(RenderGraphBarriers, BufferBarriersHaveNoLayoutAndSkipReadToRead)
{
    RenderGraph g;
    frame(g);
    const BufferHandle sink = g.importBuffer("Sink", buf(64), nullptr);
    BufferHandle       args;
    pass(g, "Build", [&](PassBuilder& b) { args = b.write(b.create("Args", buf(64, 16)), Use::Uav); });
    pass(g, "Draw1",
         [&](PassBuilder& b)
         {
             b.read(args, Use::IndirectArgs);
             b.flags(PassFlags::SideEffect);
         });
    pass(g, "Draw2",
         [&](PassBuilder& b)
         {
             b.read(args, Use::IndirectArgs);
             b.write(sink, Use::CopyDst);
         });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierRec> ab = barriersFor(g, "Args");
    ASSERT_EQ(ab.size(), 1u); // no first-use barrier for a buffer; UAV -> indirect; none between the draws
    EXPECT_EQ(ab[0].kind, ResourceKind::Buffer);
    EXPECT_EQ(ab[0].pass, g.findPass("Draw1"));
    EXPECT_EQ(ab[0].before.layout, Layout::Undefined);
    EXPECT_EQ(ab[0].after.layout, Layout::Undefined);
    EXPECT_EQ(ab[0].before.access, Access::UnorderedAccess);
    EXPECT_EQ(ab[0].after.access, Access::IndirectArgument);
    EXPECT_FALSE(ab[0].discard);
}

TEST(RenderGraphBarriers, BarriersAreMergedPerPassAndBatchedInOrder)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       a, n, m;
    pass(g, "GBuffer",
         [&](PassBuilder& b)
         {
             a = b.write(b.create("Albedo", tex(kRgba8)));
             n = b.write(b.create("Normal", tex(kRgba16F)));
             m = b.write(b.create("Motion", tex(kR32F)));
         });
    pass(g, "Light",
         [&](PassBuilder& b)
         {
             b.read(a);
             b.read(n);
             b.read(m);
             b.write(bb);
         });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierBatch>& batches = g.barrierBatches();
    ASSERT_EQ(batches.size(), 3u); // before GBuffer (3), before Light (4), after Light (1)
    EXPECT_EQ(batches[0].pass, g.findPass("GBuffer"));
    EXPECT_EQ(batches[0].count, 3u);
    EXPECT_EQ(batches[1].pass, g.findPass("Light"));
    EXPECT_EQ(batches[1].placement, BarrierPlacement::BeforePass);
    EXPECT_EQ(batches[1].count, 4u);
    EXPECT_EQ(batches[2].placement, BarrierPlacement::AfterPass);
    EXPECT_EQ(batches[2].count, 1u);
    uint32_t total = 0;
    for (const BarrierBatch& b : batches)
    {
        EXPECT_EQ(b.first, total); // contiguous
        total += b.count;
    }
    EXPECT_EQ(total, g.barriers().size());
}

TEST(RenderGraphBarriers, FullBarriersModeIsConservative)
{
    const auto build = [](RenderGraph& g, bool full)
    {
        CompileOptions o;
        o.fullBarriers = full;
        g.setOptions(o);
        const TextureHandle bb = frame(g);
        TextureHandle       t;
        pass(g, "A", [&](PassBuilder& b) { t = b.write(b.create("T", tex())); });
        pass(g, "B",
             [&](PassBuilder& b)
             {
                 b.read(t, Use::PixelSrv);
                 b.write(b.create("X", tex()));
                 b.flags(PassFlags::SideEffect);
             });
        pass(g, "C",
             [&](PassBuilder& b)
             {
                 b.read(t, Use::PixelSrv);
                 b.write(bb);
             });
    };
    RenderGraph normal, full;
    build(normal, false);
    build(full, true);
    ASSERT_TRUE(compiles(normal));
    ASSERT_TRUE(compiles(full));
    EXPECT_GT(full.barriers().size(), normal.barriers().size());
    EXPECT_EQ(barriersFor(full, "T").size(), 3u); // a barrier before the second read too
    for (const BarrierRec& b : full.barriers())
    {
        if (b.before.sync != Sync::None)
        {
            EXPECT_EQ(b.before.sync, Sync::All);
        }
        if (b.after.sync != Sync::None)
        {
            EXPECT_EQ(b.after.sync, Sync::All);
        }
    }
}

TEST(RenderGraphBarriers, EveryPlannedBarrierValidates)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       d, c, h;
    BufferHandle        bufH;
    pass(g, "Depth", [&](PassBuilder& b) { d = b.write(b.create("Depth", tex(kD32)), Use::DepthWrite); });
    pass(g, "Opaque",
         [&](PassBuilder& b)
         {
             b.read(d, Use::DepthRead);
             c = b.write(b.create("Color", tex()));
         });
    pass(g, "Particles",
         [&](PassBuilder& b)
         {
             bufH = b.write(b.create("Particles", buf(4096, 32)), Use::Uav);
             b.read(d, Use::ComputeSrv);
         });
    pass(g, "DrawParticles",
         [&](PassBuilder& b)
         {
             b.read(bufH, Use::VertexIndex);
             c = b.write(c);
         });
    pass(g, "Bloom",
         [&](PassBuilder& b)
         {
             b.read(c, Use::ComputeSrv);
             h = b.write(b.create("Bloom", tex(kR11G11B10, 0.5f)), Use::Uav);
         });
    pass(g, "Copy",
         [&](PassBuilder& b)
         {
             b.read(h, Use::CopySrc);
             b.write(bb, Use::CopyDst);
         });
    ASSERT_TRUE(compiles(g));
    for (const BarrierRec& b : g.barriers())
    {
        std::string why;
        EXPECT_TRUE(validateBarrier(b, &why)) << g.resourceInfo(b.resource).name << ": " << why;
    }
}
