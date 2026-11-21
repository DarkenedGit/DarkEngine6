#include "RenderGraphTestUtil.h"

#include <string>
#include <vector>

// Queue assignment and cross-queue sync (plan §3.5, §3.11): the forced-graphics fallback, minimal
// sync points (transitive reduction), signal values, queue-legal barrier placement and execution
// order through the Executor hooks.

using namespace Dark::Rg;
using namespace Dark::Rg::Test;

namespace
{
    CompileOptions async(bool on = true)
    {
        CompileOptions o;
        o.asyncCompute = on;
        return o;
    }

    // Depth (graphics) -> SSAO (compute) -> Light (graphics) -> back buffer.
    void buildSsao(RenderGraph& g, bool asyncOn)
    {
        g.setOptions(async(asyncOn));
        g.beginFrame(640, 360);
        const TextureHandle bb = g.importBackBuffer(nullptr, 640, 360, kBgra8);
        TextureHandle       d, ao;
        pass(g, "Depth", [&](PassBuilder& b) { d = b.write(b.create("D", tex(kD32)), Use::DepthWrite); });
        pass(g, "SSAO",
             [&](PassBuilder& b)
             {
                 b.queue(Queue::AsyncCompute);
                 b.read(d, Use::ComputeSrv);
                 ao = b.write(b.create("AO", tex(kR8, 0.5f)), Use::Uav);
             });
        pass(g, "Light",
             [&](PassBuilder& b)
             {
                 b.read(ao, Use::PixelSrv);
                 b.write(bb);
             });
    }

    bool hasSync(const RenderGraph& g, Queue signal, const char* after, Queue wait, const char* before)
    {
        const auto item = [&](const char* name)
        {
            if (std::string(name) == "prologue")
                return kPrologue;
            if (std::string(name) == "epilogue")
                return kEpilogue;
            return g.findPass(name);
        };
        for (const SyncPoint& s : g.syncPoints())
            if (s.signalQueue == signal && s.waitQueue == wait && s.afterPass == item(after) && s.beforePass == item(before))
                return true;
        return false;
    }

    BufferHandle sinkBuffer(RenderGraph& g, const char* name)
    {
        return g.importBuffer(name, buf(256), nullptr);
    }
} // namespace

TEST(RenderGraphQueues, AsyncComputeOffForcesEverythingOntoGraphics)
{
    RenderGraph g;
    buildSsao(g, false);
    ASSERT_TRUE(compiles(g));
    EXPECT_EQ(g.passQueue(g.findPass("SSAO")), Queue::Graphics);
    EXPECT_TRUE(g.queueOrder(Queue::AsyncCompute).empty());
    EXPECT_EQ(g.queueOrder(Queue::Graphics).size(), 3u);
    EXPECT_TRUE(g.syncPoints().empty());
    for (const BarrierRec& b : g.barriers())
        EXPECT_EQ(b.queue, Queue::Graphics);
    // Same plan, single queue: D goes DSW -> SR right before SSAO.
    const std::vector<BarrierRec> db = barriersFor(g, "D");
    ASSERT_EQ(db.size(), 2u);
    EXPECT_EQ(db[1].placement, BarrierPlacement::BeforePass);
    EXPECT_EQ(db[1].pass, g.findPass("SSAO"));
    // On graphics a compute-style UAV syncs ALL_SHADING.
    const std::vector<BarrierRec> ab = barriersFor(g, "AO");
    ASSERT_FALSE(ab.empty());
    EXPECT_EQ(ab[0].after.sync, Sync::AllShading);
}

