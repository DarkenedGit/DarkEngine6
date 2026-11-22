#include "RenderGraphTestUtil.h"

#include "Core/Log.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

// RenderGraph core (plan Phase 3): setup/execute API, versioning, culling, ordering, errors without
// exceptions, the plan cache, arena and blackboard. Pure CPU; nothing here touches D3D12.

using namespace Dark;
using namespace Dark::Rg;
using namespace Dark::Rg::Test;

namespace
{
    int g_graphErrorLogs = 0;

    void countGraphErrors(LogLevel level, LogCategory category, const char* message)
    {
        if (level == LogLevel::Error && category == LogCategory::Render && std::strstr(message, "RenderGraph:") != nullptr)
            ++g_graphErrorLogs;
    }

    struct LogCapture
    {
        explicit LogCapture(Log::CaptureFn fn) :
            m_fn(fn)
        {
            Log::addCapture(m_fn);
        }
        ~LogCapture()
        {
            Log::removeCapture(m_fn);
        }
        Log::CaptureFn m_fn;
    };

    // A frame with a back buffer: the usual sink.
    TextureHandle frame(RenderGraph& g, uint32_t w = 1920, uint32_t h = 1080)
    {
        g.beginFrame(w, h);
        return g.importBackBuffer(nullptr, w, h, kBgra8);
    }

    std::vector<std::string> orderNames(const RenderGraph& g)
    {
        std::vector<std::string> out;
        for (const uint32_t p : g.order())
            out.push_back(g.passName(p));
        return out;
    }
} // namespace

// ---------------------------------------------------------------- basics

TEST(RenderGraph, EmptyGraphCompilesAndExecutes)
{
    RenderGraph g;
    g.beginFrame(64, 64);
    EXPECT_TRUE(compiles(g));
    EXPECT_TRUE(g.order().empty());
    EXPECT_TRUE(g.barriers().empty());
    EXPECT_TRUE(g.syncPoints().empty());
    EXPECT_TRUE(g.execute());
}

TEST(RenderGraph, HandlesAreInvalidByDefault)
{
    TextureHandle t;
    BufferHandle  b;
    EXPECT_FALSE(t.valid());
    EXPECT_FALSE(b.valid());
    EXPECT_EQ(t.index, kInvalidIndex);
}

TEST(RenderGraph, SetupRunsImmediatelyExecuteRunsLaterWithItsData)
{
    struct Data
    {
        TextureHandle color;
        int           value = 0;
    };
    RenderGraph         g;
    const TextureHandle bb       = frame(g);
    bool                setupRan = false;
    int                 seen     = 0;
    TextureHandle       seenHandle;
    g.addPass<Data>(
        "Draw",
        [&](PassBuilder& b, Data& d)
        {
            setupRan = true;
            d.color  = b.write(bb, Use::RenderTarget);
            d.value  = 42;
        },
        [&](PassContext& ctx, const Data& d)
        {
            seen       = d.value;
            seenHandle = d.color;
            EXPECT_STREQ(ctx.passName(), "Draw");
            EXPECT_EQ(ctx.queue(), Queue::Graphics);
            EXPECT_EQ(ctx.width(d.color), 1920u);
            EXPECT_EQ(ctx.height(d.color), 1080u);
        });
    EXPECT_TRUE(setupRan);
    EXPECT_EQ(seen, 0);
    ASSERT_TRUE(compiles(g));
    EXPECT_EQ(seen, 0);
    ASSERT_TRUE(g.execute());
    EXPECT_EQ(seen, 42);
    EXPECT_EQ(seenHandle.index, bb.index);
    EXPECT_EQ(seenHandle.version, 1);
}

TEST(RenderGraph, WritesBumpTheVersion)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       t0, t1, t2;
    pass(g, "A",
         [&](PassBuilder& b)
         {
             t0 = b.create("T", tex());
             t1 = b.write(t0, Use::RenderTarget);
         });
    pass(g, "B", [&](PassBuilder& b) { t2 = b.readWrite(t1, Use::Uav); });
    pass(g, "C",
         [&](PassBuilder& b)
         {
             b.read(t2, Use::PixelSrv);
             b.write(bb, Use::RenderTarget);
         });
    EXPECT_EQ(t0.version, 0);
    EXPECT_EQ(t1.version, 1);
    EXPECT_EQ(t2.version, 2);
    EXPECT_EQ(t0.index, t2.index);
    ASSERT_TRUE(compiles(g));
    EXPECT_EQ(g.resourceInfo(t0.index).versions, 3);
    EXPECT_EQ(orderNames(g), (std::vector<std::string>{ "A", "B", "C" }));
}

TEST(RenderGraph, RelativeAndAbsoluteSizesResolve)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g, 1000, 600);
    TextureHandle       half, fixed, odd;
    pass(g, "A",
         [&](PassBuilder& b)
         {
             half  = b.write(b.create("Half", tex(kRgba16F, 0.5f)));
             fixed = b.write(b.create("Fixed", absTex(256, 128)));
             odd   = b.write(b.create("Tiny", tex(kRgba16F, 0.0001f)));
         });
    pass(g, "B",
         [&](PassBuilder& b)
         {
             b.read(half);
             b.read(fixed);
             b.read(odd);
             b.write(bb);
         });
    ASSERT_TRUE(compiles(g));
    EXPECT_EQ(g.resourceInfo(half.index).width, 500u);
    EXPECT_EQ(g.resourceInfo(half.index).height, 300u);
    EXPECT_EQ(g.resourceInfo(fixed.index).width, 256u);
    EXPECT_EQ(g.resourceInfo(fixed.index).height, 128u);
    EXPECT_GE(g.resourceInfo(odd.index).width, 1u); // never zero-sized
    EXPECT_GE(g.resourceInfo(odd.index).height, 1u);
}

