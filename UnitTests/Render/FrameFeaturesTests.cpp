#include <gtest/gtest.h>

#include "Render/FrameFeatures.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Feature lists and FeatureFrame (render-graph plan Phase 4): the real Sandbox and Editor lists are
// built into a graph with stub execute callbacks, and the compiled order must match the legacy pass
// tables (Render/PassTable.cpp before Phase 4) that the Phase 0 baselines were recorded against.

using namespace Dark;

namespace
{
    constexpr uint32_t kBgra8 = 87;
    constexpr uint32_t kD32   = 40;
    constexpr uint32_t kRgba  = 10;

    // Legacy pass tables, in order (sandboxDeferredPasses / editorDeferredPasses at f57ddd7).
    const std::vector<PassId> kLegacySandbox = {
        PassId::Shadows,    PassId::LocalShadows, PassId::GBuffer, PassId::Decals,     PassId::Gtao,        PassId::Ssr,       PassId::Lighting,   PassId::LocalVolumes, PassId::Sky,
        PassId::SsrCapture, PassId::Water,        PassId::Clouds,  PassId::Camouflage, PassId::Translucent, PassId::Particles, PassId::Underwater, PassId::Post,         PassId::Overlay,
    };
    const std::vector<PassId> kLegacyEditor = {
        PassId::Shadows,      PassId::LocalShadows, PassId::GBuffer,    PassId::Decals, PassId::Gtao,    PassId::Ssr,        PassId::Lighting,
        PassId::LocalVolumes, PassId::Sky,          PassId::SsrCapture, PassId::Water,  PassId::Clouds,  PassId::EditorGrid, PassId::EditorGizmos,
        PassId::Translucent,  PassId::Particles,    PassId::Underwater, PassId::Post,   PassId::Overlay,
    };

    ID3D12Resource* fakeResource(uint32_t r)
    {
        return reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0x100 + r * 0x10));
    }

    Rg::TextureDesc absDesc(uint32_t w, uint32_t h, uint32_t format)
    {
        Rg::TextureDesc d;
        d.sizeMode = Rg::SizeMode::Absolute;
        d.width    = w;
        d.height   = h;
        d.format   = format;
        return d;
    }

    // One host frame: imports every RenderResource, sets `ids` (in the given order) with callbacks
    // that log their PassId, compiles and executes.
    struct HostFrame
    {
        Rg::RenderGraph     graph;
        FeatureFrame        features;
        std::vector<PassId> ran;
        std::vector<bool>   sawResource;
        bool                withHistories = false; // request TAA / GTAO / SSR graph histories
        Rg::HistoryPair     taa;

        bool run(const FeaturePass* list, uint32_t count, const std::vector<PassId>& ids, const char* host = "test")
        {
            ran.clear();
            sawResource.clear();
            graph.beginFrame(1280, 720);
            features.begin(list, count);
            for (uint32_t r = 0; r < kRenderResourceCount; ++r)
            {
                const auto     res = static_cast<RenderResource>(r);
                const uint32_t fmt = res == RenderResource::Swap ? kBgra8 : (res == RenderResource::Depth ? kD32 : kRgba);
                features.importResource(graph, res, fakeResource(r), absDesc(1280, 720, fmt));
            }
            if (withHistories)
            {
                taa = graph.history("Taa", absDesc(1280, 720, kRgba));
                features.setHistory(RenderResource::TaaHistory, taa);
                features.setHistory(RenderResource::GtaoHistory, graph.history("Gtao", absDesc(1280, 720, kRgba)));
                features.setHistory(RenderResource::SsrHistory, graph.history("Ssr", absDesc(1280, 720, kRgba)));
                features.setHistory(RenderResource::Hdr, taa); // not a history resource: ignored
            }
            for (const PassId id : ids)
            {
                features.set(id,
                             [this, id](Rg::PassContext& ctx)
                             {
                                 ran.push_back(id);
                                 sawResource.push_back(ctx.resource(features.handle(RenderResource::Hdr)) == fakeResource(static_cast<uint32_t>(RenderResource::Hdr)));
                             });
            }
            if (!features.compileFrame(graph, host))
                return false;
            return graph.execute();
        }
    };

    std::vector<PassId> listIds(const FeaturePass* list, uint32_t count)
    {
        std::vector<PassId> ids;
        for (uint32_t i = 0; i < count; ++i)
            ids.push_back(list[i].id);
        return ids;
    }

    std::vector<PassId> without(std::vector<PassId> ids, std::initializer_list<PassId> drop)
    {
        for (const PassId d : drop)
            std::erase(ids, d);
        return ids;
    }

    std::string names(const std::vector<PassId>& ids)
    {
        std::string s;
        for (const PassId id : ids)
            s += std::string(s.empty() ? "" : " ") + passName(id);
        return s;
    }
} // namespace