TEST(RenderGraphQueues, AsyncComputeOnUsesTwoQueuesWithFences)
{
    RenderGraph g;
    buildSsao(g, true);
    ASSERT_TRUE(compiles(g));
    const uint32_t depth = g.findPass("Depth"), ssao = g.findPass("SSAO"), light = g.findPass("Light");
    EXPECT_EQ(g.passQueue(ssao), Queue::AsyncCompute);
    ASSERT_EQ(g.queueOrder(Queue::AsyncCompute).size(), 1u);
    EXPECT_EQ(g.queueOrder(Queue::Graphics), (std::vector<uint32_t>{ depth, light }));
    ASSERT_EQ(g.syncPoints().size(), 2u);
    EXPECT_TRUE(hasSync(g, Queue::Graphics, "Depth", Queue::AsyncCompute, "SSAO"));
    EXPECT_TRUE(hasSync(g, Queue::AsyncCompute, "SSAO", Queue::Graphics, "Light"));
    for (const SyncPoint& s : g.syncPoints())
        EXPECT_EQ(s.signalValue, 1u);
    EXPECT_TRUE(g.happensBefore(depth, ssao));
    EXPECT_TRUE(g.happensBefore(ssao, light));
    EXPECT_TRUE(g.happensBefore(depth, light));
    EXPECT_FALSE(g.happensBefore(light, depth));
    // On compute a UAV syncs COMPUTE_SHADING.
    const std::vector<BarrierRec> ab = barriersFor(g, "AO");
    ASSERT_EQ(ab.size(), 2u);
    EXPECT_EQ(ab[0].queue, Queue::AsyncCompute);
    EXPECT_EQ(ab[0].after.sync, Sync::ComputeShading);
    EXPECT_TRUE(ab[0].discard);
    // UAV -> SR on graphics, before Light, nothing to flush on graphics (compute wrote it).
    EXPECT_EQ(ab[1].queue, Queue::Graphics);
    EXPECT_EQ(ab[1].pass, light);
    EXPECT_EQ(ab[1].before.sync, Sync::None);
    EXPECT_EQ(ab[1].before.access, Access::NoAccess);
    EXPECT_EQ(ab[1].before.layout, Layout::UnorderedAccess);
    EXPECT_EQ(ab[1].after.layout, Layout::ShaderResource);
}

TEST(RenderGraphQueues, GraphicsOnlyLayoutIsTransitionedOnGraphicsBeforeTheSignal)
{
    RenderGraph g;
    buildSsao(g, true);
    ASSERT_TRUE(compiles(g));
    // D is DEPTH_STENCIL_WRITE, which compute can't touch: graphics moves it to SHADER_RESOURCE
    // after Depth; compute reads it with no barrier of its own.
    const std::vector<BarrierRec> db = barriersFor(g, "D");
    ASSERT_EQ(db.size(), 2u);
    EXPECT_EQ(db[1].queue, Queue::Graphics);
    EXPECT_EQ(db[1].placement, BarrierPlacement::AfterPass);
    EXPECT_EQ(db[1].pass, g.findPass("Depth"));
    EXPECT_EQ(db[1].before.layout, Layout::DepthStencilWrite);
    EXPECT_EQ(db[1].after.layout, Layout::ShaderResource);
    EXPECT_EQ(db[1].after.sync, Sync::None); // the fence orders the compute read
    for (const BarrierRec& b : g.barriers())
    {
        if (b.queue != Queue::AsyncCompute)
            continue;
        EXPECT_TRUE(layoutLegalOnQueue(b.before.layout, Queue::AsyncCompute));
        EXPECT_TRUE(layoutLegalOnQueue(b.after.layout, Queue::AsyncCompute));
    }
}

TEST(RenderGraphQueues, ExecuteSignalsAndWaitsInOrder)
{
    RenderGraph g;
    buildSsao(g, true);
    ASSERT_TRUE(compiles(g));
    Recorder rec(g);
    ASSERT_TRUE(g.execute(&rec));
    const std::vector<std::string> expected = {
        "B before Depth Graphics [D]",
        "P Depth",
        "E Depth",
        "B after Depth Graphics [D]",
        "S Graphics 1",
        "W AsyncCompute<-Graphics 1",
        "B before SSAO AsyncCompute [AO]",
        "P SSAO",
        "E SSAO",
        "S AsyncCompute 1",
        "W Graphics<-AsyncCompute 1",
        "B before Light Graphics [backbuffer,AO]",
        "P Light",
        "E Light",
        "B after Light Graphics [backbuffer]",
    };
    EXPECT_EQ(rec.events, expected);
}