// ---------------------------------------------------------------- culling

TEST(RenderGraph, PassesWithNoConsumerAreCulled)
{
    RenderGraph         g;
    const TextureHandle bb   = frame(g);
    int                 ranA = 0, ranUnused = 0, ranB = 0;
    TextureHandle       t;
    g.addPass<NoData>("A", [&](PassBuilder& b, NoData&) { t = b.write(b.create("T", tex())); }, [&](PassContext&, const NoData&) { ++ranA; });
    g.addPass<NoData>("Unused", [&](PassBuilder& b, NoData&) { b.write(b.create("U", tex())); }, [&](PassContext&, const NoData&) { ++ranUnused; });
    g.addPass<NoData>(
        "B",
        [&](PassBuilder& b, NoData&)
        {
            b.read(t);
            b.write(bb);
        },
        [&](PassContext&, const NoData&) { ++ranB; });
    ASSERT_TRUE(compiles(g));
    EXPECT_FALSE(g.passCulled(g.findPass("A")));
    EXPECT_TRUE(g.passCulled(g.findPass("Unused")));
    EXPECT_FALSE(g.passCulled(g.findPass("B")));
    EXPECT_EQ(g.passOrder(g.findPass("Unused")), kNoPass);
    EXPECT_FALSE(g.resourceInfo(g.findResource("U")).used);
    EXPECT_EQ(orderNames(g), (std::vector<std::string>{ "A", "B" }));
    ASSERT_TRUE(g.execute());
    EXPECT_EQ(ranA, 1);
    EXPECT_EQ(ranUnused, 0);
    EXPECT_EQ(ranB, 1);
}

TEST(RenderGraph, CullingIsTransitive)
{
    RenderGraph g;
    frame(g);
    TextureHandle a, b2;
    pass(g, "P1", [&](PassBuilder& b) { a = b.write(b.create("A", tex())); });
    pass(g, "P2",
         [&](PassBuilder& b)
         {
             b.read(a);
             b2 = b.write(b.create("B", tex()));
         });
    pass(g, "P3", [&](PassBuilder& b) { b.read(b2); }); // reads only: nothing escapes
    ASSERT_TRUE(compiles(g));
    EXPECT_TRUE(g.passCulled(0));
    EXPECT_TRUE(g.passCulled(1));
    EXPECT_TRUE(g.passCulled(2));
    EXPECT_TRUE(g.order().empty());
    EXPECT_TRUE(g.barriers().empty());
}

TEST(RenderGraph, OnlyTheConsumedVersionChainSurvives)
{
    // A writes T; B overwrites T (writes depend on the previous version); C reads v1 only.
    // Nothing reads B's version and T is transient, so B is culled; A survives for C.
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       v1;
    pass(g, "A", [&](PassBuilder& b) { v1 = b.write(b.create("T", tex())); });
    pass(g, "B", [&](PassBuilder& b) { b.write(v1); });
    pass(g, "C",
         [&](PassBuilder& b)
         {
             b.read(v1);
             b.write(bb);
         });
    ASSERT_TRUE(compiles(g));
    EXPECT_FALSE(g.passCulled(0));
    EXPECT_TRUE(g.passCulled(1));
    EXPECT_FALSE(g.passCulled(2));
}

TEST(RenderGraph, SideEffectAndNeverCullPassesSurvive)
{
    RenderGraph g;
    frame(g);
    pass(g, "Readback",
         [&](PassBuilder& b)
         {
             b.flags(PassFlags::SideEffect);
             b.write(b.create("T", tex()));
         });
    pass(g, "Debug", [&](PassBuilder& b) { b.flags(PassFlags::NeverCull); });
    pass(g, "Dead", [&](PassBuilder& b) { b.write(b.create("D", tex())); });
    ASSERT_TRUE(compiles(g));
    EXPECT_FALSE(g.passCulled(0));
    EXPECT_FALSE(g.passCulled(1));
    EXPECT_TRUE(g.passCulled(2));
}

TEST(RenderGraph, WritingAnImportOrHistoryIsASink)
{
    RenderGraph g;
    g.beginFrame(128, 128);
    const TextureHandle shadow = g.importTexture("ShadowAtlas", absTex(512, 512, kD32), nullptr, Layout::ShaderResource, Layout::ShaderResource);
    const BufferHandle  counts = g.importBuffer("Counters", buf(256), nullptr);
    const HistoryPair   taa    = g.history("TAA", tex());
    pass(g, "Shadows", [&](PassBuilder& b) { b.write(shadow, Use::DepthWrite); });
    pass(g, "Count", [&](PassBuilder& b) { b.write(counts, Use::Uav); });
    pass(g, "Resolve", [&](PassBuilder& b) { b.write(taa.curr, Use::RenderTarget); });
    pass(g, "ReadOnlyImport", [&](PassBuilder& b) { b.read(shadow, Use::PixelSrv); });
    ASSERT_TRUE(compiles(g));
    EXPECT_FALSE(g.passCulled(0));
    EXPECT_FALSE(g.passCulled(1));
    EXPECT_FALSE(g.passCulled(2));
    EXPECT_TRUE(g.passCulled(3));
}

TEST(RenderGraph, NoCullOptionKeepsEverything)
{
    RenderGraph g;
    frame(g);
    CompileOptions o;
    o.noCull = true;
    g.setOptions(o);
    pass(g, "Dead1", [&](PassBuilder& b) { b.write(b.create("A", tex())); });
    pass(g, "Dead2", [&](PassBuilder&) {});
    ASSERT_TRUE(compiles(g));
    EXPECT_FALSE(g.passCulled(0));
    EXPECT_FALSE(g.passCulled(1));
    EXPECT_EQ(g.order().size(), 2u);
}