TEST(FrameFeatures, NamesEveryPassIdAndResource)
{
    for (uint32_t i = 0; i < kPassIdCount; ++i)
        EXPECT_STRNE(passName(static_cast<PassId>(i)), "Unknown") << i;
    EXPECT_STREQ(passName(PassId::Gtao), "GTAO");
    EXPECT_STREQ(passName(static_cast<PassId>(200)), "Unknown");
    for (uint32_t i = 0; i < kRenderResourceCount; ++i)
        EXPECT_STRNE(renderResourceName(static_cast<RenderResource>(i)), "Unknown") << i;
}

TEST(FrameFeatures, ListsMatchTheLegacyTablesPlusDebugOverlays)
{
    uint32_t           count   = 0;
    const FeaturePass* sandbox = sandboxFeatures(count);
    EXPECT_EQ(without(listIds(sandbox, count), { PassId::DebugOverlays }), kLegacySandbox);
    const FeaturePass* editor = editorFeatures(count);
    // Underwater is optional in both lists; the Editor never sets it (as before, it never marked it).
    EXPECT_EQ(without(listIds(editor, count), { PassId::DebugOverlays }), kLegacyEditor);
}

TEST(FrameFeatures, OrderCheckSkipsOptionalAndRejectsMissingOrReordered)
{
    uint32_t                  count        = 0;
    const FeaturePass*        list         = sandboxFeatures(count);
    const std::vector<PassId> withoutWater = without(listIds(list, count), { PassId::Water });
    EXPECT_TRUE(featureOrderOk(list, count, withoutWater.data(), static_cast<uint32_t>(withoutWater.size())));

    const std::vector<PassId> withoutGBuffer = without(listIds(list, count), { PassId::GBuffer });
    EXPECT_FALSE(featureOrderOk(list, count, withoutGBuffer.data(), static_cast<uint32_t>(withoutGBuffer.size())));

    std::vector<PassId> swapped = withoutWater;
    std::swap(swapped[0], swapped[1]);
    EXPECT_FALSE(featureOrderOk(list, count, swapped.data(), static_cast<uint32_t>(swapped.size())));

    const FeaturePass* editor        = editorFeatures(count);
    const PassId       editorOrder[] = { PassId::GBuffer,    PassId::Decals,     PassId::Gtao,         PassId::Ssr,         PassId::Lighting, PassId::LocalVolumes,
                                         PassId::SsrCapture, PassId::EditorGrid, PassId::EditorGizmos, PassId::Translucent, PassId::Post };
    EXPECT_TRUE(featureOrderOk(editor, count, editorOrder, 11));
    PassId reversed[11];
    std::memcpy(reversed, editorOrder, sizeof(editorOrder));
    std::swap(reversed[7], reversed[8]); // EditorGizmos before EditorGrid
    EXPECT_FALSE(featureOrderOk(editor, count, reversed, 11));
}

TEST(FrameFeatures, HostCheckCountsEveryFailedFrame)
{
    uint32_t           count  = 0;
    const FeaturePass* list   = sandboxFeatures(count);
    const uint32_t     before = hostPassSequenceFailures();
    const PassId       good[] = { PassId::GBuffer, PassId::Decals, PassId::Gtao, PassId::Ssr, PassId::Lighting, PassId::LocalVolumes, PassId::Translucent, PassId::Post };
    EXPECT_TRUE(validateHostFeatureOrder("test", list, count, good, 8));
    EXPECT_EQ(hostPassSequenceFailures(), before);
    const PassId bad[] = { PassId::Lighting, PassId::GBuffer };
    EXPECT_FALSE(validateHostFeatureOrder("test", list, count, bad, 2));
    EXPECT_FALSE(validateHostFeatureOrder("test", list, count, bad, 2)); // logged once, counted twice
    EXPECT_EQ(hostPassSequenceFailures(), before + 2);
}