TEST(RenderGraphQueues, RedundantWaitsAreReduced)
{
    // G1 -> X, G2 -> Y; C1 reads X, C2 reads Y, C3 reads X again. C3's need is covered by C1's wait.
    RenderGraph g;
    g.setOptions(async());
    g.beginFrame(256, 256);
    TextureHandle x, y;
    pass(g, "G1", [&](PassBuilder& b) { x = b.write(b.create("X", tex())); });
    pass(g, "G2", [&](PassBuilder& b) { y = b.write(b.create("Y", tex())); });
    const char* names[] = { "C1", "C2", "C3" };
    for (int i = 0; i < 3; ++i)
    {
        const BufferHandle out = sinkBuffer(g, names[i]);
        pass(g, names[i],
             [&](PassBuilder& b)
             {
                 b.queue(Queue::AsyncCompute);
                 b.read(i == 1 ? y : x, Use::ComputeSrv);
                 b.write(out, Use::Uav);
             });
    }
    ASSERT_TRUE(compiles(g));
    uint32_t graphicsToCompute = 0;
    for (const SyncPoint& s : g.syncPoints())
        graphicsToCompute += s.signalQueue == Queue::Graphics ? 1u : 0u;
    EXPECT_EQ(graphicsToCompute, 2u);
    EXPECT_TRUE(hasSync(g, Queue::Graphics, "G1", Queue::AsyncCompute, "C1"));
    EXPECT_TRUE(hasSync(g, Queue::Graphics, "G2", Queue::AsyncCompute, "C2"));
    EXPECT_FALSE(hasSync(g, Queue::Graphics, "G1", Queue::AsyncCompute, "C3"));
    // Values increase along the signalling queue.
    for (const SyncPoint& s : g.syncPoints())
    {
        if (s.signalQueue == Queue::Graphics)
        {
            EXPECT_EQ(s.signalValue, s.afterPass == g.findPass("G1") ? 1u : 2u);
        }
    }
    // The compute writes to imports: the epilogue waits for the last of them.
    EXPECT_TRUE(hasSync(g, Queue::AsyncCompute, "C3", Queue::Graphics, "epilogue"));
    EXPECT_TRUE(g.happensBefore(g.findPass("G1"), g.findPass("C3")));
}

TEST(RenderGraphQueues, ALaterWaitCoversEarlierProducers)
{
    // C1 needs G2 (the later producer), C2 needs G1: one wait (after G2) serves both.
    RenderGraph g;
    g.setOptions(async());
    g.beginFrame(256, 256);
    TextureHandle x, y;
    pass(g, "G1", [&](PassBuilder& b) { x = b.write(b.create("X", tex())); });
    pass(g, "G2", [&](PassBuilder& b) { y = b.write(b.create("Y", tex())); });
    const BufferHandle o1 = sinkBuffer(g, "O1"), o2 = sinkBuffer(g, "O2");
    pass(g, "C1",
         [&](PassBuilder& b)
         {
             b.queue(Queue::AsyncCompute);
             b.read(y, Use::ComputeSrv);
             b.write(o1, Use::Uav);
         });
    pass(g, "C2",
         [&](PassBuilder& b)
         {
             b.queue(Queue::AsyncCompute);
             b.read(x, Use::ComputeSrv);
             b.write(o2, Use::Uav);
         });
    ASSERT_TRUE(compiles(g));
    uint32_t waitsOnCompute = 0;
    for (const SyncPoint& s : g.syncPoints())
        waitsOnCompute += s.waitQueue == Queue::AsyncCompute ? 1u : 0u;
    EXPECT_EQ(waitsOnCompute, 1u);
    EXPECT_TRUE(hasSync(g, Queue::Graphics, "G2", Queue::AsyncCompute, "C1"));
    EXPECT_TRUE(g.happensBefore(g.findPass("G1"), g.findPass("C2")));
}

TEST(RenderGraphQueues, IndependentWorkOverlaps)
{
    RenderGraph g;
    g.setOptions(async());
    const TextureHandle bb = [&]
    {
        g.beginFrame(256, 256);
        return g.importBackBuffer(nullptr, 256, 256, kBgra8);
    }();
    const BufferHandle sim = sinkBuffer(g, "Sim");
    TextureHandle      x;
    pass(g, "G1", [&](PassBuilder& b) { x = b.write(b.create("X", tex())); });
    pass(g, "Particles",
         [&](PassBuilder& b)
         {
             b.queue(Queue::AsyncCompute);
             b.write(sim, Use::Uav);
         });
    pass(g, "G2",
         [&](PassBuilder& b)
         {
             b.read(x);
             b.write(bb);
         });
    ASSERT_TRUE(compiles(g));
    const uint32_t g1 = g.findPass("G1"), p = g.findPass("Particles"), g2 = g.findPass("G2");
    EXPECT_FALSE(g.happensBefore(g1, p));
    EXPECT_FALSE(g.happensBefore(p, g1));
    EXPECT_FALSE(g.happensBefore(p, g2));
    ASSERT_EQ(g.syncPoints().size(), 1u); // only the epilogue joins the compute queue
    EXPECT_TRUE(hasSync(g, Queue::AsyncCompute, "Particles", Queue::Graphics, "epilogue"));
    Recorder rec(g);
    ASSERT_TRUE(g.execute(&rec));
    EXPECT_EQ(rec.events.back(), "W Graphics<-AsyncCompute 1");
}