// ---------------------------------------------------------------- ordering

TEST(RenderGraph, IndependentPassesKeepInsertionOrder)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       a, b1, c;
    pass(g, "C", [&](PassBuilder& b) { c = b.write(b.create("Tc", tex())); });
    pass(g, "A", [&](PassBuilder& b) { a = b.write(b.create("Ta", tex())); });
    pass(g, "B", [&](PassBuilder& b) { b1 = b.write(b.create("Tb", tex())); });
    pass(g, "Final",
         [&](PassBuilder& b)
         {
             b.read(a);
             b.read(b1);
             b.read(c);
             b.write(bb);
         });
    ASSERT_TRUE(compiles(g));
    EXPECT_EQ(orderNames(g), (std::vector<std::string>{ "C", "A", "B", "Final" }));
    for (uint32_t i = 0; i < g.order().size(); ++i)
        EXPECT_EQ(g.passOrder(g.order()[i]), i);
}

TEST(RenderGraph, ReaderOfAnOldVersionIsOrderedBeforeTheOverwrite)
{
    // Recorded as: A writes T(v1); B overwrites T (v2); C reads v1 late; D reads v2.
    // C must run before B (write-after-read), so the sort moves C ahead of B.
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       v1, v2;
    pass(g, "A", [&](PassBuilder& b) { v1 = b.write(b.create("T", tex())); });
    pass(g, "B", [&](PassBuilder& b) { v2 = b.write(v1); });
    pass(g, "C",
         [&](PassBuilder& b)
         {
             b.read(v1);
             b.write(bb);
         });
    pass(g, "D",
         [&](PassBuilder& b)
         {
             b.read(v2);
             b.write(b.create("Out", tex()));
             b.flags(PassFlags::SideEffect);
         });
    ASSERT_TRUE(compiles(g));
    const uint32_t B = g.findPass("B"), C = g.findPass("C"), D = g.findPass("D");
    EXPECT_LT(g.passOrder(C), g.passOrder(B));
    EXPECT_LT(g.passOrder(B), g.passOrder(D));
    EXPECT_TRUE(g.happensBefore(C, B));
    EXPECT_FALSE(g.happensBefore(B, C));
}

TEST(RenderGraph, CycleIsReportedNotCrashed)
{
    // A reads U.v1 and writes T.v1->v2... built so that: B (writer of U.v1) needs T.v1 read by...
    // Simplest WAR cycle: P1 writes T v1. P2 writes U v1. P3 reads T v1 and overwrites U (v2).
    // P4 reads U v1 and overwrites T (v2). WAR: P3 before P4 (P3 reads T v1, P4 overwrites T)
    // and P4 before P3 (P4 reads U v1, P3 overwrites U). Both overwrites are sinks via imports.
    RenderGraph g;
    g.beginFrame(64, 64);
    const TextureHandle t = g.importTexture("T", absTex(64, 64), nullptr, Layout::ShaderResource, Layout::ShaderResource);
    const TextureHandle u = g.importTexture("U", absTex(64, 64), nullptr, Layout::ShaderResource, Layout::ShaderResource);
    TextureHandle       t1, u1;
    pass(g, "P1", [&](PassBuilder& b) { t1 = b.write(t); });
    pass(g, "P2", [&](PassBuilder& b) { u1 = b.write(u); });
    pass(g, "P3",
         [&](PassBuilder& b)
         {
             b.read(t1);
             b.write(u1);
         });
    pass(g, "P4",
         [&](PassBuilder& b)
         {
             b.read(u1);
             b.write(t1);
         });
    EXPECT_FALSE(g.compile());
    EXPECT_TRUE(g.hasError(ErrorCode::Cycle)) << errorText(g);
    bool namesPasses = false;
    for (const Error& e : g.errors())
        if (e.code == ErrorCode::Cycle)
            namesPasses = e.message.find("P3") != std::string::npos && e.message.find("P4") != std::string::npos;
    EXPECT_TRUE(namesPasses) << errorText(g);
    EXPECT_FALSE(g.compiledOk());
    EXPECT_FALSE(g.execute());
    EXPECT_TRUE(g.hasError(ErrorCode::NotCompiled));
}

// ---------------------------------------------------------------- errors (no exceptions)

TEST(RenderGraph, ReadBeforeWriteIsAnError)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    pass(g, "A",
         [&](PassBuilder& b)
         {
             const TextureHandle t = b.create("T", tex());
             const TextureHandle r = b.read(t);
             EXPECT_FALSE(r.valid());
             b.write(bb);
         });
    EXPECT_FALSE(g.compile());
    EXPECT_EQ(countErrors(g, ErrorCode::ReadBeforeWrite), 1u);
    EXPECT_FALSE(g.execute());
}

TEST(RenderGraph, ReadWriteOfAnUnwrittenTransientIsReadBeforeWrite)
{
    RenderGraph g;
    frame(g);
    pass(g, "A",
         [&](PassBuilder& b)
         {
             const BufferHandle h = b.create("B", buf(64));
             EXPECT_FALSE(b.readWrite(h, Use::Uav).valid());
         });
    EXPECT_FALSE(g.compile());
    EXPECT_TRUE(g.hasError(ErrorCode::ReadBeforeWrite));
}

TEST(RenderGraph, TwoWritersOfOneVersionIsAStaleWrite)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       v1;
    pass(g, "A", [&](PassBuilder& b) { v1 = b.write(b.create("T", tex())); });
    pass(g, "B", [&](PassBuilder& b) { b.write(v1); });
    pass(g, "C", [&](PassBuilder& b) { EXPECT_FALSE(b.write(v1).valid()); }); // B already wrote v2
    pass(g, "D",
         [&](PassBuilder& b)
         {
             b.read(v1);
             b.write(bb);
         });
    EXPECT_FALSE(g.compile());
    ASSERT_EQ(countErrors(g, ErrorCode::StaleWrite), 1u);
    for (const Error& e : g.errors())
        if (e.code == ErrorCode::StaleWrite)
        {
            EXPECT_NE(e.message.find("'B'"), std::string::npos) << e.message;
            EXPECT_EQ(e.pass, g.findPass("C"));
        }
}