TEST(FrameFeatures, SandboxFrameCompilesInLegacyOrderWithNoBarriers)
{
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    HostFrame          f;
    const uint32_t     failures = hostPassSequenceFailures();
    // Every pass the Sandbox can set (underwater, camouflage, debug tiles on).
    ASSERT_TRUE(f.run(list, count, listIds(list, count), "Sandbox"));
    EXPECT_TRUE(f.features.orderOk());
    EXPECT_EQ(f.features.compiledOrder(), listIds(list, count)) << names(f.features.compiledOrder());
    EXPECT_EQ(without(f.features.compiledOrder(), { PassId::DebugOverlays }), kLegacySandbox);
    EXPECT_EQ(f.ran, f.features.compiledOrder());
    EXPECT_TRUE(f.graph.barriers().empty()); // opaque passes own their barriers until Phase 6
    EXPECT_TRUE(f.graph.syncPoints().empty());
    EXPECT_EQ(f.graph.queueOrder(Rg::Queue::AsyncCompute).size(), 0u);
    for (const bool saw : f.sawResource)
        EXPECT_TRUE(saw); // ctx.resource() hands the pass the imported ID3D12Resource
    EXPECT_EQ(hostPassSequenceFailures(), failures);

    // A plain frame (no water, camouflage, underwater or debug tiles), set in reverse order: still list order.
    std::vector<PassId> plain = without(listIds(list, count), { PassId::Water, PassId::Camouflage, PassId::Underwater, PassId::Overlay });
    std::vector<PassId> reversed(plain.rbegin(), plain.rend());
    ASSERT_TRUE(f.run(list, count, reversed, "Sandbox"));
    EXPECT_EQ(f.ran, plain) << names(f.ran);
    EXPECT_EQ(hostPassSequenceFailures(), failures);
}

TEST(FrameFeatures, EditorFrameCompilesInLegacyOrderWithNoBarriers)
{
    uint32_t                  count = 0;
    const FeaturePass*        list  = editorFeatures(count);
    HostFrame                 f;
    const uint32_t            failures = hostPassSequenceFailures();
    const std::vector<PassId> all      = without(listIds(list, count), { PassId::Underwater });
    ASSERT_TRUE(f.run(list, count, all, "Editor 3D"));
    EXPECT_EQ(f.ran, all) << names(f.ran);
    EXPECT_EQ(without(f.ran, { PassId::DebugOverlays }), without(kLegacyEditor, { PassId::Underwater }));
    EXPECT_TRUE(f.graph.barriers().empty());
    EXPECT_EQ(hostPassSequenceFailures(), failures);

    // Same frame again: the plan comes from the cache.
    ASSERT_TRUE(f.run(list, count, all, "Editor 3D"));
    EXPECT_TRUE(f.graph.compiledFromCache());
    EXPECT_EQ(f.ran, all);
}

TEST(FrameFeatures, MissingRequiredPassFailsTheOrderCheck)
{
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    HostFrame          f;
    const uint32_t     before = hostPassSequenceFailures();
    EXPECT_TRUE(f.run(list, count, without(listIds(list, count), { PassId::Lighting }), "missing-lighting"));
    EXPECT_FALSE(f.features.orderOk());
    EXPECT_EQ(hostPassSequenceFailures(), before + 1);
    EXPECT_EQ(f.ran, without(listIds(list, count), { PassId::Lighting })); // the frame still runs, as before
}

TEST(FrameFeatures, PassNotInTheListIsRejectedAndNeverRuns)
{
    uint32_t           count = 0;
    const FeaturePass* list  = editorFeatures(count);
    Rg::RenderGraph    g;
    FeatureFrame       features;
    g.beginFrame(64, 64);
    features.begin(list, count);
    bool ran = false;
    EXPECT_FALSE(features.set(PassId::Camouflage, [&](Rg::PassContext&) { ran = true; })); // Sandbox only
    EXPECT_FALSE(features.isSet(PassId::Camouflage));
    EXPECT_TRUE(features.set(PassId::GBuffer, [&](Rg::PassContext&) {}));
    EXPECT_TRUE(features.isSet(PassId::GBuffer));
    features.begin(list, count); // a new frame forgets the callbacks
    EXPECT_FALSE(features.isSet(PassId::GBuffer));
    EXPECT_FALSE(ran);
}

TEST(FrameFeatures, MissingImportStillCompiles)
{
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    Rg::RenderGraph    g;
    FeatureFrame       features;
    g.beginFrame(320, 180);
    features.begin(list, count);
    // Only the back buffer, depth and HDR exist (no shadows, no G-buffer handles).
    features.importResource(g, RenderResource::Swap, nullptr, absDesc(320, 180, kBgra8));
    features.importResource(g, RenderResource::Depth, nullptr, absDesc(320, 180, kD32));
    features.importResource(g, RenderResource::Hdr, nullptr, absDesc(320, 180, kRgba));
    std::vector<PassId> ran;
    for (const PassId id : without(listIds(list, count), { PassId::Water, PassId::Camouflage, PassId::Underwater, PassId::Overlay }))
        features.set(id, [&ran, id](Rg::PassContext&) { ran.push_back(id); });
    ASSERT_TRUE(features.compileFrame(g, "partial"));
    ASSERT_TRUE(g.execute());
    EXPECT_EQ(ran.size(), features.compiledOrder().size());
    EXPECT_EQ(ran.front(), PassId::Shadows);
    EXPECT_EQ(ran.back(), PassId::Post);
}