TEST(RenderGraphQueues, ImportInAGraphicsOnlyLayoutIsTransitionedInThePrologue)
{
    RenderGraph g;
    g.setOptions(async());
    g.beginFrame(128, 128);
    const TextureHandle shadow = g.importTexture("Shadow", absTex(128, 128, kD32), nullptr, Layout::DepthStencilWrite, Layout::DepthStencilWrite);
    const BufferHandle  out    = sinkBuffer(g, "Out");
    pass(g, "Cull",
         [&](PassBuilder& b)
         {
             b.queue(Queue::AsyncCompute);
             b.read(shadow, Use::ComputeSrv);
             b.write(out, Use::Uav);
         });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierRec> sb = barriersFor(g, "Shadow");
    ASSERT_EQ(sb.size(), 2u);
    EXPECT_EQ(sb[0].placement, BarrierPlacement::Prologue);
    EXPECT_EQ(sb[0].queue, Queue::Graphics);
    EXPECT_EQ(sb[0].before.layout, Layout::DepthStencilWrite);
    EXPECT_EQ(sb[0].after.layout, Layout::ShaderResource);
    // Back to its final layout on graphics, after compute is done with it.
    EXPECT_EQ(sb[1].placement, BarrierPlacement::Epilogue);
    EXPECT_EQ(sb[1].after.layout, Layout::DepthStencilWrite);
    EXPECT_TRUE(hasSync(g, Queue::Graphics, "prologue", Queue::AsyncCompute, "Cull"));
    EXPECT_TRUE(hasSync(g, Queue::AsyncCompute, "Cull", Queue::Graphics, "epilogue"));
    Recorder rec(g);
    ASSERT_TRUE(g.execute(&rec));
    const std::vector<std::string> expected = {
        "B prologue prologue Graphics [Shadow]", "S Graphics 1", "W AsyncCompute<-Graphics 1", "P Cull", "E Cull", "S AsyncCompute 1", "W Graphics<-AsyncCompute 1",
        "B epilogue epilogue Graphics [Shadow]",
    };
    EXPECT_EQ(rec.events, expected);
}

TEST(RenderGraphQueues, FinalLayoutLegalOnComputeIsSetThereAfterTheLastUse)
{
    RenderGraph g;
    g.setOptions(async());
    g.beginFrame(128, 128);
    const TextureHandle vol = g.importTexture("Volume", absTex(64, 64), nullptr, Layout::ShaderResource, Layout::ShaderResource);
    pass(g, "Inject",
         [&](PassBuilder& b)
         {
             b.queue(Queue::AsyncCompute);
             b.write(vol, Use::Uav);
         });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierRec> vb = barriersFor(g, "Volume");
    ASSERT_EQ(vb.size(), 2u);
    EXPECT_EQ(vb[0].queue, Queue::AsyncCompute);
    EXPECT_EQ(vb[0].before.layout, Layout::ShaderResource);
    EXPECT_FALSE(vb[0].discard);
    EXPECT_EQ(vb[1].queue, Queue::AsyncCompute);
    EXPECT_EQ(vb[1].placement, BarrierPlacement::AfterPass);
    EXPECT_EQ(vb[1].after.layout, Layout::ShaderResource);
    EXPECT_TRUE(hasSync(g, Queue::AsyncCompute, "Inject", Queue::Graphics, "epilogue"));
}

TEST(RenderGraphQueues, SameLayoutAcrossQueuesNeedsOnlyTheFence)
{
    RenderGraph g;
    g.setOptions(async());
    g.beginFrame(128, 128);
    const BufferHandle out = sinkBuffer(g, "Out");
    TextureHandle      t;
    pass(g, "Sim",
         [&](PassBuilder& b)
         {
             b.queue(Queue::AsyncCompute);
             t = b.write(b.create("Field", tex(kR32F)), Use::Uav);
         });
    pass(g, "Splat",
         [&](PassBuilder& b)
         {
             t = b.readWrite(t, Use::Uav); // graphics, still UNORDERED_ACCESS
             b.write(out, Use::Uav);
         });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierRec> fb = barriersFor(g, "Field");
    ASSERT_EQ(fb.size(), 1u); // only the first-use discard
    EXPECT_TRUE(fb[0].discard);
    EXPECT_TRUE(hasSync(g, Queue::AsyncCompute, "Sim", Queue::Graphics, "Splat"));
}