TEST(RenderGraph, InvalidAndMismatchedHandlesAreErrors)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    BufferHandle        realBuffer;
    pass(g, "A",
         [&](PassBuilder& b)
         {
             realBuffer = b.write(b.create("Buf", buf(64)), Use::Uav);
             EXPECT_FALSE(b.read(TextureHandle{}).valid());                        // default handle
             EXPECT_FALSE(b.read(TextureHandle{ 999, 0 }).valid());                // out of range
             EXPECT_FALSE(b.read(TextureHandle{ realBuffer.index, 0 }).valid());   // buffer as texture
             EXPECT_FALSE(b.read(TextureHandle{ bb.index, 7 }).valid());           // version from the future
             EXPECT_FALSE(b.write(BufferHandle{ bb.index, 0 }, Use::Uav).valid()); // texture as buffer
         });
    EXPECT_FALSE(g.compile());
    EXPECT_EQ(countErrors(g, ErrorCode::InvalidHandle), 5u) << errorText(g);
}

TEST(RenderGraph, HandleFromAnEarlierFrameIsAnError)
{
    RenderGraph   g;
    TextureHandle old;
    {
        const TextureHandle bb = frame(g);
        pass(g, "A",
             [&](PassBuilder& b)
             {
                 old = b.write(b.create("T1", tex()));
                 b.create("T2", tex());
                 b.write(bb);
             });
        ASSERT_TRUE(compiles(g));
    }
    frame(g); // resources reset: T1/T2 no longer exist; old.index is past the end
    pass(g, "B", [&](PassBuilder& b) { EXPECT_FALSE(b.read(TextureHandle{ 2, 1 }).valid()); });
    EXPECT_FALSE(g.compile());
    EXPECT_TRUE(g.hasError(ErrorCode::InvalidHandle));
}

TEST(RenderGraph, UseErrors)
{
    RenderGraph g;
    frame(g);
    pass(g, "A",
         [&](PassBuilder& b)
         {
             const TextureHandle t  = b.create("T", tex());
             const BufferHandle  bu = b.create("B", buf(64));
             EXPECT_FALSE(b.write(t, Use::PixelSrv).valid());      // NotAWriteUse
             EXPECT_FALSE(b.write(bu, Use::IndirectArgs).valid()); // NotAWriteUse
             EXPECT_FALSE(b.write(bu, Use::RenderTarget).valid()); // WrongUseForKind
             EXPECT_FALSE(b.write(t, Use::Present).valid());       // PresentUse
             const TextureHandle t1 = b.write(t, Use::RenderTarget);
             const BufferHandle  b1 = b.write(bu, Use::Uav);
             EXPECT_FALSE(b.read(t1, Use::IndirectArgs).valid()); // WrongUseForKind
             EXPECT_FALSE(b.read(t1, Use::VertexIndex).valid());  // WrongUseForKind
             EXPECT_FALSE(b.read(t1, Use::DepthWrite).valid());   // NotAReadUse
             EXPECT_FALSE(b.read(b1, Use::DepthRead).valid());    // WrongUseForKind
             EXPECT_FALSE(b.read(t1, Use::Present).valid());      // PresentUse
         });
    EXPECT_FALSE(g.compile());
    EXPECT_EQ(countErrors(g, ErrorCode::NotAWriteUse), 2u) << errorText(g);
    EXPECT_EQ(countErrors(g, ErrorCode::WrongUseForKind), 4u) << errorText(g);
    EXPECT_EQ(countErrors(g, ErrorCode::NotAReadUse), 1u) << errorText(g);
    EXPECT_EQ(countErrors(g, ErrorCode::PresentUse), 2u) << errorText(g);
}

TEST(RenderGraph, IncompatibleUsesInOnePassAreAnError)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       t;
    pass(g, "A", [&](PassBuilder& b) { t = b.write(b.create("T", tex())); });
    pass(g, "Feedback",
         [&](PassBuilder& b)
         {
             b.read(t, Use::PixelSrv);
             b.write(t, Use::RenderTarget); // sampling and rendering one texture at once
             b.write(bb);
         });
    EXPECT_FALSE(g.compile());
    EXPECT_TRUE(g.hasError(ErrorCode::IncompatibleUses)) << errorText(g);
}

TEST(RenderGraph, CompatibleReadsInOnePassMerge)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       t, d;
    pass(g, "A",
         [&](PassBuilder& b)
         {
             t = b.write(b.create("T", tex()));
             d = b.write(b.create("Depth", tex(kD32)), Use::DepthWrite);
         });
    pass(g, "B",
         [&](PassBuilder& b)
         {
             b.read(t, Use::PixelSrv);
             b.read(t, Use::CopySrc); // SRV + copy source = GENERIC_READ
             b.read(d, Use::DepthRead);
             b.read(d, Use::PixelSrv); // depth test + sample = DIRECT_QUEUE_GENERIC_READ
             b.write(bb);
         });
    ASSERT_TRUE(compiles(g));
    const std::vector<BarrierRec> tb = barriersFor(g, "T");
    ASSERT_EQ(tb.size(), 2u);
    EXPECT_EQ(tb[1].after.layout, Layout::GenericRead);
    EXPECT_TRUE(any(tb[1].after.access & Access::ShaderResource));
    EXPECT_TRUE(any(tb[1].after.access & Access::CopySource));
    const std::vector<BarrierRec> db = barriersFor(g, "Depth");
    ASSERT_EQ(db.size(), 2u);
    EXPECT_EQ(db[1].after.layout, Layout::DirectQueueGenericRead);
    EXPECT_TRUE(validateBarrier(db[1]));
}