TEST(FrameFeatures, CompileFailureCountsAFailedFrameAndRunsNothing)
{
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    Rg::RenderGraph    g;
    FeatureFrame       features;
    g.beginFrame(320, 180);
    features.begin(list, count);
    features.importResource(g, RenderResource::Hdr, nullptr, absDesc(320, 180, kRgba));
    features.importResource(g, RenderResource::Swap, nullptr, absDesc(320, 180, kBgra8));
    bool ran = false;
    features.set(PassId::Post, [&](Rg::PassContext&) { ran = true; });
    // A declared AsyncCompute pass on a resource an opaque pass also uses (opaque layouts are only
    // known on the graphics queue).
    g.addPass<Rg::NoData>(
        "Declared",
        [&](Rg::PassBuilder& b, Rg::NoData&)
        {
            b.flags(Rg::PassFlags::SideEffect);
            b.queue(Rg::Queue::AsyncCompute);
            b.read(features.handle(RenderResource::Hdr), Rg::Use::ComputeSrv);
        },
        [](Rg::PassContext&, const Rg::NoData&) {});
    const uint32_t before = hostPassSequenceFailures();
    EXPECT_FALSE(features.compileFrame(g, "compile-failure"));
    EXPECT_TRUE(g.hasError(Rg::ErrorCode::MixedOpaqueUse));
    EXPECT_EQ(hostPassSequenceFailures(), before + 1);
    EXPECT_FALSE(features.orderOk());
    EXPECT_FALSE(ran);
}

// ---------------------------------------------------------------- histories (Phase 5)

TEST(FrameFeatures, HistoriesAreNeverImported)
{
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    HostFrame          f;
    ASSERT_TRUE(f.run(list, count, listIds(list, count), "Sandbox"));
    EXPECT_FALSE(f.features.handle(RenderResource::TaaHistory).valid());
    EXPECT_FALSE(f.features.handle(RenderResource::GtaoHistory).valid());
    EXPECT_FALSE(f.features.handle(RenderResource::SsrHistory).valid());
    EXPECT_TRUE(f.features.handle(RenderResource::Hdr).valid());
    EXPECT_EQ(f.graph.historyCount(), 0u);
}

TEST(FrameFeatures, PostGtaoAndSsrWriteTheirHistoriesSoTheGraphSwapsThem)
{
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    HostFrame          f;
    f.withHistories = true;
    ASSERT_TRUE(f.run(list, count, listIds(list, count), "Sandbox"));
    EXPECT_FALSE(f.taa.prev.valid());
    EXPECT_EQ(f.ran, f.features.compiledOrder()); // the history declarations change no order
    EXPECT_EQ(without(f.features.compiledOrder(), { PassId::DebugOverlays }), kLegacySandbox);
    EXPECT_EQ(f.graph.historyCount(), 3u);

    // Written last frame (declared by Post / GTAO / SSR): this frame reads a valid prev.
    ASSERT_TRUE(f.run(list, count, listIds(list, count), "Sandbox"));
    EXPECT_TRUE(f.taa.prev.valid());
    EXPECT_TRUE(f.graph.historySlots("Gtao").prevValid);
    EXPECT_TRUE(f.graph.historySlots("Ssr").prevValid);

    // A skipped write (TAA off this frame, the pass ran without it) drops the next prev only.
    f.graph.markHistoryUnwritten("Taa");
    ASSERT_TRUE(f.run(list, count, listIds(list, count), "Sandbox"));
    EXPECT_FALSE(f.taa.prev.valid());
    EXPECT_TRUE(f.graph.historySlots("Gtao").prevValid);
}

TEST(FrameFeatures, AHistoryWithoutItsWriterNeverSwaps)
{
    // Post not set this frame (e.g. a movie frame): nothing wrote TaaHistory, so no valid prev next.
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    HostFrame          f;
    f.withHistories = true;
    ASSERT_TRUE(f.run(list, count, without(listIds(list, count), { PassId::Post }), "Sandbox"));
    ASSERT_TRUE(f.run(list, count, listIds(list, count), "Sandbox"));
    EXPECT_FALSE(f.taa.prev.valid());
    EXPECT_TRUE(f.graph.historySlots("Gtao").prevValid);
}

// ---------------------------------------------------------------- converted entries (Phase 6 part 2)

namespace
{
    // The Sandbox list with Post converted: Bloom-like declared passes on a target the entry imports,
    // then a tonemap-like pass to the back buffer, with the TAA history through the builder.
    struct DeclaredPostFrame
    {
        HostFrame              host;
        std::vector<std::string> ran;
        Rg::TextureHandle      mip;