TEST(RenderGraphQueues, ComputeReadsAfterAGraphicsReadWaitForTheTransition)
{
    // X goes RT -> SR before G2 (graphics, the group's first reader); C1 shares the read group and
    // must wait for that transition, not just for G1.
    RenderGraph g;
    g.setOptions(async());
    g.beginFrame(128, 128);
    const BufferHandle o1 = sinkBuffer(g, "O1"), o2 = sinkBuffer(g, "O2");
    TextureHandle      x;
    pass(g, "G1", [&](PassBuilder& b) { x = b.write(b.create("X", tex())); });
    pass(g, "G2",
         [&](PassBuilder& b)
         {
             b.read(x, Use::PixelSrv);
             b.write(o1, Use::CopyDst);
         });
    pass(g, "C1",
         [&](PassBuilder& b)
         {
             b.queue(Queue::AsyncCompute);
             b.read(x, Use::ComputeSrv);
             b.write(o2, Use::Uav);
         });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierRec> xb = barriersFor(g, "X");
    ASSERT_EQ(xb.size(), 2u);
    EXPECT_EQ(xb[1].pass, g.findPass("G2"));
    EXPECT_EQ(xb[1].queue, Queue::Graphics);
    EXPECT_TRUE(hasSync(g, Queue::Graphics, "G2", Queue::AsyncCompute, "C1"));
}

TEST(RenderGraphQueues, ComputeFirstInAReadGroupMovesTheTransitionToGraphicsAfterPass)
{
    RenderGraph g;
    g.setOptions(async());
    g.beginFrame(128, 128);
    const BufferHandle o1 = sinkBuffer(g, "O1"), o2 = sinkBuffer(g, "O2");
    TextureHandle      x;
    pass(g, "G1", [&](PassBuilder& b) { x = b.write(b.create("X", tex())); });
    pass(g, "C1",
         [&](PassBuilder& b)
         {
             b.queue(Queue::AsyncCompute);
             b.read(x, Use::ComputeSrv);
             b.write(o2, Use::Uav);
         });
    pass(g, "G2",
         [&](PassBuilder& b)
         {
             b.read(x, Use::PixelSrv);
             b.write(o1, Use::CopyDst);
         });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierRec> xb = barriersFor(g, "X");
    ASSERT_EQ(xb.size(), 2u);
    EXPECT_EQ(xb[1].placement, BarrierPlacement::AfterPass);
    EXPECT_EQ(xb[1].pass, g.findPass("G1"));
    EXPECT_EQ(xb[1].after.sync, Sync::PixelShading); // G2's read, on the same queue
    EXPECT_EQ(xb[1].after.layout, Layout::ShaderResource);
    EXPECT_TRUE(hasSync(g, Queue::Graphics, "G1", Queue::AsyncCompute, "C1"));
}

TEST(RenderGraphQueues, LayoutChangeLegalOnComputeHappensOnCompute)
{
    RenderGraph g;
    g.setOptions(async());
    g.beginFrame(128, 128);
    const BufferHandle out = sinkBuffer(g, "Out");
    TextureHandle      t;
    pass(g, "Write", [&](PassBuilder& b) { t = b.write(b.create("T", tex()), Use::Uav); });
    pass(g, "Read",
         [&](PassBuilder& b)
         {
             b.queue(Queue::AsyncCompute);
             b.read(t, Use::ComputeSrv);
             b.write(out, Use::Uav);
         });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierRec> tb = barriersFor(g, "T");
    ASSERT_EQ(tb.size(), 2u);
    EXPECT_EQ(tb[1].queue, Queue::AsyncCompute);
    EXPECT_EQ(tb[1].pass, g.findPass("Read"));
    EXPECT_EQ(tb[1].before.sync, Sync::None); // graphics' work is behind the fence
    EXPECT_EQ(tb[1].before.layout, Layout::UnorderedAccess);
    EXPECT_EQ(tb[1].after.layout, Layout::ShaderResource);
}

TEST(RenderGraphQueues, GraphicsOnlyUseOnComputeIsAnErrorEvenWhenForced)
{
    for (bool on : { false, true })
    {
        RenderGraph g;
        g.setOptions(async(on));
        g.beginFrame(64, 64);
        pass(g, "Bad",
             [&](PassBuilder& b)
             {
                 b.queue(Queue::AsyncCompute);
                 b.write(b.create("T", tex()), Use::RenderTarget);
                 b.flags(PassFlags::SideEffect);
             });
        EXPECT_FALSE(g.compile());
        EXPECT_TRUE(g.hasError(ErrorCode::IllegalOnQueue)) << errorText(g);
    }
}