TEST(RenderGraph, ErrorsAreLoggedOnceAcrossFrames)
{
    RenderGraph      g;
    const LogCapture cap(&countGraphErrors);
    g_graphErrorLogs = 0;
    for (int f = 0; f < 5; ++f)
    {
        frame(g);
        pass(g, "Bad", [&](PassBuilder& b) { b.read(b.create("T", tex())); });
        EXPECT_FALSE(g.compile());
        EXPECT_EQ(countErrors(g, ErrorCode::ReadBeforeWrite), 1u); // errors() is per frame
    }
    EXPECT_EQ(g_graphErrorLogs, 1);
}

TEST(RenderGraph, ErrorsResetEachFrameAndTheGraphRecovers)
{
    RenderGraph g;
    frame(g);
    pass(g, "Bad", [&](PassBuilder& b) { b.read(b.create("T", tex())); });
    EXPECT_FALSE(g.compile());
    const TextureHandle bb = frame(g);
    EXPECT_TRUE(g.errors().empty());
    pass(g, "Good", [&](PassBuilder& b) { b.write(bb); });
    EXPECT_TRUE(compiles(g));
    EXPECT_TRUE(g.execute());
}

TEST(RenderGraph, ExecuteWithoutCompileFails)
{
    RenderGraph         g;
    const TextureHandle bb  = frame(g);
    int                 ran = 0;
    g.addPass<NoData>("A", [&](PassBuilder& b, NoData&) { b.write(bb); }, [&](PassContext&, const NoData&) { ++ran; });
    EXPECT_FALSE(g.execute());
    EXPECT_TRUE(g.hasError(ErrorCode::NotCompiled));
    EXPECT_EQ(ran, 0);
}

TEST(RenderGraph, TooManyResourcesIsAnErrorNotACrash)
{
    RenderGraph g;
    frame(g);
    uint32_t invalid = 0;
    pass(g, "Many",
         [&](PassBuilder& b)
         {
             for (uint32_t i = 0; i < 70000; ++i)
                 invalid += b.create("T", tex()).valid() ? 0u : 1u;
         });
    EXPECT_GT(invalid, 0u);
    EXPECT_FALSE(g.compile());
    EXPECT_TRUE(g.hasError(ErrorCode::TooManyResources));
}

// ---------------------------------------------------------------- execute

TEST(RenderGraph, ExecuteRunsPassesInOrderWithBarriersAround)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       t;
    pass(g, "GBuffer", [&](PassBuilder& b) { t = b.write(b.create("Albedo", tex(kRgba8))); });
    pass(g, "Unused", [&](PassBuilder& b) { b.write(b.create("U", tex())); });
    pass(g, "Light",
         [&](PassBuilder& b)
         {
             b.read(t);
             b.write(bb);
         });
    ASSERT_TRUE(compiles(g));
    Recorder rec(g);
    ASSERT_TRUE(g.execute(&rec));
    const std::vector<std::string> expected = {
        "B before GBuffer Graphics [Albedo]", // UNDEFINED -> RT (discard)
        "P GBuffer",
        "E GBuffer",
        "B before Light Graphics [backbuffer,Albedo]", // PRESENT -> RT, RT -> SR (records sorted by resource)
        "P Light",
        "E Light",
        "B after Light Graphics [backbuffer]", // RT -> PRESENT
    };
    EXPECT_EQ(rec.events, expected);
}

TEST(RenderGraph, PassContextReportsScopeFrameAndNatives)
{
    RenderGraph g;
    g.beginFrame(320, 200);
    int                 nativeTag = 0;
    const TextureHandle imported  = g.importTexture("Env", absTex(64, 64), reinterpret_cast<ID3D12Resource*>(&nativeTag), Layout::ShaderResource, Layout::ShaderResource);
    const BufferHandle  ibuf      = g.importBuffer("Lights", buf(1024, 16), reinterpret_cast<ID3D12Resource*>(&nativeTag));
    TextureHandle       out;
    struct Data
    {
        TextureHandle env, out;
        BufferHandle  lights;
    };
    bool ran = false;
    g.addPass<Data>(
        "Sky",
        [&](PassBuilder& b, Data& d)
        {
            b.scope("SkyScope", 0xFF00FF00u);
            d.env    = b.read(imported);
            d.lights = b.read(ibuf, Use::PixelSrv);
            d.out = out = b.write(imported);
        },
        [&](PassContext& ctx, const Data& d)
        {
            ran = true;
            EXPECT_STREQ(ctx.scopeName(), "SkyScope");
            EXPECT_EQ(ctx.scopeColor(), 0xFF00FF00u);
            EXPECT_EQ(ctx.frameIndex(), 1u);
            EXPECT_EQ(ctx.resource(d.env), reinterpret_cast<ID3D12Resource*>(&nativeTag));
            EXPECT_EQ(ctx.resource(d.out), reinterpret_cast<ID3D12Resource*>(&nativeTag));
            EXPECT_EQ(ctx.resource(d.lights), reinterpret_cast<ID3D12Resource*>(&nativeTag));
            ASSERT_NE(ctx.desc(d.env), nullptr);
            EXPECT_EQ(ctx.desc(d.env)->width, 64u);
            EXPECT_EQ(ctx.desc(TextureHandle{}), nullptr);
            EXPECT_EQ(ctx.resource(TextureHandle{}), nullptr);
            EXPECT_EQ(ctx.width(TextureHandle{}), 0u);
        });
    EXPECT_FALSE(g.compile()); // read + write of one texture in one pass (SR + RT)
    EXPECT_TRUE(g.hasError(ErrorCode::IncompatibleUses));
    EXPECT_FALSE(ran);
}