        bool run(bool withEcho)
        {
            uint32_t           count = 0;
            const FeaturePass* list  = sandboxFeatures(count);
            ran.clear();
            Rg::RenderGraph& g = host.graph;
            g.beginFrame(1280, 720);
            host.features.begin(list, count);
            for (uint32_t r = 0; r < kRenderResourceCount; ++r)
            {
                const auto     res = static_cast<RenderResource>(r);
                const uint32_t fmt = res == RenderResource::Swap ? kBgra8 : (res == RenderResource::Depth ? kD32 : kRgba);
                host.features.importResource(g, res, fakeResource(r), absDesc(1280, 720, fmt), Rg::Layout::RenderTarget);
            }
            const Rg::HistoryPair taa = g.history("Taa", absDesc(1280, 720, kRgba));
            host.features.setHistory(RenderResource::TaaHistory, taa);
            for (const PassId id : without(listIds(list, count), { PassId::Post, PassId::Overlay }))
                host.features.set(id, [this, id](Rg::PassContext&) { ran.push_back(passName(id)); });
            host.features.declare(PassId::Post,
                                  [this, withEcho](FeatureBuilder& fb)
                                  {
                                      Rg::RenderGraph& graph = fb.graph();
                                      if (withEcho)
                                          fb.opaque("Post.Echo", { RenderResource::Depth }, { RenderResource::Hdr }, [this](Rg::PassContext&) { ran.push_back("Post.Echo"); });
                                      mip = graph.importTexture("Bloom.Mip0", absDesc(640, 360, kRgba), fakeResource(40), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
                                      fb.addPass<Rg::NoData>(
                                          "Bloom.Extract",
                                          [&](Rg::PassBuilder& b, Rg::NoData&)
                                          {
                                              b.read(fb.handle(RenderResource::Hdr), Rg::Use::PixelSrv);
                                              mip = b.write(mip, Rg::Use::RenderTarget);
                                          },
                                          [this](Rg::PassContext&, const Rg::NoData&) { ran.push_back("Bloom.Extract"); });
                                      fb.addPass<Rg::NoData>(
                                          "Bloom.Composite",
                                          [&](Rg::PassBuilder& b, Rg::NoData&)
                                          {
                                              b.read(mip, Rg::Use::PixelSrv);
                                              fb.set(RenderResource::Hdr, b.readWrite(fb.handle(RenderResource::Hdr), Rg::Use::RenderTarget));
                                          },
                                          [this](Rg::PassContext&, const Rg::NoData&) { ran.push_back("Bloom.Composite"); });
                                      fb.addPass<Rg::NoData>(
                                          "Taa",
                                          [&](Rg::PassBuilder& b, Rg::NoData&)
                                          {
                                              b.read(fb.handle(RenderResource::Hdr), Rg::Use::PixelSrv);
                                              if (fb.prev(RenderResource::TaaHistory).valid())
                                                  b.read(fb.prev(RenderResource::TaaHistory), Rg::Use::PixelSrv);
                                              fb.set(RenderResource::Post, b.write(fb.handle(RenderResource::Post), Rg::Use::RenderTarget));
                                              fb.set(RenderResource::TaaHistory, b.write(fb.handle(RenderResource::TaaHistory), Rg::Use::RenderTarget));
                                          },
                                          [this](Rg::PassContext&, const Rg::NoData&) { ran.push_back("Taa"); });
                                      fb.addPass<Rg::NoData>(
                                          "Tonemap",
                                          [&](Rg::PassBuilder& b, Rg::NoData&)
                                          {
                                              b.read(fb.handle(RenderResource::Post), Rg::Use::PixelSrv);
                                              fb.set(RenderResource::Swap, b.write(fb.handle(RenderResource::Swap), Rg::Use::RenderTarget));
                                          },
                                          [this](Rg::PassContext&, const Rg::NoData&) { ran.push_back("Tonemap"); });
                                  });
            host.features.set(PassId::Overlay, [this](Rg::PassContext&) { ran.push_back("Overlay"); });
            if (!host.features.compileFrame(g, "test"))
                return false;
            return g.execute();
        }
    };