TEST(RenderGraphQueues, OwnBarriersPassesMustBeGraphics)
{
    RenderGraph g;
    g.setOptions(async());
    g.beginFrame(64, 64);
    pass(g, "Legacy",
         [&](PassBuilder& b)
         {
             b.queue(Queue::AsyncCompute);
             b.flags(PassFlags::OwnBarriers | PassFlags::SideEffect);
         });
    EXPECT_FALSE(g.compile());
    EXPECT_TRUE(g.hasError(ErrorCode::OwnBarriersOnCompute));
}

TEST(RenderGraphQueues, PingPongAcrossQueuesStaysDeadlockFree)
{
    // G0 -> C0 -> G1 -> C1 -> G2 through one UAV texture, plus independent work on both queues.
    RenderGraph g;
    g.setOptions(async());
    g.beginFrame(256, 256);
    const BufferHandle  side = sinkBuffer(g, "Side");
    const TextureHandle bb   = g.importBackBuffer(nullptr, 256, 256, kBgra8);
    TextureHandle       t;
    pass(g, "G0", [&](PassBuilder& b) { t = b.write(b.create("T", tex()), Use::Uav); });
    pass(g, "C0",
         [&](PassBuilder& b)
         {
             b.queue(Queue::AsyncCompute);
             t = b.readWrite(t, Use::Uav);
         });
    pass(g, "Gx",
         [&](PassBuilder& b)
         {
             b.write(b.create("Unrelated", tex()), Use::RenderTarget);
             b.flags(PassFlags::NeverCull);
         });
    pass(g, "G1", [&](PassBuilder& b) { t = b.readWrite(t, Use::Uav); });
    pass(g, "Cx",
         [&](PassBuilder& b)
         {
             b.queue(Queue::AsyncCompute);
             b.write(side, Use::Uav);
         });
    pass(g, "C1",
         [&](PassBuilder& b)
         {
             b.queue(Queue::AsyncCompute);
             t = b.readWrite(t, Use::Uav);
         });
    pass(g, "G2",
         [&](PassBuilder& b)
         {
             b.read(t, Use::PixelSrv);
             b.write(bb);
         });
    ASSERT_TRUE(compiles(g)); // validateCompiled simulates both queues: no deadlock
    EXPECT_TRUE(hasSync(g, Queue::Graphics, "G0", Queue::AsyncCompute, "C0"));
    EXPECT_TRUE(hasSync(g, Queue::AsyncCompute, "C0", Queue::Graphics, "G1"));
    EXPECT_TRUE(hasSync(g, Queue::Graphics, "G1", Queue::AsyncCompute, "C1"));
    EXPECT_TRUE(hasSync(g, Queue::AsyncCompute, "C1", Queue::Graphics, "G2"));
    // Signals on one queue have increasing values in queue order.
    for (Queue q : { Queue::Graphics, Queue::AsyncCompute })
    {
        uint32_t lastValue = 0;
        int64_t  lastPos   = -1;
        for (const SyncPoint& s : g.syncPoints())
        {
            if (s.signalQueue != q || s.afterPass >= g.passCount())
                continue;
            const int64_t pos = g.passOrder(s.afterPass);
            if (s.signalValue > lastValue)
            {
                EXPECT_GT(pos, lastPos);
                lastValue = s.signalValue;
                lastPos   = pos;
            }
        }
    }
    Recorder rec(g);
    ASSERT_TRUE(g.execute(&rec));
    // Each signal value is signalled exactly once.
    for (const std::string& e : rec.events)
    {
        if (e.rfind("S ", 0) != 0)
            continue;
        int n = 0;
        for (const std::string& other : rec.events)
            n += other == e ? 1 : 0;
        EXPECT_EQ(n, 1) << e;
    }
}

TEST(RenderGraphQueues, ReenablingAsyncComputeRecompiles)
{
    RenderGraph g;
    buildSsao(g, false);
    ASSERT_TRUE(compiles(g));
    buildSsao(g, true);
    ASSERT_TRUE(compiles(g));
    EXPECT_FALSE(g.compiledFromCache());
    EXPECT_EQ(g.passQueue(g.findPass("SSAO")), Queue::AsyncCompute);
    buildSsao(g, false);
    ASSERT_TRUE(compiles(g));
    EXPECT_FALSE(g.compiledFromCache());
    EXPECT_TRUE(g.syncPoints().empty());
}