TEST(RenderGraph, PassContextNativesWhenValid)
{
    RenderGraph g;
    g.beginFrame(320, 200);
    int                 nativeTag = 0;
    const TextureHandle imported  = g.importTexture("Env", absTex(64, 64), reinterpret_cast<ID3D12Resource*>(&nativeTag), Layout::ShaderResource, Layout::ShaderResource);
    struct Data
    {
        TextureHandle env, tmp;
    };
    bool ran = false;
    g.addPass<Data>(
        "Sky",
        [&](PassBuilder& b, Data& d)
        {
            b.scope("SkyScope", 7u);
            b.flags(PassFlags::SideEffect);
            d.env = b.read(imported);
            d.tmp = b.write(b.create("Tmp", tex(kRgba8, 0.25f)));
        },
        [&](PassContext& ctx, const Data& d)
        {
            ran = true;
            EXPECT_STREQ(ctx.scopeName(), "SkyScope");
            EXPECT_EQ(ctx.scopeColor(), 7u);
            EXPECT_EQ(ctx.frameIndex(), 1u);
            EXPECT_EQ(ctx.resource(d.env), reinterpret_cast<ID3D12Resource*>(&nativeTag));
            EXPECT_EQ(ctx.resource(d.tmp), nullptr); // transients get memory in Phase 5
            EXPECT_EQ(ctx.width(d.tmp), 80u);
            EXPECT_EQ(ctx.height(d.tmp), 50u);
            EXPECT_EQ(ctx.historyId(d.tmp), kNoPass);
        });
    ASSERT_TRUE(compiles(g));
    ASSERT_TRUE(g.execute());
    EXPECT_TRUE(ran);
}

TEST(RenderGraph, ExecuteThunkCapturesAreDestroyedAtTheNextFrame)
{
    RenderGraph         g;
    auto                token = std::make_shared<int>(5);
    const TextureHandle bb    = frame(g);
    int                 seen  = 0;
    g.addPass<NoData>("A", [&](PassBuilder& b, NoData&) { b.write(bb); }, [token, &seen](PassContext&, const NoData&) { seen = *token; });
    EXPECT_EQ(token.use_count(), 2);
    ASSERT_TRUE(compiles(g));
    ASSERT_TRUE(g.execute());
    EXPECT_EQ(seen, 5);
    g.beginFrame(1920, 1080);
    EXPECT_EQ(token.use_count(), 1); // the arena ran the lambda's destructor
}

TEST(RenderGraph, PassNamesAreCopied)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    {
        std::string name = "Temporary";
        pass(g, name.c_str(), [&](PassBuilder& b) { b.write(bb); });
        name.assign("XXXXXXXXXX");
    }
    EXPECT_STREQ(g.passName(0), "Temporary");
    EXPECT_EQ(g.findPass("Temporary"), 0u);
    EXPECT_EQ(g.findPass("Missing"), kNoPass);
    EXPECT_EQ(g.findResource("backbuffer"), bb.index);
    EXPECT_EQ(g.findResource("Missing"), kInvalidIndex);
}

// ---------------------------------------------------------------- plan cache

namespace
{
    void buildStandard(RenderGraph& g, uint32_t w, uint32_t h, bool extra = false)
    {
        const TextureHandle bb = frame(g, w, h);
        TextureHandle       t;
        pass(g, "A", [&](PassBuilder& b) { t = b.write(b.create("T", tex())); });
        if (extra)
            pass(g, "Extra", [&](PassBuilder& b) { t = b.readWrite(t, Use::Uav); });
        pass(g, "B",
             [&](PassBuilder& b)
             {
                 b.read(t);
                 b.write(bb);
             });
    }
} // namespace

TEST(RenderGraph, IdenticalFramesReuseTheCompiledPlan)
{
    RenderGraph g;
    buildStandard(g, 800, 600);
    ASSERT_TRUE(compiles(g));
    EXPECT_FALSE(g.compiledFromCache());
    const uint64_t                key      = g.planKey();
    const std::vector<BarrierRec> barriers = g.barriers();
    ASSERT_TRUE(g.execute());

    buildStandard(g, 800, 600);
    ASSERT_TRUE(compiles(g));
    EXPECT_TRUE(g.compiledFromCache());
    EXPECT_EQ(g.planKey(), key);
    EXPECT_EQ(g.barriers().size(), barriers.size());
    Recorder rec(g);
    ASSERT_TRUE(g.execute(&rec));
    EXPECT_NE(rec.find("P A"), -1);
    EXPECT_NE(rec.find("P B"), -1);
}

TEST(RenderGraph, StructureSizeOrOptionChangesRecompile)
{
    RenderGraph g;
    buildStandard(g, 800, 600);
    ASSERT_TRUE(compiles(g));
    const uint64_t key = g.planKey();

    buildStandard(g, 1024, 600); // output size
    ASSERT_TRUE(compiles(g));
    EXPECT_FALSE(g.compiledFromCache());
    EXPECT_NE(g.planKey(), key);

    buildStandard(g, 1024, 600, true); // extra pass
    ASSERT_TRUE(compiles(g));
    EXPECT_FALSE(g.compiledFromCache());

    CompileOptions o;
    o.noAliasing = true;
    g.setOptions(o);
    buildStandard(g, 1024, 600, true); // options
    ASSERT_TRUE(compiles(g));
    EXPECT_FALSE(g.compiledFromCache());

    buildStandard(g, 1024, 600, true);
    ASSERT_TRUE(compiles(g));
    EXPECT_TRUE(g.compiledFromCache());
}