    std::vector<Rg::BarrierRec> recordsOn(const Rg::RenderGraph& g, const char* name)
    {
        std::vector<Rg::BarrierRec> out;
        const uint16_t              r = g.findResource(name);
        for (const Rg::BarrierRec& b : g.barriers())
            if (b.resource == r)
                out.push_back(b);
        return out;
    }
} // namespace

TEST(FrameFeatures, DeclaredEntryAddsItsPassesInListOrderAndCountsAsOneEntry)
{
    DeclaredPostFrame f;
    ASSERT_TRUE(f.run(true));
    EXPECT_TRUE(f.host.features.orderOk());
    EXPECT_TRUE(f.host.features.isDeclared(PassId::Post));
    const std::vector<PassId>& order = f.host.features.compiledOrder();
    EXPECT_EQ(std::count(order.begin(), order.end(), PassId::Post), 1);
    // Every list pass, then the converted Post passes in setup order, then Overlay.
    const auto at = [&](const char* n) { return std::find(f.ran.begin(), f.ran.end(), std::string(n)) - f.ran.begin(); };
    EXPECT_LT(at("Underwater"), at("Post.Echo"));
    EXPECT_LT(at("Post.Echo"), at("Bloom.Extract"));
    EXPECT_LT(at("Bloom.Extract"), at("Bloom.Composite"));
    EXPECT_LT(at("Bloom.Composite"), at("Taa"));
    EXPECT_LT(at("Taa"), at("Tonemap"));
    EXPECT_LT(at("Tonemap"), at("Overlay"));
    EXPECT_EQ(static_cast<size_t>(at("Overlay")), f.ran.size() - 1);
}

TEST(FrameFeatures, DeclaredPassesGetPlannedBarriersAndTrackedOnesAfterOpaquePasses)
{
    DeclaredPostFrame f;
    ASSERT_TRUE(f.run(false));
    const Rg::RenderGraph& g = f.host.graph;
    // The bloom mip is only used by declared passes: plain planned barriers (not tracked).
    const std::vector<Rg::BarrierRec> mip = recordsOn(g, "Bloom.Mip0");
    ASSERT_EQ(mip.size(), 2u);
    EXPECT_FALSE(mip[0].tracked);
    EXPECT_EQ(mip[0].before.layout, Rg::Layout::ShaderResource); // the import's layout
    EXPECT_EQ(mip[0].after.layout, Rg::Layout::RenderTarget);
    EXPECT_EQ(mip[1].after.layout, Rg::Layout::ShaderResource);
    // HDR was last written by an opaque pass (Underwater): the first declared reader gets one
    // tracked barrier, whose before-state comes from the frame tracker at execute time.
    const std::vector<Rg::BarrierRec> hdr = recordsOn(g, "Hdr");
    ASSERT_FALSE(hdr.empty());
    EXPECT_TRUE(hdr[0].tracked);
    EXPECT_EQ(hdr[0].after.layout, Rg::Layout::ShaderResource);
    EXPECT_EQ(g.passName(hdr[0].pass), std::string("Bloom.Extract"));
    // The back buffer goes from the declared Tonemap to the opaque Overlay with no graph barrier
    // (the opaque pass moves it itself), and none for the import (the tracker already has it).
    for (const Rg::BarrierRec& b : recordsOn(g, "backbuffer"))
        EXPECT_NE(g.passName(b.pass), std::string("Overlay"));
}

TEST(FrameFeatures, DeclaredEntryWritesTheHistorySoTheGraphSwapsIt)
{
    DeclaredPostFrame f;
    ASSERT_TRUE(f.run(false));
    f.host.graph.beginFrame(1280, 720);
    EXPECT_TRUE(f.host.graph.history("Taa", absDesc(1280, 720, kRgba)).prev.valid());
}

TEST(FrameFeatures, DeclareAndSetReplaceEachOther)
{
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    FeatureFrame       features;
    features.begin(list, count);
    EXPECT_TRUE(features.declare(PassId::Post, [](FeatureBuilder&) {}));
    EXPECT_TRUE(features.isDeclared(PassId::Post));
    EXPECT_TRUE(features.set(PassId::Post, [](Rg::PassContext&) {}));
    EXPECT_TRUE(features.isSet(PassId::Post));
    EXPECT_FALSE(features.isDeclared(PassId::Post));
    EXPECT_FALSE(features.declare(PassId::EditorGrid, [](FeatureBuilder&) {})); // not in the Sandbox list
}

// The timing contract a declare callback relies on (the Phase 6 part 2 bug: Post captured the
// host's view-proj at setup, before the GBuffer pass that fills it had run). A declare callback
// runs while execute() builds the graph, before any pass executes; values a host fills in an
// earlier pass's execute are only visible from a declared pass's execute, through a reference.
TEST(FrameFeatures, DeclareRunsBeforeAnyPassExecutesSoHostValuesAreReadAtExecute)
{
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    HostFrame          host;
    Rg::RenderGraph&   g = host.graph;
    g.beginFrame(1280, 720);
    host.features.begin(list, count);
    for (uint32_t r = 0; r < kRenderResourceCount; ++r)
    {
        const auto     res = static_cast<RenderResource>(r);
        const uint32_t fmt = res == RenderResource::Swap ? kBgra8 : (res == RenderResource::Depth ? kD32 : kRgba);
        host.features.importResource(g, res, fakeResource(r), absDesc(1280, 720, fmt), Rg::Layout::RenderTarget);
    }
    int filledByGBuffer = 0; // stands in for the host's viewProj
    int seenAtSetup     = -1;
    int seenAtExecute   = -1;
    for (const PassId id : without(listIds(list, count), { PassId::Post }))
        host.features.set(id, [&, id](Rg::PassContext&) {
            if (id == PassId::GBuffer)
                filledByGBuffer = 42;
        });
    host.features.declare(PassId::Post,
                          [&](FeatureBuilder& fb)
                          {
                              seenAtSetup           = filledByGBuffer;
                              const int* atExecute  = &filledByGBuffer;
                              fb.addPass<Rg::NoData>(
                                  "Tonemap",
                                  [&](Rg::PassBuilder& b, Rg::NoData&)
                                  {
                                      b.read(fb.handle(RenderResource::Hdr), Rg::Use::PixelSrv);
                                      fb.set(RenderResource::Swap, b.write(fb.handle(RenderResource::Swap), Rg::Use::RenderTarget));
                                  },
                                  [&seenAtExecute, atExecute](Rg::PassContext&, const Rg::NoData&) { seenAtExecute = *atExecute; });
                          });
    ASSERT_TRUE(host.features.compileFrame(g, "test"));
    EXPECT_EQ(seenAtSetup, 0); // GBuffer has not executed yet
    ASSERT_TRUE(g.execute());
    EXPECT_EQ(seenAtExecute, 42);
}

// ---------------------------------------------------------------- converted entries (Phase 6 part 3)

namespace
{
    // The Sandbox list with GTAO converted the way SceneRenderer::addGtaoPasses does it: declared
    // steps on targets the entry imports, the history through the builder, then an opaque hand-off
    // pass (OwnBarriers | SideEffect) that declares a read of the output for the opaque Lighting.
    struct DeclaredGtaoFrame
    {
        HostFrame                host;
        std::vector<std::string> ran;
        Rg::HistoryPair          gtao;