TEST(RenderGraph, FailedCompileIsNeverCached)
{
    RenderGraph g;
    for (int i = 0; i < 2; ++i)
    {
        frame(g);
        pass(g, "Bad", [&](PassBuilder& b) { b.read(b.create("T", tex())); });
        EXPECT_FALSE(g.compile());
        EXPECT_FALSE(g.compiledFromCache());
    }
    buildStandard(g, 64, 64);
    ASSERT_TRUE(compiles(g));
    EXPECT_FALSE(g.compiledFromCache());
}

TEST(RenderGraph, DescChangeRecompiles)
{
    RenderGraph g;
    for (int i = 0; i < 2; ++i)
    {
        const TextureHandle bb = frame(g, 256, 256);
        TextureHandle       t;
        pass(g, "A", [&](PassBuilder& b) { t = b.write(b.create("T", tex(i == 0 ? kRgba16F : kRgba8))); });
        pass(g, "B",
             [&](PassBuilder& b)
             {
                 b.read(t);
                 b.write(bb);
             });
        ASSERT_TRUE(compiles(g));
        EXPECT_FALSE(g.compiledFromCache());
    }
}

// ---------------------------------------------------------------- arena and blackboard

TEST(RenderGraphArena, AlignmentLargeBlocksAndReset)
{
    Arena a;
    void* p1 = a.allocate(3, 1);
    void* p2 = a.allocate(16, 64);
    EXPECT_NE(p1, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p2) % 64, 0u);
    void* big = a.allocate(1u << 20, 16); // larger than one block
    ASSERT_NE(big, nullptr);
    std::memset(big, 0xAB, 1u << 20);
    EXPECT_GE(a.bytesUsed(), (1u << 20));
    const char* s = a.copyString("hello");
    EXPECT_STREQ(s, "hello");
    EXPECT_STREQ(a.copyString(nullptr), "");
    a.reset();
    EXPECT_EQ(a.bytesUsed(), 0u);
    void* p3 = a.allocate(8, 8);
    EXPECT_NE(p3, nullptr);
}

TEST(RenderGraphArena, CreateRunsDestructorsOnReset)
{
    struct Counted
    {
        int* counter;
        explicit Counted(int* c) :
            counter(c)
        {
        }
        ~Counted()
        {
            ++*counter;
        }
    };
    int   destroyed = 0;
    Arena a;
    a.create<Counted>(&destroyed);
    a.create<Counted>(&destroyed);
    int* trivial = a.create<int>(9);
    EXPECT_EQ(*trivial, 9);
    EXPECT_EQ(destroyed, 0);
    a.reset();
    EXPECT_EQ(destroyed, 2);
    a.reset();
    EXPECT_EQ(destroyed, 2);
}

namespace
{
    struct alignas(32) Over32
    {
        float v[8] = {};
    };

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // alignas pads sizeof; these tests allocate that size
#endif
    struct alignas(64) Over64
    {
        float v[16] = {};
        int   tag   = 0;
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    bool isAligned(const void* p, size_t align)
    {
        return reinterpret_cast<uintptr_t>(p) % align == 0;
    }

    // Base remainders (mod 64) the test hook can force: 0 is "as allocated", the rest model an
    // allocator that only guarantees 16-byte alignment (MSVC x64 operator new).
    constexpr size_t kRemainders[] = { 0, 16, 32, 48 };
    constexpr size_t kBlockBytes   = 64 * 1024; // Arena's block size (RenderGraph.cpp)
} // namespace

TEST(RenderGraphArena, OverAlignedAfterASmallAllocation)
{
    for (const size_t rem : kRemainders)
    {
        SCOPED_TRACE("base % 64 == " + std::to_string(rem));
        Arena a(rem);
        a.allocate(1, 1);
        const Over32* p32 = a.create<Over32>();
        EXPECT_TRUE(isAligned(p32, 32));
        a.allocate(24, 8);
        Over64* p64 = a.create<Over64>();
        EXPECT_TRUE(isAligned(p64, 64));
        p64->tag = 7;
        a.allocate(3, 1);
        EXPECT_TRUE(isAligned(a.allocate(10, 64), 64));
        EXPECT_TRUE(isAligned(a.allocate(1, 32), 32));
        EXPECT_EQ(p64->tag, 7);
    }
}

TEST(RenderGraphArena, OverAlignedAfterReset)
{
    for (const size_t rem : kRemainders)
    {
        SCOPED_TRACE("base % 64 == " + std::to_string(rem));
        Arena a(rem);
        a.allocate(5, 1);
        a.create<Over64>();
        a.reset();
        // First allocation in the reused block, then one after a small allocation.
        EXPECT_TRUE(isAligned(a.create<Over64>(), 64));
        a.reset();
        a.allocate(1, 1);
        EXPECT_TRUE(isAligned(a.create<Over32>(), 32));
        EXPECT_TRUE(isAligned(a.create<Over64>(), 64));
    }
}

TEST(RenderGraphArena, OverAlignedAcrossBlockBoundariesAndNearTheEnd)
{
    for (const size_t rem : kRemainders)
    {
        SCOPED_TRACE("base % 64 == " + std::to_string(rem));
        Arena a(rem);
        // Many blocks' worth of mixed small and over-aligned allocations; every object is written in
        // full so ASan catches a capacity check that forgot the padding.
        for (int i = 0; i < 4000; ++i)
        {
            a.allocate(static_cast<size_t>(1 + i % 13), 1);
            Over64* p = a.create<Over64>();
            ASSERT_TRUE(isAligned(p, 64)) << i;
            std::memset(static_cast<void*>(p), 0x5A, sizeof(Over64));
            Over32* q = a.create<Over32>();
            ASSERT_TRUE(isAligned(q, 32)) << i;
            std::memset(static_cast<void*>(q), 0x5A, sizeof(Over32));
        }
        // Fill a block to just short of its end: the padded request must not run past it.
        for (size_t tail = 1; tail <= 64; tail += 7)
        {
            a.reset();
            void* fill = a.allocate(kBlockBytes - 64 - tail, 1);
            std::memset(fill, 0, kBlockBytes - 64 - tail);
            void* p = a.allocate(64, 64);
            ASSERT_TRUE(isAligned(p, 64)) << tail;
            std::memset(p, 0xA5, 64);
        }
        // A request larger than a block, over-aligned.
        void* big = a.allocate(kBlockBytes * 2, 64);
        ASSERT_TRUE(isAligned(big, 64));
        std::memset(big, 1, kBlockBytes * 2);
        EXPECT_TRUE(isAligned(a.create<Over64>(), 64));
    }
}

TEST(RenderGraphArena, OverAlignedPassDataAcrossFrames)
{
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // alignas pads sizeof; these tests allocate that size
#endif
    struct alignas(64) Data
    {
        TextureHandle t;
        float         pad[3] = {};
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    RenderGraph g;
    for (int frame = 0; frame < 3; ++frame)
    {
        const TextureHandle bb = [&]
        {
            g.beginFrame(64, 64);
            return g.importBackBuffer(nullptr, 64, 64, kBgra8);
        }();
        bool aligned = false;
        g.addPass<Data>("Over", [&](PassBuilder& b, Data& d) { d.t = b.write(bb); }, [&](PassContext&, const Data& d) { aligned = isAligned(&d, 64); });
        ASSERT_TRUE(compiles(g));
        ASSERT_TRUE(g.execute());
        EXPECT_TRUE(aligned) << frame;
    }
}

TEST(RenderGraphBlackboard, SetGetOverwriteAndPerFrameReset)
{
    struct GBuffer
    {
        TextureHandle albedo, normal;
    };
    struct Depth
    {
        TextureHandle depth;
    };
    RenderGraph g;
    frame(g);
    Blackboard& bb = g.blackboard();
    EXPECT_FALSE(bb.has<GBuffer>());
    EXPECT_EQ(bb.tryGet<GBuffer>(), nullptr);
    bb.set(GBuffer{ { 1, 1 }, { 2, 1 } });
    bb.set(Depth{ { 3, 4 } });
    ASSERT_TRUE(bb.has<GBuffer>());
    EXPECT_EQ(bb.tryGet<GBuffer>()->normal.index, 2);
    EXPECT_EQ(bb.tryGet<Depth>()->depth.version, 4);
    bb.set(GBuffer{ { 5, 0 }, { 6, 0 } });
    EXPECT_EQ(bb.tryGet<GBuffer>()->albedo.index, 5);
    frame(g);
    EXPECT_FALSE(g.blackboard().has<GBuffer>());
    EXPECT_FALSE(g.blackboard().has<Depth>());
}

// ---------------------------------------------------------------- dumps

TEST(RenderGraphDump, DotAndJsonDescribeTheFrame)
{
    RenderGraph         g;
    const TextureHandle bb = frame(g);
    TextureHandle       t;
    pass(g, "Shade \"quoted\"", [&](PassBuilder& b) { t = b.write(b.create("T\\1", tex())); });
    pass(g, "Dead", [&](PassBuilder& b) { b.write(b.create("D", tex())); });
    pass(g, "Post",
         [&](PassBuilder& b)
         {
             b.read(t);
             b.write(bb);
         });
    ASSERT_TRUE(compiles(g));

    std::string dot;
    g.dumpDot(dot);
    EXPECT_EQ(dot.rfind("digraph RenderGraph {", 0), 0u);
    EXPECT_NE(dot.find("p0 [shape=box"), std::string::npos);
    EXPECT_NE(dot.find("style=dashed,color=gray"), std::string::npos); // the culled pass
    EXPECT_NE(dot.find("p0 -> r"), std::string::npos);
    EXPECT_EQ(dot.back(), '\n');

    std::string json;
    g.dumpJson(json);
    EXPECT_NE(json.find("\"compiled\":true"), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"Shade \\\"quoted\\\"\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"T\\\\1\""), std::string::npos);
    EXPECT_NE(json.find("\"culled\":true"), std::string::npos);
    EXPECT_NE(json.find("\"barriers\":["), std::string::npos);
    EXPECT_NE(json.find("\"layout\":\"COMMON\""), std::string::npos);
    // Balanced braces/brackets outside strings.
    int  depth = 0;
    bool inStr = false;
    for (size_t i = 0; i < json.size(); ++i)
    {
        const char c = json[i];
        if (inStr)
        {
            if (c == '\\')
                ++i;
            else if (c == '"')
                inStr = false;
            continue;
        }
        if (c == '"')
            inStr = true;
        else if (c == '{' || c == '[')
            ++depth;
        else if (c == '}' || c == ']')
            --depth;
        EXPECT_GE(depth, 0);
    }
    EXPECT_EQ(depth, 0);
    EXPECT_FALSE(inStr);
}

TEST(RenderGraphDump, JsonListsErrorsOfAFailedFrame)
{
    RenderGraph g;
    frame(g);
    pass(g, "Bad", [&](PassBuilder& b) { b.read(b.create("T", tex())); });
    EXPECT_FALSE(g.compile());
    std::string json;
    g.dumpJson(json);
    EXPECT_NE(json.find("\"compiled\":false"), std::string::npos);
    EXPECT_NE(json.find("\"code\":\"ReadBeforeWrite\""), std::string::npos);
    EXPECT_EQ(json.find("\"syncs\""), std::string::npos);
}