        bool run(bool skip)
        {
            uint32_t           count = 0;
            const FeaturePass* list  = sandboxFeatures(count);
            ran.clear();
            Rg::RenderGraph& g = host.graph;
            g.beginFrame(1280, 720);
            host.features.begin(list, count);
            for (uint32_t r = 0; r < kRenderResourceCount; ++r)
            {
                const auto     res = static_cast<RenderResource>(r);
                const uint32_t fmt = res == RenderResource::Swap ? kBgra8 : (res == RenderResource::Depth ? kD32 : kRgba);
                host.features.importResource(g, res, fakeResource(r), absDesc(1280, 720, fmt), Rg::Layout::RenderTarget);
            }
            gtao = g.history("Gtao", absDesc(1280, 720, kRgba));
            host.features.setHistory(RenderResource::GtaoHistory, gtao);
            for (const PassId id : without(listIds(list, count), { PassId::Gtao }))
                host.features.set(id, [this, id](Rg::PassContext&) { ran.push_back(passName(id)); });
            host.features.declare(PassId::Gtao,
                                  [this, skip](FeatureBuilder& fb)
                                  {
                                      if (skip)
                                      {
                                          fb.graph().markHistoryUnwritten("Gtao");
                                          fb.opaque("Gtao", {}, {}, [this](Rg::PassContext&) { ran.push_back("Gtao.Skip"); });
                                          return;
                                      }
                                      Rg::RenderGraph&  graph = fb.graph();
                                      Rg::TextureHandle half  = graph.importTexture("DE.Gtao.AoHalf", absDesc(640, 360, kRgba), fakeResource(50), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
                                      Rg::TextureHandle compose =
                                          graph.importTexture("DE.Gtao.AoCompose", absDesc(1280, 720, kRgba), fakeResource(51), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
                                      fb.addPass<Rg::NoData>(
                                          "Gtao.Produce",
                                          [&](Rg::PassBuilder& b, Rg::NoData&)
                                          {
                                              b.read(fb.handle(RenderResource::Depth), Rg::Use::PixelSrv);
                                              b.read(fb.handle(RenderResource::Attrib), Rg::Use::PixelSrv);
                                              half = b.write(half, Rg::Use::RenderTarget);
                                          },
                                          [this](Rg::PassContext&, const Rg::NoData&) { ran.push_back("Gtao.Produce"); });
                                      fb.addPass<Rg::NoData>(
                                          "Gtao.Upsample",
                                          [&](Rg::PassBuilder& b, Rg::NoData&)
                                          {
                                              b.read(half, Rg::Use::PixelSrv);
                                              if (fb.prev(RenderResource::GtaoHistory).valid())
                                                  b.read(fb.prev(RenderResource::GtaoHistory), Rg::Use::PixelSrv);
                                              fb.set(RenderResource::GtaoHistory, b.write(fb.handle(RenderResource::GtaoHistory), Rg::Use::RenderTarget));
                                          },
                                          [this](Rg::PassContext&, const Rg::NoData&) { ran.push_back("Gtao.Upsample"); });
                                      fb.addPass<Rg::NoData>(
                                          "Gtao.Compose",
                                          [&](Rg::PassBuilder& b, Rg::NoData&)
                                          {
                                              b.read(fb.handle(RenderResource::Ao), Rg::Use::PixelSrv);
                                              b.read(fb.handle(RenderResource::GtaoHistory), Rg::Use::PixelSrv);
                                              compose = b.write(compose, Rg::Use::RenderTarget);
                                          },
                                          [this](Rg::PassContext&, const Rg::NoData&) { ran.push_back("Gtao.Compose"); });
                                      fb.addPass<Rg::NoData>(
                                          "Gtao.Handoff",
                                          [&](Rg::PassBuilder& b, Rg::NoData&)
                                          {
                                              b.flags(Rg::PassFlags::OwnBarriers | Rg::PassFlags::SideEffect);
                                              b.read(compose, Rg::Use::PixelSrv);
                                          },
                                          [this](Rg::PassContext&, const Rg::NoData&) { ran.push_back("Gtao.Handoff"); });
                                  });
            if (!host.features.compileFrame(g, "test"))
                return false;
            return g.execute();
        }

        long at(const char* n) const
        {
            return std::find(ran.begin(), ran.end(), std::string(n)) - ran.begin();
        }
    };
} // namespace

TEST(FrameFeatures, GtaoEntryRunsItsStepsThenTheHandoffBeforeLighting)
{
    DeclaredGtaoFrame f;
    ASSERT_TRUE(f.run(false));
    EXPECT_TRUE(f.host.features.orderOk());
    EXPECT_LT(f.at("Decals"), f.at("Gtao.Produce"));
    EXPECT_LT(f.at("Gtao.Produce"), f.at("Gtao.Upsample"));
    EXPECT_LT(f.at("Gtao.Upsample"), f.at("Gtao.Compose"));
    EXPECT_LT(f.at("Gtao.Compose"), f.at("Gtao.Handoff"));
    EXPECT_LT(f.at("Gtao.Handoff"), f.at("SSR"));
    EXPECT_LT(f.at("SSR"), f.at("Lighting"));
    EXPECT_EQ(static_cast<size_t>(f.at("Gtao.Handoff")) + 1, static_cast<size_t>(f.at("SSR")));
}

TEST(FrameFeatures, HandoffPassGetsNoGraphBarrierSoItMovesTheOutputItself)
{
    DeclaredGtaoFrame f;
    ASSERT_TRUE(f.run(false));
    const Rg::RenderGraph& g = f.host.graph;
    // The compose is written by a declared pass (planned barrier from the import's layout) and
    // read by the opaque hand-off: nothing is planned into the hand-off, it transitions itself.
    const std::vector<Rg::BarrierRec> compose = recordsOn(g, "DE.Gtao.AoCompose");
    ASSERT_FALSE(compose.empty());
    EXPECT_EQ(g.passName(compose[0].pass), std::string("Gtao.Compose"));
    EXPECT_EQ(compose[0].before.layout, Rg::Layout::ShaderResource);
    EXPECT_EQ(compose[0].after.layout, Rg::Layout::RenderTarget);
    for (const Rg::BarrierRec& b : g.barriers())
        EXPECT_NE(g.passName(b.pass), std::string("Gtao.Handoff"));
    // Depth was last touched by opaque passes: the first declared reader gets a tracked barrier.
    const std::vector<Rg::BarrierRec> depth = recordsOn(g, "Depth");
    ASSERT_FALSE(depth.empty());
    EXPECT_TRUE(depth[0].tracked);
    EXPECT_EQ(g.passName(depth[0].pass), std::string("Gtao.Produce"));
}

TEST(FrameFeatures, GtaoEntryWritesItsHistoryAndASkippedFrameDropsTheNextPrev)
{
    DeclaredGtaoFrame f;
    ASSERT_TRUE(f.run(false));
    EXPECT_FALSE(f.gtao.prev.valid());
    ASSERT_TRUE(f.run(false));
    EXPECT_TRUE(f.gtao.prev.valid()); // written by Gtao.Upsample last frame
    // The skip path: one opaque pass (the entry still adds a pass), history marked unwritten.
    ASSERT_TRUE(f.run(true));
    EXPECT_TRUE(f.host.features.orderOk());
    EXPECT_NE(static_cast<size_t>(f.at("Gtao.Skip")), f.ran.size());
    ASSERT_TRUE(f.run(false));
    EXPECT_FALSE(f.gtao.prev.valid());
    ASSERT_TRUE(f.run(false));
    EXPECT_TRUE(f.gtao.prev.valid());
}
