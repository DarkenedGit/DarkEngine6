#include <gtest/gtest.h>

#include "Particles/ParticleInputs.h"
#include "Render/DebugTileInputs.h"
#include "Render/FrameFeatures.h"
#include "Render/GBufferInputs.h"
#include "Render/HdrDrawInputs.h"
#include "Render/LightingInputs.h"
#include "Render/PostMatrices.h"
#include "Render/ResourceStateTracker.h"
#include "RenderGraphTestUtil.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Feature lists and FeatureFrame (render-graph plan Phase 4): the real Sandbox and Editor lists are
// built into a graph with stub execute callbacks, and the compiled order must match the legacy pass
// tables (Render/PassTable.cpp before Phase 4) that the Phase 0 baselines were recorded against, with
// the Phase 7 D12 shadow reorder applied.

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

    // A legacy table with the D12 reorder (plan Phase 7): FrameCull, the GPU cull that was the start of
    // Shadows, comes first, and Shadows and LocalShadows move to right after GBuffer.
    std::vector<PassId> d12(std::vector<PassId> ids)
    {
        std::erase(ids, PassId::Shadows);
        std::erase(ids, PassId::LocalShadows);
        ids.insert(std::find(ids.begin(), ids.end(), PassId::GBuffer) + 1, { PassId::Shadows, PassId::LocalShadows });
        ids.insert(ids.begin(), PassId::FrameCull);
        return ids;
    }

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

TEST(FrameFeatures, ListsMatchTheLegacyTablesWithTheD12ReorderPlusDebugOverlays)
{
    uint32_t           count   = 0;
    const FeaturePass* sandbox = sandboxFeatures(count);
    EXPECT_EQ(without(listIds(sandbox, count), { PassId::DebugOverlays }), d12(kLegacySandbox));
    const FeaturePass* editor = editorFeatures(count);
    // Underwater is optional in both lists; the Editor never sets it (as before, it never marked it).
    EXPECT_EQ(without(listIds(editor, count), { PassId::DebugOverlays }), d12(kLegacyEditor));
}

TEST(FrameFeatures, ShadowsFollowTheGBufferAndNothingBeforeLightingReadsAShadowMap)
{
    for (const bool editor : { false, true })
    {
        uint32_t                  count = 0;
        const FeaturePass*        list  = editor ? editorFeatures(count) : sandboxFeatures(count);
        const std::vector<PassId> ids   = listIds(list, count);
        const auto                at    = [&ids](PassId id) { return std::find(ids.begin(), ids.end(), id) - ids.begin(); };
        EXPECT_EQ(at(PassId::FrameCull), 0) << editor;
        EXPECT_EQ(at(PassId::GBuffer), 1) << editor;
        EXPECT_EQ(at(PassId::Shadows), 2) << editor;
        EXPECT_EQ(at(PassId::LocalShadows), 3) << editor;
        EXPECT_EQ(at(PassId::Decals), 4) << editor;
        EXPECT_LT(at(PassId::Ssr), at(PassId::Lighting)) << editor;
        for (uint32_t i = 0; i < count && list[i].id != PassId::Lighting; ++i)
        {
            for (uint32_t r = 0; r < list[i].readCount; ++r)
            {
                EXPECT_NE(list[i].reads[r], RenderResource::Shadow) << passName(list[i].id);
                EXPECT_NE(list[i].reads[r], RenderResource::LocalShadow) << passName(list[i].id);
            }
        }
        ASSERT_LT(at(PassId::FrameCull), static_cast<long>(count)) << editor;
        const FeaturePass& cull = list[at(PassId::FrameCull)];
        EXPECT_TRUE(cull.optional);
        EXPECT_EQ(cull.readCount + cull.writeCount, 0);
    }
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

TEST(FrameFeatures, SandboxFrameCompilesInD12OrderWithNoBarriers)
{
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    HostFrame          f;
    const uint32_t     failures = hostPassSequenceFailures();
    // Every pass the Sandbox can set (underwater, camouflage, debug tiles on).
    ASSERT_TRUE(f.run(list, count, listIds(list, count), "Sandbox"));
    EXPECT_TRUE(f.features.orderOk());
    EXPECT_EQ(f.features.compiledOrder(), listIds(list, count)) << names(f.features.compiledOrder());
    EXPECT_EQ(without(f.features.compiledOrder(), { PassId::DebugOverlays }), d12(kLegacySandbox));
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

TEST(FrameFeatures, EditorFrameCompilesInD12OrderWithNoBarriers)
{
    uint32_t                  count = 0;
    const FeaturePass*        list  = editorFeatures(count);
    HostFrame                 f;
    const uint32_t            failures = hostPassSequenceFailures();
    const std::vector<PassId> all      = without(listIds(list, count), { PassId::Underwater });
    ASSERT_TRUE(f.run(list, count, all, "Editor 3D"));
    EXPECT_EQ(f.ran, all) << names(f.ran);
    EXPECT_EQ(without(f.ran, { PassId::DebugOverlays }), without(d12(kLegacyEditor), { PassId::Underwater }));
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
    EXPECT_EQ(ran.front(), PassId::FrameCull);
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
    EXPECT_EQ(without(f.features.compiledOrder(), { PassId::DebugOverlays }), d12(kLegacySandbox));
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
            host.features.declare(PassId::Overlay,
                                  [this](FeatureBuilder& fb)
                                  {
                                      DebugTileRequest tiles;
                                      tiles.depth = true;
                                      addDebugTilePass(fb, tiles, [this](Rg::PassContext&, const DebugTileReads&) { ran.push_back("Overlay"); });
                                  });
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
    // The back buffer stays a render target from the declared Tonemap through the declared debug
    // tiles: the plan's render-target-to-render-target record is one the emitter drops.
    for (const Rg::BarrierRec& b : recordsOn(g, "backbuffer"))
        if (g.passName(b.pass) == std::string("Overlay"))
        {
            EXPECT_FALSE(ResourceStateTracker::needsBarrier(b.before, b.after, Rg::ResourceKind::Texture));
        }
    // Depth was last used by opaque passes: the tiles' read is one tracked barrier.
    bool depthIntoTiles = false;
    for (const Rg::BarrierRec& b : recordsOn(g, "Depth"))
        if (g.passName(b.pass) == std::string("Overlay"))
        {
            depthIntoTiles = true;
            EXPECT_TRUE(b.tracked);
            EXPECT_EQ(b.after.layout, Rg::Layout::ShaderResource);
        }
    EXPECT_TRUE(depthIntoTiles);
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

// ---------------------------------------------------------------- converted entries (Phase 6 parts 3 and 4)

namespace
{
    // The Sandbox list with GTAO, SSR, Lighting and LocalVolumes declared the way SceneRenderer does
    // it: GTAO and SSR publish their outputs on the blackboard (GtaoOutput / SsrOutput) and the
    // lighting passes read them through lightingReads() / declareLightingUses() (LightingInputs.h),
    // so the graph plans the barriers into Lighting and no hand-off pass is needed.
    struct DeclaredLightingFrame
    {
        HostFrame                host;
        std::vector<std::string> ran;
        Rg::HistoryPair          gtao;
        LightingReads            lit{}, vol{}; // the versions the declared lighting passes used
        Rg::TextureHandle        litHdrOut, volHdrOut;
        bool                     litDeclared = false;

        bool run(bool skipGtao, bool ssr = true)
        {
            uint32_t           count = 0;
            const FeaturePass* list  = sandboxFeatures(count);
            ran.clear();
            lit = vol = {};
            litHdrOut = volHdrOut = {};
            litDeclared           = false;
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
            for (const PassId id : without(listIds(list, count), { PassId::Gtao, PassId::Ssr, PassId::Lighting, PassId::LocalVolumes }))
                host.features.set(id, [this, id](Rg::PassContext&) { ran.push_back(passName(id)); });
            host.features.declare(PassId::Gtao, [this, skipGtao](FeatureBuilder& fb) { declareGtao(fb, skipGtao); });
            host.features.declare(PassId::Ssr, [this, ssr](FeatureBuilder& fb) { declareSsr(fb, ssr); });
            host.features.declare(PassId::Lighting,
                                  [this](FeatureBuilder& fb)
                                  {
                                      const LightingReads in = lightingReads(fb);
                                      if (!in.valid())
                                          return;
                                      litDeclared = true;
                                      fb.addPass<LightingReads>(
                                          "Lighting", [&](Rg::PassBuilder& b, LightingReads& d) { litHdrOut = declareLightingUses(b, in, LightingPass::Lighting, d); },
                                          [this](Rg::PassContext&, const LightingReads& d)
                                          {
                                              ran.push_back("Lighting");
                                              lit = d;
                                          });
                                      fb.set(RenderResource::Hdr, litHdrOut);
                                  });
            host.features.declare(PassId::LocalVolumes,
                                  [this](FeatureBuilder& fb)
                                  {
                                      const LightingReads in = lightingReads(fb);
                                      fb.addPass<LightingReads>(
                                          "LocalVolumes", [&](Rg::PassBuilder& b, LightingReads& d) { volHdrOut = declareLightingUses(b, in, LightingPass::LocalVolumes, d); },
                                          [this](Rg::PassContext&, const LightingReads& d)
                                          {
                                              ran.push_back("LocalVolumes");
                                              vol = d;
                                          });
                                      fb.set(RenderResource::Hdr, volHdrOut);
                                  });
            if (!host.features.compileFrame(g, "test"))
                return false;
            return g.execute();
        }

        void declareGtao(FeatureBuilder& fb, bool skip)
        {
            if (skip)
            {
                fb.graph().markHistoryUnwritten("Gtao");
                fb.opaque("Gtao", {}, {}, [this](Rg::PassContext&) { ran.push_back("Gtao.Skip"); });
                return;
            }
            Rg::RenderGraph&  graph   = fb.graph();
            Rg::TextureHandle half    = graph.importTexture("DE.Gtao.AoHalf", absDesc(640, 360, kRgba), fakeResource(50), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
            Rg::TextureHandle compose = graph.importTexture("DE.Gtao.AoCompose", absDesc(1280, 720, kRgba), fakeResource(51), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
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
            fb.graph().blackboard().set(GtaoOutput{ compose, {} });
        }

        void declareSsr(FeatureBuilder& fb, bool run)
        {
            if (!run)
            {
                fb.opaque("Ssr", {}, {}, [this](Rg::PassContext&) { ran.push_back("Ssr.Skip"); });
                return;
            }
            Rg::TextureHandle full = fb.graph().importTexture("DE.Ssr.Full", absDesc(1280, 720, kRgba), fakeResource(52), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
            fb.addPass<Rg::NoData>(
                "Ssr.Upsample",
                [&](Rg::PassBuilder& b, Rg::NoData&)
                {
                    b.read(fb.handle(RenderResource::Depth), Rg::Use::PixelSrv);
                    b.read(fb.handle(RenderResource::Albedo), Rg::Use::PixelSrv);
                    full = b.write(full, Rg::Use::RenderTarget);
                },
                [this](Rg::PassContext&, const Rg::NoData&) { ran.push_back("Ssr.Upsample"); });
            fb.graph().blackboard().set(SsrOutput{ full, {} });
        }

        long at(const char* n) const
        {
            return static_cast<long>(std::find(ran.begin(), ran.end(), std::string(n)) - ran.begin());
        }

        std::string resName(Rg::TextureHandle h) const
        {
            return h.valid() ? host.graph.resourceInfo(h.index).name : "";
        }
    };

    bool hasPass(const Rg::RenderGraph& g, const char* name)
    {
        for (uint32_t p = 0; p < g.passCount(); ++p)
            if (std::string(g.passName(p)) == name)
                return true;
        return false;
    }
} // namespace

TEST(FrameFeatures, GtaoAndSsrRunTheirStepsBeforeTheDeclaredLightingPasses)
{
    DeclaredLightingFrame f;
    ASSERT_TRUE(f.run(false));
    EXPECT_TRUE(f.host.features.orderOk());
    EXPECT_LT(f.at("Decals"), f.at("Gtao.Produce"));
    EXPECT_LT(f.at("Gtao.Produce"), f.at("Gtao.Upsample"));
    EXPECT_LT(f.at("Gtao.Upsample"), f.at("Gtao.Compose"));
    EXPECT_LT(f.at("Gtao.Compose"), f.at("Ssr.Upsample"));
    EXPECT_LT(f.at("Ssr.Upsample"), f.at("Lighting"));
    EXPECT_LT(f.at("Lighting"), f.at("LocalVolumes"));
    EXPECT_EQ(static_cast<size_t>(f.at("LocalVolumes")) + 1, static_cast<size_t>(f.at("Sky")));
    // Phase 6 part 4: Lighting is declared, so the GTAO / SSR hand-off passes are gone.
    EXPECT_FALSE(hasPass(f.host.graph, "Gtao.Handoff"));
    EXPECT_FALSE(hasPass(f.host.graph, "Ssr.Handoff"));
}

TEST(FrameFeatures, LightingReadsTheGtaoComposeThroughAPlannedBarrier)
{
    DeclaredLightingFrame f;
    ASSERT_TRUE(f.run(false));
    const Rg::RenderGraph& g = f.host.graph;
    EXPECT_EQ(f.resName(f.lit.ao), "DE.Gtao.AoCompose");
    EXPECT_EQ(f.resName(f.vol.ao), "DE.Gtao.AoCompose");
    // Compose: written by Gtao.Compose (RT), then the graph plans RT -> SRV right into Lighting.
    const std::vector<Rg::BarrierRec> compose = recordsOn(g, "DE.Gtao.AoCompose");
    ASSERT_EQ(compose.size(), 2u);
    EXPECT_EQ(g.passName(compose[1].pass), std::string("Lighting"));
    EXPECT_FALSE(compose[1].tracked);
    EXPECT_EQ(compose[1].before.layout, Rg::Layout::RenderTarget);
    EXPECT_EQ(compose[1].after.layout, Rg::Layout::ShaderResource);
    // The authored AO is only Gtao.Compose's input on a GTAO frame.
    for (const Rg::BarrierRec& b : recordsOn(g, "Ao"))
        EXPECT_NE(g.passName(b.pass), std::string("Lighting"));
}

TEST(FrameFeatures, SkippedGtaoLightingReadsTheAuthoredAoWithATrackedBarrier)
{
    DeclaredLightingFrame f;
    ASSERT_TRUE(f.run(false));
    ASSERT_TRUE(f.run(true)); // the blackboard is per frame: last frame's GtaoOutput must not leak
    EXPECT_TRUE(f.host.features.orderOk());
    EXPECT_EQ(f.resName(f.lit.ao), "Ao");
    EXPECT_EQ(f.resName(f.vol.ao), "Ao");
    const Rg::RenderGraph& g = f.host.graph;
    EXPECT_EQ(g.findResource("DE.Gtao.AoCompose"), Rg::kInvalidIndex);
    // No declared pass touched AO before: Lighting is its first declared reader, from the import's state.
    const std::vector<Rg::BarrierRec> ao = recordsOn(g, "Ao");
    ASSERT_EQ(ao.size(), 1u);
    EXPECT_EQ(g.passName(ao[0].pass), std::string("Lighting"));
    EXPECT_EQ(ao[0].before.layout, Rg::Layout::RenderTarget);
    EXPECT_EQ(ao[0].after.layout, Rg::Layout::ShaderResource);
}

TEST(FrameFeatures, LightingReadsSsrFullOnlyWhenSsrRan)
{
    DeclaredLightingFrame f;
    ASSERT_TRUE(f.run(false, true));
    EXPECT_EQ(f.resName(f.lit.ssr), "DE.Ssr.Full");
    EXPECT_FALSE(f.vol.ssr.valid()); // local volumes never sample SSR
    const std::vector<Rg::BarrierRec> full = recordsOn(f.host.graph, "DE.Ssr.Full");
    ASSERT_EQ(full.size(), 2u);
    EXPECT_EQ(f.host.graph.passName(full[1].pass), std::string("Lighting"));
    EXPECT_EQ(full[1].after.layout, Rg::Layout::ShaderResource);

    ASSERT_TRUE(f.run(false, false));
    EXPECT_TRUE(f.host.features.orderOk());
    EXPECT_NE(static_cast<size_t>(f.at("Ssr.Skip")), f.ran.size());
    EXPECT_NE(static_cast<size_t>(f.at("Lighting")), f.ran.size());
    EXPECT_FALSE(f.lit.ssr.valid()); // the exec binds the SSR dummy
}

TEST(FrameFeatures, LocalVolumesBlendOntoTheHdrLightingWrote)
{
    DeclaredLightingFrame f;
    ASSERT_TRUE(f.run(false));
    const Rg::RenderGraph& g = f.host.graph;
    ASSERT_TRUE(f.litHdrOut.valid());
    ASSERT_TRUE(f.volHdrOut.valid());
    EXPECT_EQ(f.resName(f.lit.hdr), "Hdr");
    EXPECT_EQ(f.lit.hdr, f.litHdrOut);
    // LocalVolumes reads Lighting's HDR version (readWrite) and writes the next one.
    EXPECT_EQ(f.vol.hdr, f.volHdrOut);
    EXPECT_EQ(f.volHdrOut.index, f.litHdrOut.index);
    EXPECT_EQ(f.volHdrOut.version, f.litHdrOut.version + 1);
    EXPECT_NE(static_cast<size_t>(f.at("Lighting")), f.ran.size()); // not culled
    // HDR: a tracked barrier into Lighting (Decals, opaque, drew to it last), then only a planned
    // RT -> RT write-after-write sync into LocalVolumes; the layout never leaves RenderTarget.
    const std::vector<Rg::BarrierRec> hdr = recordsOn(g, "Hdr");
    ASSERT_EQ(hdr.size(), 2u);
    EXPECT_EQ(g.passName(hdr[0].pass), std::string("Lighting"));
    EXPECT_TRUE(hdr[0].tracked);
    EXPECT_EQ(hdr[0].after.layout, Rg::Layout::RenderTarget);
    EXPECT_EQ(g.passName(hdr[1].pass), std::string("LocalVolumes"));
    EXPECT_FALSE(hdr[1].tracked);
    EXPECT_EQ(hdr[1].before.layout, Rg::Layout::RenderTarget);
    EXPECT_EQ(hdr[1].after.layout, Rg::Layout::RenderTarget);
    // G-buffer inputs were last written by opaque passes: tracked barriers into the first declared reader.
    const std::vector<Rg::BarrierRec> albedo = recordsOn(g, "Albedo");
    ASSERT_FALSE(albedo.empty());
    EXPECT_TRUE(albedo[0].tracked);
    EXPECT_EQ(albedo[0].after.layout, Rg::Layout::ShaderResource);
}

// declareLightingUses' HDR contract: Lighting discards sky pixels (they keep the Decals clear) and
// LocalVolumes blends additively, so both read and write HDR (one RenderTarget access, read + write).
// A write-only use would tell the graph the earlier contents are dead.
TEST(FrameFeatures, LightingPassesReadAndWriteHdr)
{
    Rg::RenderGraph g;
    g.beginFrame(1280, 720);
    LightingReads in;
    in.albedo              = g.importTexture("Albedo", absDesc(1280, 720, kRgba), fakeResource(1), Rg::Layout::ShaderResource, Rg::Layout::ShaderResource);
    in.attrib              = g.importTexture("Attrib", absDesc(1280, 720, kRgba), fakeResource(2), Rg::Layout::ShaderResource, Rg::Layout::ShaderResource);
    in.depth               = g.importTexture("Depth", absDesc(1280, 720, kD32), fakeResource(3), Rg::Layout::ShaderResource, Rg::Layout::ShaderResource);
    in.ao                  = g.importTexture("Ao", absDesc(1280, 720, kRgba), fakeResource(4), Rg::Layout::ShaderResource, Rg::Layout::ShaderResource);
    Rg::TextureHandle swap = g.importTexture("Swap", absDesc(1280, 720, kBgra8), fakeResource(5), Rg::Layout::RenderTarget, Rg::Layout::RenderTarget);
    Rg::TextureHandle hdr0;
    g.addPass<Rg::NoData>(
        "Clear", [&](Rg::PassBuilder& b, Rg::NoData&) { hdr0 = b.write(b.create("Hdr", absDesc(1280, 720, kRgba)), Rg::Use::RenderTarget); }, [](Rg::PassContext&, const Rg::NoData&) {});
    in.hdr = hdr0;
    g.addPass<LightingReads>("Lighting", [&](Rg::PassBuilder& b, LightingReads& d) { in.hdr = declareLightingUses(b, in, LightingPass::Lighting, d); }, [](Rg::PassContext&, const LightingReads&) {});
    g.addPass<LightingReads>("LocalVolumes", [&](Rg::PassBuilder& b, LightingReads& d) { in.hdr = declareLightingUses(b, in, LightingPass::LocalVolumes, d); }, [](Rg::PassContext&, const LightingReads&) {});
    g.addPass<Rg::NoData>(
        "Post",
        [&](Rg::PassBuilder& b, Rg::NoData&)
        {
            b.read(in.hdr, Rg::Use::PixelSrv);
            b.write(swap, Rg::Use::RenderTarget);
        },
        [](Rg::PassContext&, const Rg::NoData&) {});
    ASSERT_TRUE(g.compile());
    std::string json;
    g.dumpJson(json);
    const auto accesses = [&](const char* pass)
    {
        const size_t at  = json.find(std::string("\"name\":\"") + pass + "\"");
        const size_t end = json.find("{\"index\":", at);
        return at == std::string::npos ? std::string() : json.substr(at, end - at);
    };
    const std::string readWrite = "\"use\":\"RenderTarget\",\"read\":true,\"write\":true";
    EXPECT_NE(accesses("Lighting").find(readWrite), std::string::npos) << accesses("Lighting");
    EXPECT_NE(accesses("LocalVolumes").find(readWrite), std::string::npos) << accesses("LocalVolumes");
    // The clear feeds Lighting, so nothing before it is culled and the order is kept.
    EXPECT_FALSE(g.passCulled(g.findPass("Clear")));
    EXPECT_LT(g.passOrder(g.findPass("Clear")), g.passOrder(g.findPass("Lighting")));
    EXPECT_LT(g.passOrder(g.findPass("Lighting")), g.passOrder(g.findPass("LocalVolumes")));
}

TEST(FrameFeatures, LightingWithoutSceneBuffersFallsBackToAnOpaquePass)
{
    // lightingReads().valid() is what SceneRenderer checks before declaring (else an opaque no-op).
    HostFrame          host;
    Rg::RenderGraph&   g     = host.graph;
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    g.beginFrame(1280, 720);
    host.features.begin(list, count); // nothing imported: no G-buffer handles
    bool valid = true;
    host.features.declare(PassId::Lighting,
                          [&](FeatureBuilder& fb)
                          {
                              valid = lightingReads(fb).valid();
                              fb.opaque("Lighting", {}, {}, [](Rg::PassContext&) {});
                          });
    host.features.compileFrame(g, "test"); // the other entries are missing: only the declare matters
    EXPECT_FALSE(valid);
}

TEST(FrameFeatures, GtaoEntryWritesItsHistoryAndASkippedFrameDropsTheNextPrev)
{
    DeclaredLightingFrame f;
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

// ---------------------------------------------------------------- PR #48 review

// The Post matrices as SceneRenderer::addPostPasses uses them: built in the declare callback (graph
// build time) and filled in the pass exec, after a host's GBuffer pass wrote the jittered view-proj.
// Fails if PostMatrices inverts or copies the matrix when it is built (the Phase 6 part 2 bug).
TEST(FrameFeatures, PostMatricesBuiltAtDeclareFillTheViewProjGBufferWrote)
{
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    for (const bool havePrev : { false, true })
    {
        HostFrame        host;
        Rg::RenderGraph& g = host.graph;
        g.beginFrame(1280, 720);
        host.features.begin(list, count);
        for (uint32_t r = 0; r < kRenderResourceCount; ++r)
        {
            const auto     res = static_cast<RenderResource>(r);
            const uint32_t fmt = res == RenderResource::Swap ? kBgra8 : (res == RenderResource::Depth ? kD32 : kRgba);
            host.features.importResource(g, res, fakeResource(r), absDesc(1280, 720, fmt), Rg::Layout::RenderTarget);
        }
        Math::Matrix4f       viewProj = Math::Matrix4f::ScaleMatrix(1.0f); // the host's, before GBuffer runs
        const Math::Matrix4f prev     = Math::Matrix4f::ScaleMatrix(8.0f);
        float                inv[16]{};
        float                prevOut[16]{};
        for (const PassId id : without(listIds(list, count), { PassId::Post }))
            host.features.set(id,
                              [&, id](Rg::PassContext&)
                              {
                                  if (id == PassId::GBuffer)
                                      viewProj = Math::Matrix4f::ScaleMatrix(4.0f); // beginCameraFrame + jitter
                              });
        host.features.declare(PassId::Post,
                              [&](FeatureBuilder& fb)
                              {
                                  const PostMatrices matrices(viewProj, havePrev, prev);
                                  fb.addPass<Rg::NoData>(
                                      "Post.Taa",
                                      [&](Rg::PassBuilder& b, Rg::NoData&)
                                      {
                                          b.read(fb.handle(RenderResource::Hdr), Rg::Use::PixelSrv);
                                          fb.set(RenderResource::Swap, b.write(fb.handle(RenderResource::Swap), Rg::Use::RenderTarget));
                                      },
                                      [matrices, &inv, &prevOut](Rg::PassContext&, const Rg::NoData&) { matrices.fill(inv, prevOut); });
                              });
        ASSERT_TRUE(host.features.compileFrame(g, "test"));
        ASSERT_TRUE(g.execute());
        EXPECT_FLOAT_EQ(inv[0], 0.25f) << "havePrev=" << havePrev;
        EXPECT_FLOAT_EQ(inv[5], 0.25f);
        EXPECT_FLOAT_EQ(inv[10], 0.25f);
        EXPECT_FLOAT_EQ(prevOut[0], havePrev ? 8.0f : 4.0f);
    }
}

// An opaque Sky leaves HDR bound as a render target; the capture's HDR RT->SRV barrier is recorded
// before the capture executes. The executor clears the output merger before that batch, so the
// SsrCapture entry needs no unbind pass of its own.
TEST(FrameFeatures, OpaquePassBeforeADeclaredReaderIsUnboundBeforeItsBarrier)
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
    for (const PassId id : without(listIds(list, count), { PassId::SsrCapture }))
        host.features.set(id, [](Rg::PassContext&) {});
    host.features.declare(PassId::SsrCapture,
                          [&](FeatureBuilder& fb)
                          {
                              Rg::TextureHandle dst = fb.graph().importTexture("DE.Ssr.SceneColor1", absDesc(640, 360, kRgba), fakeResource(60), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
                              fb.addPass<Rg::NoData>(
                                  "SsrCapture",
                                  [&](Rg::PassBuilder& b, Rg::NoData&)
                                  {
                                      b.read(fb.handle(RenderResource::Hdr), Rg::Use::PixelSrv);
                                      dst = b.write(dst, Rg::Use::RenderTarget);
                                  },
                                  [](Rg::PassContext&, const Rg::NoData&) {});
                          });
    ASSERT_TRUE(host.features.compileFrame(g, "test"));
    Rg::Test::SplitRecorder rec(g, false); // only the opaque passes can have bound anything
    ASSERT_TRUE(g.execute(&rec));
    const int sky     = rec.find("E Sky");
    const int capture = rec.find("P SsrCapture");
    int       hdr     = -1;
    for (size_t i = 0; i < rec.events.size(); ++i)
        if (rec.events[i].rfind("B before SsrCapture ", 0) == 0 && rec.events[i].find("Hdr") != std::string::npos)
            hdr = static_cast<int>(i);
    ASSERT_GE(sky, 0);
    ASSERT_GT(hdr, sky);
    EXPECT_LT(hdr, capture);
    EXPECT_EQ(rec.events[static_cast<size_t>(hdr) - 1], "U Graphics");
}

// ---------------------------------------------------------------- G-buffer and decals (Phase 6 part 5)

namespace
{
    // The Sandbox list with GBuffer, Decals and Lighting declared the way the hosts and SceneRenderer
    // do (GBufferInputs.h / LightingInputs.h); GTAO and SSR take their skip paths (opaque, no uses).
    // Imports start in the layouts the frame tracker holds at that point: depth DEPTH_WRITE (cleared
    // in Renderer::beginFrame), the G-buffer and HDR as last frame left them (shader resource).
    struct DeclaredGeometryFrame
    {
        HostFrame                host;
        std::vector<std::string> ran;
        GBufferTargets           gb{};
        DecalTargets             dec{};
        HdrTarget                clear{};
        LightingReads            lit{};
        bool                     decalsAdded = false;

        bool run(bool decals)
        {
            uint32_t           count = 0;
            const FeaturePass* list  = sandboxFeatures(count);
            ran.clear();
            gb                 = {};
            dec                = {};
            clear              = {};
            lit                = {};
            decalsAdded        = false;
            Rg::RenderGraph& g = host.graph;
            g.beginFrame(1280, 720);
            host.features.begin(list, count);
            for (uint32_t r = 0; r < kRenderResourceCount; ++r)
            {
                const auto     res    = static_cast<RenderResource>(r);
                const uint32_t fmt    = res == RenderResource::Swap ? kBgra8 : (res == RenderResource::Depth ? kD32 : kRgba);
                Rg::Layout     layout = Rg::Layout::ShaderResource;
                if (res == RenderResource::Swap)
                    layout = Rg::Layout::RenderTarget;
                else if (res == RenderResource::Depth)
                    layout = Rg::Layout::DepthStencilWrite;
                host.features.importResource(g, res, fakeResource(r), absDesc(1280, 720, fmt), layout);
            }
            for (const PassId id : without(listIds(list, count), { PassId::GBuffer, PassId::Decals, PassId::Gtao, PassId::Ssr, PassId::Lighting }))
                host.features.set(id, [this, id](Rg::PassContext&) { ran.push_back(passName(id)); });
            host.features.declare(PassId::GBuffer,
                                  [this](FeatureBuilder& fb)
                                  {
                                      addGBufferPass(fb,
                                                     [this](Rg::PassContext&, const GBufferTargets& d)
                                                     {
                                                         ran.push_back("GBuffer");
                                                         gb = d;
                                                     });
                                  });
            host.features.declare(PassId::Decals,
                                  [this, decals](FeatureBuilder& fb)
                                  {
                                      if (decals)
                                          decalsAdded = addDecalPass(fb,
                                                                     [this](Rg::PassContext&, const DecalTargets& d)
                                                                     {
                                                                         ran.push_back("Decals");
                                                                         dec = d;
                                                                     });
                                      addHdrClearPass(fb,
                                                      [this](Rg::PassContext&, const HdrTarget& d)
                                                      {
                                                          ran.push_back("Decals.ClearHdr");
                                                          clear = d;
                                                      });
                                  });
            host.features.declare(PassId::Gtao, [this](FeatureBuilder& fb) { fb.opaque("Gtao", {}, {}, [this](Rg::PassContext&) { ran.push_back("Gtao.Skip"); }); });
            host.features.declare(PassId::Ssr, [this](FeatureBuilder& fb) { fb.opaque("Ssr", {}, {}, [this](Rg::PassContext&) { ran.push_back("Ssr.Skip"); }); });
            host.features.declare(PassId::Lighting,
                                  [this](FeatureBuilder& fb)
                                  {
                                      const LightingReads in = lightingReads(fb);
                                      Rg::TextureHandle   hdr;
                                      fb.addPass<LightingReads>(
                                          "Lighting", [&](Rg::PassBuilder& b, LightingReads& d) { hdr = declareLightingUses(b, in, LightingPass::Lighting, d); },
                                          [this](Rg::PassContext&, const LightingReads& d)
                                          {
                                              ran.push_back("Lighting");
                                              lit = d;
                                          });
                                      fb.set(RenderResource::Hdr, hdr);
                                  });
            if (!host.features.compileFrame(g, "test"))
                return false;
            return g.execute();
        }

        long at(const char* n) const
        {
            return static_cast<long>(std::find(ran.begin(), ran.end(), std::string(n)) - ran.begin());
        }

        // The barriers on `resource` recorded before `pass`.
        std::vector<Rg::BarrierRec> into(const char* resource, const char* pass) const
        {
            std::vector<Rg::BarrierRec> out;
            for (const Rg::BarrierRec& b : recordsOn(host.graph, resource))
                if (b.pass != Rg::kNoPass && b.pass < host.graph.passCount() && std::string(host.graph.passName(b.pass)) == pass)
                    out.push_back(b);
            return out;
        }

        // The pass's accesses in the graph's JSON dump.
        std::string accesses(const char* pass) const
        {
            std::string json;
            host.graph.dumpJson(json);
            const size_t at  = json.find(std::string("\"name\":\"") + pass + "\"");
            const size_t end = json.find("{\"index\":", at);
            return at == std::string::npos ? std::string() : json.substr(at, end - at);
        }
    };

    std::string accessOf(const std::string& passAccesses, uint16_t resource)
    {
        const std::string key = "{\"resource\":" + std::to_string(resource) + ",";
        const size_t      at  = passAccesses.find(key);
        return at == std::string::npos ? std::string() : passAccesses.substr(at, passAccesses.find('}', at) - at);
    }
} // namespace

TEST(FrameFeatures, GBufferAndDecalsRunAsDeclaredPassesInListOrder)
{
    DeclaredGeometryFrame f;
    ASSERT_TRUE(f.run(true));
    EXPECT_TRUE(f.host.features.orderOk());
    ASSERT_TRUE(f.decalsAdded);
    EXPECT_LT(f.at("FrameCull"), f.at("GBuffer"));
    EXPECT_LT(f.at("GBuffer"), f.at("LocalShadows"));
    EXPECT_LT(f.at("LocalShadows"), f.at("Decals"));
    EXPECT_LT(f.at("Decals"), f.at("Decals.ClearHdr"));
    EXPECT_LT(f.at("Decals.ClearHdr"), f.at("Gtao.Skip"));
    EXPECT_LT(f.at("Ssr.Skip"), f.at("Lighting"));
    // Each pass gets the versions the one before it wrote (FeatureBuilder::set after each pass).
    EXPECT_EQ(f.dec.albedo.index, f.gb.albedo.index);
    EXPECT_EQ(f.dec.albedo.version, f.gb.albedo.version + 1);
    EXPECT_EQ(f.dec.attrib.version, f.gb.attrib.version + 1);
    EXPECT_EQ(f.dec.depth, f.gb.depth);
    EXPECT_EQ(f.lit.albedo, f.dec.albedo);
    EXPECT_EQ(f.lit.attrib, f.dec.attrib);
    EXPECT_EQ(f.lit.ao, f.gb.ao);
    EXPECT_EQ(f.lit.depth, f.gb.depth);
    EXPECT_EQ(f.lit.hdr.version, f.clear.hdr.version + 1);
}

TEST(FrameFeatures, GBufferWritesItsClearedTargetsAndKeepsTheFrameStartDepthClear)
{
    DeclaredGeometryFrame f;
    ASSERT_TRUE(f.run(true));
    const std::string gb = f.accesses("GBuffer");
    ASSERT_FALSE(gb.empty());
    // MRTs: cleared in the pass, so written only (their old contents are dead).
    for (const char* name : { "Albedo", "Attrib", "Velocity", "Ao" })
    {
        const std::string a = accessOf(gb, f.host.graph.findResource(name));
        EXPECT_NE(a.find("\"use\":\"RenderTarget\",\"read\":false,\"write\":true"), std::string::npos) << name << ": " << a;
    }
    // Depth: cleared by Renderer::beginFrame, so tested against and written (read too).
    const std::string depth = accessOf(gb, f.host.graph.findResource("Depth"));
    EXPECT_NE(depth.find("\"use\":\"DepthWrite\",\"read\":true,\"write\":true"), std::string::npos) << depth;
    // Planned barriers: each MRT shader resource -> render target right before the pass; depth is
    // already DEPTH_WRITE, so it gets none.
    for (const char* name : { "Albedo", "Attrib", "Velocity", "Ao" })
    {
        const std::vector<Rg::BarrierRec> b = f.into(name, "GBuffer");
        ASSERT_EQ(b.size(), 1u) << name;
        EXPECT_EQ(b[0].before.layout, Rg::Layout::ShaderResource) << name;
        EXPECT_EQ(b[0].after.layout, Rg::Layout::RenderTarget) << name;
        EXPECT_FALSE(b[0].tracked) << name;
    }
    EXPECT_TRUE(f.into("Depth", "GBuffer").empty());
}

TEST(FrameFeatures, DecalsBlendIntoTheGBufferThroughUavsAndReadDepthReadOnly)
{
    DeclaredGeometryFrame f;
    ASSERT_TRUE(f.run(true));
    const std::string dec = f.accesses("Decals");
    for (const char* name : { "Albedo", "Attrib" })
    {
        const std::string a = accessOf(dec, f.host.graph.findResource(name));
        EXPECT_NE(a.find("\"use\":\"Uav\",\"read\":true,\"write\":true"), std::string::npos) << name << ": " << a;
        const std::vector<Rg::BarrierRec> b = f.into(name, "Decals");
        ASSERT_EQ(b.size(), 1u) << name;
        EXPECT_EQ(b[0].before.layout, Rg::Layout::RenderTarget) << name;
        EXPECT_EQ(b[0].after.layout, Rg::Layout::UnorderedAccess) << name;
        // ...and back to a shader resource for Lighting.
        const std::vector<Rg::BarrierRec> l = f.into(name, "Lighting");
        ASSERT_EQ(l.size(), 1u) << name;
        EXPECT_EQ(l[0].before.layout, Rg::Layout::UnorderedAccess) << name;
        EXPECT_EQ(l[0].after.layout, Rg::Layout::ShaderResource) << name;
    }
    const std::vector<Rg::BarrierRec> depth = f.into("Depth", "Decals");
    ASSERT_EQ(depth.size(), 1u);
    EXPECT_EQ(depth[0].before.layout, Rg::Layout::DepthStencilWrite);
    EXPECT_EQ(depth[0].after.layout, Rg::Layout::DirectQueueGenericRead);
    // The decal pass does not touch velocity or AO.
    EXPECT_TRUE(accessOf(dec, f.host.graph.findResource("Velocity")).empty());
    EXPECT_TRUE(accessOf(dec, f.host.graph.findResource("Ao")).empty());
}

TEST(FrameFeatures, NoDecalPassMeansNoUavRoundTrip)
{
    DeclaredGeometryFrame f;
    ASSERT_TRUE(f.run(false));
    EXPECT_TRUE(f.host.features.orderOk()); // the HDR clear keeps the Decals entry
    EXPECT_EQ(static_cast<size_t>(f.at("Decals")), f.ran.size());
    EXPECT_FALSE(f.dec.albedo.valid());
    EXPECT_EQ(f.lit.albedo, f.gb.albedo);
    for (const char* name : { "Albedo", "Attrib" })
    {
        const std::vector<Rg::BarrierRec> l = f.into(name, "Lighting");
        ASSERT_EQ(l.size(), 1u) << name;
        EXPECT_EQ(l[0].before.layout, Rg::Layout::RenderTarget) << name;
        EXPECT_EQ(l[0].after.layout, Rg::Layout::ShaderResource) << name;
        for (const Rg::BarrierRec& b : recordsOn(f.host.graph, name))
            EXPECT_NE(b.after.layout, Rg::Layout::UnorderedAccess) << name;
    }
}

TEST(FrameFeatures, HdrClearIsAWriteTheLightingPassDrawsOnto)
{
    DeclaredGeometryFrame f;
    ASSERT_TRUE(f.run(true));
    const std::string a = accessOf(f.accesses("Decals.ClearHdr"), f.host.graph.findResource("Hdr"));
    EXPECT_NE(a.find("\"use\":\"RenderTarget\",\"read\":false,\"write\":true"), std::string::npos) << a;
    // Shader resource (last frame) -> render target before the clear; then only a RT -> RT sync into
    // Lighting, which reads the clear (sky pixels keep it).
    const std::vector<Rg::BarrierRec> clr = f.into("Hdr", "Decals.ClearHdr");
    ASSERT_EQ(clr.size(), 1u);
    EXPECT_EQ(clr[0].before.layout, Rg::Layout::ShaderResource);
    EXPECT_EQ(clr[0].after.layout, Rg::Layout::RenderTarget);
    const std::vector<Rg::BarrierRec> lit = f.into("Hdr", "Lighting");
    ASSERT_EQ(lit.size(), 1u);
    EXPECT_EQ(lit[0].before.layout, Rg::Layout::RenderTarget);
    EXPECT_EQ(lit[0].after.layout, Rg::Layout::RenderTarget);
    EXPECT_FALSE(lit[0].tracked);
}

TEST(FrameFeatures, GBufferAndDecalHelpersAddNothingWithoutTargets)
{
    HostFrame          host;
    Rg::RenderGraph&   g     = host.graph;
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    g.beginFrame(1280, 720);
    host.features.begin(list, count); // nothing imported
    bool gbAdded = true, decAdded = true, clrAdded = true;
    host.features.declare(PassId::GBuffer,
                          [&](FeatureBuilder& fb)
                          {
                              gbAdded = addGBufferPass(fb, [](Rg::PassContext&, const GBufferTargets&) {});
                              fb.opaque("GBuffer", {}, {}, [](Rg::PassContext&) {});
                          });
    host.features.declare(PassId::Decals,
                          [&](FeatureBuilder& fb)
                          {
                              decAdded = addDecalPass(fb, [](Rg::PassContext&, const DecalTargets&) {});
                              clrAdded = addHdrClearPass(fb, [](Rg::PassContext&, const HdrTarget&) {});
                              fb.opaque("Decals", {}, {}, [](Rg::PassContext&) {});
                          });
    host.features.compileFrame(g, "test"); // the other entries are missing: only the declares matter
    EXPECT_FALSE(gbAdded);
    EXPECT_FALSE(decAdded);
    EXPECT_FALSE(clrAdded);
}

namespace
{
    // The Sandbox list from Sky to Underwater declared the way SceneRenderer does it
    // (HdrDrawInputs.h): SsrCapture publishes its scene color; Sky, Water, Clouds, Translucent,
    // DebugOverlays and Particles draw into HDR with read-only depth (Particles also samples depth and
    // attrib from compute); Camouflage and Underwater copy HDR first, and Camouflage writes depth. The
    // other entries are opaque. Depth and attrib come in as the lighting passes leave them (shader
    // resources), HDR as a render target.
    struct DeclaredHdrDrawFrame
    {
        HostFrame                host;
        std::vector<std::string> ran;
        HdrDrawTargets           sky{}, water{}, clouds{}, translucent{}, overlays{}, particles{};
        HdrCopyTargets           camoCopy{}, camo{};
        UnderwaterTargets        underCopy{}, under{};
        Rg::TextureHandle        captured;
        bool                     skyAdded = false, waterAdded = false, cloudsAdded = false, underAdded = false;
        bool                     camoAdded = false, translucentAdded = false, overlaysAdded = false, particlesAdded = false;

        bool run(bool capture, bool underwater = false, bool camouflage = false)
        {
            uint32_t           count = 0;
            const FeaturePass* list  = sandboxFeatures(count);
            ran.clear();
            sky = water = clouds = translucent = overlays = particles = {};
            camoCopy = camo = {};
            underCopy = under = {};
            captured          = {};
            skyAdded = waterAdded = cloudsAdded = underAdded = false;
            camoAdded = translucentAdded = overlaysAdded = particlesAdded = false;
            Rg::RenderGraph& g                               = host.graph;
            g.beginFrame(1280, 720);
            host.features.begin(list, count);
            for (uint32_t r = 0; r < kRenderResourceCount; ++r)
            {
                const auto     res    = static_cast<RenderResource>(r);
                const uint32_t fmt    = res == RenderResource::Swap ? kBgra8 : (res == RenderResource::Depth ? kD32 : kRgba);
                Rg::Layout     layout = Rg::Layout::ShaderResource;
                if (res == RenderResource::Swap || res == RenderResource::Hdr)
                    layout = Rg::Layout::RenderTarget;
                host.features.importResource(g, res, fakeResource(r), absDesc(1280, 720, fmt), layout);
            }
            for (const PassId id : without(listIds(list, count), { PassId::Sky, PassId::SsrCapture, PassId::Water, PassId::Clouds, PassId::Camouflage, PassId::Translucent, PassId::DebugOverlays,
                                                                   PassId::Particles, PassId::Underwater }))
                host.features.set(id, [this, id](Rg::PassContext&) { ran.push_back(passName(id)); });
            host.features.declare(PassId::Sky,
                                  [this](FeatureBuilder& fb)
                                  {
                                      skyAdded = addHdrDrawPass(fb, "Sky", skyUses(),
                                                                [this](Rg::PassContext&, const HdrDrawTargets& d)
                                                                {
                                                                    ran.push_back("Sky");
                                                                    sky = d;
                                                                });
                                  });
            host.features.declare(PassId::SsrCapture,
                                  [this, capture](FeatureBuilder& fb)
                                  {
                                      if (!capture)
                                      {
                                          fb.opaque("SsrCapture", {}, {}, [this](Rg::PassContext&) { ran.push_back("SsrCapture.Skip"); });
                                          return;
                                      }
                                      Rg::TextureHandle dst =
                                          fb.graph().importTexture("DE.Ssr.SceneColor1", absDesc(640, 360, kRgba), fakeResource(60), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
                                      fb.addPass<Rg::NoData>(
                                          "SsrCapture",
                                          [&](Rg::PassBuilder& b, Rg::NoData&)
                                          {
                                              b.read(fb.handle(RenderResource::Hdr), Rg::Use::PixelSrv);
                                              dst = b.write(dst, Rg::Use::RenderTarget);
                                          },
                                          [this](Rg::PassContext&, const Rg::NoData&) { ran.push_back("SsrCapture"); });
                                      captured = dst;
                                      fb.graph().blackboard().set(SsrCaptureOutput{ dst });
                                  });
            host.features.declare(PassId::Water,
                                  [this](FeatureBuilder& fb)
                                  {
                                      waterAdded = addHdrDrawPass(fb, "Water", waterUses(),
                                                                  [this](Rg::PassContext&, const HdrDrawTargets& d)
                                                                  {
                                                                      ran.push_back("Water");
                                                                      water = d;
                                                                  });
                                  });
            host.features.declare(PassId::Clouds,
                                  [this](FeatureBuilder& fb)
                                  {
                                      cloudsAdded = addHdrDrawPass(fb, "Clouds", false,
                                                                   [this](Rg::PassContext&, const HdrDrawTargets& d)
                                                                   {
                                                                       ran.push_back("Clouds");
                                                                       clouds = d;
                                                                   });
                                  });
            // Camouflage off: the host declares nothing and the optional entry is absent.
            if (camouflage)
                host.features.declare(PassId::Camouflage,
                                      [this](FeatureBuilder& fb)
                                      {
                                          const Rg::TextureHandle copy =
                                              fb.graph().importTexture("DE.Camouflage.SceneCopy", absDesc(1280, 720, kRgba), fakeResource(62), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
                                          camoAdded = addCamouflagePasses(
                                              fb, copy,
                                              [this](Rg::PassContext&, const HdrCopyTargets& d)
                                              {
                                                  ran.push_back("Camouflage.Copy");
                                                  camoCopy = d;
                                              },
                                              [this](Rg::PassContext&, const HdrCopyTargets& d)
                                              {
                                                  ran.push_back("Camouflage");
                                                  camo = d;
                                              });
                                      });
            const auto overlay = [this](PassId id, const char* name, HdrDrawUses uses, HdrDrawTargets* out, bool* added)
            {
                host.features.declare(id,
                                      [this, name, uses, out, added](FeatureBuilder& fb)
                                      {
                                          *added = addHdrDrawPass(fb, name, uses,
                                                                  [this, name, out](Rg::PassContext&, const HdrDrawTargets& d)
                                                                  {
                                                                      ran.push_back(name);
                                                                      *out = d;
                                                                  });
                                      });
            };
            overlay(PassId::Translucent, "Translucent", translucentUses(), &translucent, &translucentAdded);
            overlay(PassId::DebugOverlays, "DebugOverlays", HdrDrawUses{}, &overlays, &overlaysAdded);
            host.features.declare(PassId::Particles,
                                  [this](FeatureBuilder& fb)
                                  {
                                      particlesAdded = addHdrDrawPass(fb, "Particles", particlesUses(),
                                                                      [this](Rg::PassContext&, const HdrDrawTargets& d)
                                                                      {
                                                                          ran.push_back("Particles");
                                                                          particles = d;
                                                                      });
                                  });
            host.features.declare(PassId::Underwater,
                                  [this, underwater](FeatureBuilder& fb)
                                  {
                                      if (!underwater)
                                      {
                                          fb.opaque("Underwater", {}, {}, [this](Rg::PassContext&) { ran.push_back("Underwater.Skip"); });
                                          return;
                                      }
                                      const Rg::TextureHandle copy =
                                          fb.graph().importTexture("DE.Underwater.HdrCopy", absDesc(1280, 720, kRgba), fakeResource(61), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
                                      underAdded = addUnderwaterPasses(
                                          fb, copy,
                                          [this](Rg::PassContext&, const UnderwaterTargets& d)
                                          {
                                              ran.push_back("Underwater.Copy");
                                              underCopy = d;
                                          },
                                          [this](Rg::PassContext&, const UnderwaterTargets& d)
                                          {
                                              ran.push_back("Underwater");
                                              under = d;
                                          });
                                  });
            if (!host.features.compileFrame(g, "test"))
                return false;
            return g.execute();
        }

        long at(const char* n) const
        {
            return static_cast<long>(std::find(ran.begin(), ran.end(), std::string(n)) - ran.begin());
        }

        std::vector<Rg::BarrierRec> into(const char* resource, const char* pass) const
        {
            std::vector<Rg::BarrierRec> out;
            for (const Rg::BarrierRec& b : recordsOn(host.graph, resource))
                if (b.pass != Rg::kNoPass && b.pass < host.graph.passCount() && std::string(host.graph.passName(b.pass)) == pass)
                    out.push_back(b);
            return out;
        }

        std::string accesses(const char* pass) const
        {
            std::string json;
            host.graph.dumpJson(json);
            const size_t at  = json.find(std::string("\"name\":\"") + pass + "\"");
            const size_t end = json.find("{\"index\":", at);
            return at == std::string::npos ? std::string() : json.substr(at, end - at);
        }

        bool hasPass(const char* name) const
        {
            for (uint32_t i = 0; i < host.graph.passCount(); ++i)
                if (std::string(host.graph.passName(i)) == name)
                    return true;
            return false;
        }
    };
} // namespace

TEST(FrameFeatures, SkyWaterAndCloudsDrawOntoHdrInListOrder)
{
    DeclaredHdrDrawFrame f;
    ASSERT_TRUE(f.run(true));
    EXPECT_TRUE(f.host.features.orderOk());
    ASSERT_TRUE(f.skyAdded && f.waterAdded && f.cloudsAdded);
    EXPECT_LT(f.at("LocalVolumes"), f.at("Sky"));
    EXPECT_LT(f.at("Sky"), f.at("SsrCapture"));
    EXPECT_LT(f.at("SsrCapture"), f.at("Water"));
    EXPECT_LT(f.at("Water"), f.at("Clouds"));
    EXPECT_LT(f.at("Clouds"), f.at("Translucent"));
    // Each blends onto the HDR the one before wrote, and the depth target is never written.
    EXPECT_EQ(f.water.hdr.index, f.sky.hdr.index);
    EXPECT_EQ(f.water.hdr.version, f.sky.hdr.version + 1);
    EXPECT_EQ(f.clouds.hdr.version, f.water.hdr.version + 1);
    EXPECT_EQ(f.water.depth, f.sky.depth);
    EXPECT_EQ(f.clouds.depth, f.sky.depth);
    for (const char* pass : { "Sky", "Water", "Clouds" })
    {
        const std::string a     = f.accesses(pass);
        const std::string hdr   = accessOf(a, f.host.graph.findResource("Hdr"));
        const std::string depth = accessOf(a, f.host.graph.findResource("Depth"));
        EXPECT_NE(hdr.find("\"use\":\"RenderTarget\",\"read\":true,\"write\":true"), std::string::npos) << pass << ": " << hdr;
        EXPECT_NE(depth.find("\"use\":\"DepthRead\",\"read\":true,\"write\":false"), std::string::npos) << pass << ": " << depth;
    }
}

TEST(FrameFeatures, DepthMovesToReadOnlyOnceBeforeSkyAndStaysThere)
{
    DeclaredHdrDrawFrame f;
    ASSERT_TRUE(f.run(true));
    // Opaque LocalVolumes before Sky: a tracked barrier (its before layout is read from the tracker).
    const std::vector<Rg::BarrierRec> sky = f.into("Depth", "Sky");
    ASSERT_EQ(sky.size(), 1u);
    EXPECT_EQ(sky[0].after.layout, Rg::Layout::DirectQueueGenericRead);
    EXPECT_TRUE(sky[0].tracked);
    // Water depth-tests, Clouds samples, Particles also samples from compute: one layout serves
    // them all, so no barrier between them, and the one into Sky covers the compute reads too.
    EXPECT_TRUE(any(sky[0].after.sync & Rg::Sync::ComputeShading));
    EXPECT_TRUE(any(sky[0].after.sync & Rg::Sync::DepthStencil));
    for (const char* pass : { "Water", "Clouds", "Translucent", "DebugOverlays", "Particles" })
        EXPECT_TRUE(f.into("Depth", pass).empty()) << pass;
}

TEST(FrameFeatures, TheHdrTailRunsDeclaredInListOrderWithoutTheBridge)
{
    DeclaredHdrDrawFrame f;
    ASSERT_TRUE(f.run(true, true, true));
    EXPECT_TRUE(f.host.features.orderOk());
    ASSERT_TRUE(f.camoAdded && f.translucentAdded && f.overlaysAdded && f.particlesAdded);
    EXPECT_LT(f.at("Clouds"), f.at("Camouflage.Copy"));
    EXPECT_LT(f.at("Camouflage.Copy"), f.at("Camouflage"));
    EXPECT_LT(f.at("Camouflage"), f.at("Translucent"));
    EXPECT_LT(f.at("Translucent"), f.at("DebugOverlays"));
    EXPECT_LT(f.at("DebugOverlays"), f.at("Particles"));
    EXPECT_LT(f.at("Particles"), f.at("Underwater.Copy"));
    EXPECT_FALSE(f.hasPass("Clouds.BindHdr"));
    // Every pass after Clouds is declared: none is left that needs HDR bound for it.
    for (const char* pass : { "Camouflage.Copy", "Camouflage", "Translucent", "DebugOverlays", "Particles", "Underwater.Copy", "Underwater" })
    {
        const std::string a  = f.accesses(pass);
        const size_t      at = a.find("\"flags\":");
        ASSERT_NE(at, std::string::npos) << pass;
        const uint32_t flags = static_cast<uint32_t>(std::stoul(a.substr(at + 8)));
        EXPECT_EQ(flags & static_cast<uint32_t>(Rg::PassFlags::OwnBarriers), 0u) << pass;
    }
    // The copy reads the HDR Clouds wrote; each draw after it blends onto the one before.
    EXPECT_EQ(f.camoCopy.hdr, f.clouds.hdr);
    EXPECT_EQ(f.translucent.hdr.version, f.camo.hdr.version + 1);
    EXPECT_EQ(f.overlays.hdr.version, f.translucent.hdr.version + 1);
    EXPECT_EQ(f.particles.hdr.version, f.overlays.hdr.version + 1);
    for (const char* pass : { "Translucent", "DebugOverlays", "Particles" })
    {
        const std::string a     = f.accesses(pass);
        const std::string hdr   = accessOf(a, f.host.graph.findResource("Hdr"));
        const std::string depth = accessOf(a, f.host.graph.findResource("Depth"));
        EXPECT_NE(hdr.find("\"use\":\"RenderTarget\",\"read\":true,\"write\":true"), std::string::npos) << pass << ": " << hdr;
        EXPECT_NE(depth.find("\"use\":\"DepthRead\",\"read\":true,\"write\":false"), std::string::npos) << pass << ": " << depth;
    }
}

TEST(FrameFeatures, CamouflageWritesDepthAndTheTailTestsAgainstIt)
{
    DeclaredHdrDrawFrame f;
    ASSERT_TRUE(f.run(true, false, true));
    ASSERT_TRUE(f.camoAdded);
    // The body writes a new depth version; Translucent onward read it.
    EXPECT_EQ(f.camo.depth.index, f.clouds.depth.index);
    EXPECT_EQ(f.camo.depth.version, f.clouds.depth.version + 1);
    EXPECT_EQ(f.translucent.depth, f.camo.depth);
    EXPECT_EQ(f.overlays.depth, f.camo.depth);
    EXPECT_EQ(f.particles.depth, f.camo.depth);
    const std::string depth = accessOf(f.accesses("Camouflage"), f.host.graph.findResource("Depth"));
    EXPECT_NE(depth.find("\"use\":\"DepthWrite\",\"read\":true,\"write\":true"), std::string::npos) << depth;

    const std::vector<Rg::BarrierRec> toWrite = f.into("Depth", "Camouflage");
    ASSERT_EQ(toWrite.size(), 1u);
    EXPECT_EQ(toWrite[0].before.layout, Rg::Layout::DirectQueueGenericRead);
    EXPECT_EQ(toWrite[0].after.layout, Rg::Layout::DepthStencilWrite);
    EXPECT_FALSE(toWrite[0].tracked);
    // Back to read-only once, before Translucent, for every read up to Particles' compute sampling.
    const std::vector<Rg::BarrierRec> toRead = f.into("Depth", "Translucent");
    ASSERT_EQ(toRead.size(), 1u);
    EXPECT_EQ(toRead[0].before.layout, Rg::Layout::DepthStencilWrite);
    EXPECT_EQ(toRead[0].after.layout, Rg::Layout::DirectQueueGenericRead);
    EXPECT_TRUE(any(toRead[0].after.sync & Rg::Sync::ComputeShading));
    EXPECT_TRUE(f.into("Depth", "DebugOverlays").empty());
    EXPECT_TRUE(f.into("Depth", "Particles").empty());

    // The copy: HDR to copy source and the copy target to copy dest, then back for the draw.
    const std::vector<Rg::BarrierRec> hdrCopy = f.into("Hdr", "Camouflage.Copy");
    ASSERT_EQ(hdrCopy.size(), 1u);
    EXPECT_EQ(hdrCopy[0].before.layout, Rg::Layout::RenderTarget);
    EXPECT_EQ(hdrCopy[0].after.layout, Rg::Layout::CopySource);
    const std::vector<Rg::BarrierRec> dstCopy = f.into("DE.Camouflage.SceneCopy", "Camouflage.Copy");
    ASSERT_EQ(dstCopy.size(), 1u);
    EXPECT_EQ(dstCopy[0].after.layout, Rg::Layout::CopyDest);
    const std::vector<Rg::BarrierRec> hdrDraw = f.into("Hdr", "Camouflage");
    ASSERT_EQ(hdrDraw.size(), 1u);
    EXPECT_EQ(hdrDraw[0].before.layout, Rg::Layout::CopySource);
    EXPECT_EQ(hdrDraw[0].after.layout, Rg::Layout::RenderTarget);
    const std::vector<Rg::BarrierRec> srcDraw = f.into("DE.Camouflage.SceneCopy", "Camouflage");
    ASSERT_EQ(srcDraw.size(), 1u);
    EXPECT_EQ(srcDraw[0].before.layout, Rg::Layout::CopyDest);
    EXPECT_EQ(srcDraw[0].after.layout, Rg::Layout::ShaderResource);
    EXPECT_EQ(f.camo.copy, f.camoCopy.copy);
    EXPECT_EQ(f.camo.hdr.version, f.camoCopy.hdr.version + 1);
}

TEST(FrameFeatures, ParticlesSampleDepthAndAttribFromCompute)
{
    DeclaredHdrDrawFrame f;
    ASSERT_TRUE(f.run(true));
    ASSERT_TRUE(f.particlesAdded);
    ASSERT_TRUE(f.particles.attrib.valid());
    EXPECT_FALSE(f.translucent.attrib.valid());
    // Neither this frame's SSR capture nor the shadow map.
    ASSERT_TRUE(f.captured.valid());
    EXPECT_FALSE(f.particles.sceneColor.valid());
    EXPECT_FALSE(f.particles.shadow.valid());
    const std::string a      = f.accesses("Particles");
    const std::string attrib = accessOf(a, f.host.graph.findResource("Attrib"));
    EXPECT_NE(attrib.find("\"use\":\"ComputeSrv\",\"read\":true,\"write\":false"), std::string::npos) << attrib;
    EXPECT_NE(a.find("\"use\":\"ComputeSrv\""), std::string::npos) << a;
    // Lighting (opaque) read attrib last: a tracked barrier into a compute-visible shader resource.
    const std::vector<Rg::BarrierRec> b = f.into("Attrib", "Particles");
    ASSERT_EQ(b.size(), 1u);
    EXPECT_TRUE(b[0].tracked);
    EXPECT_EQ(b[0].after.layout, Rg::Layout::ShaderResource);
    EXPECT_TRUE(any(b[0].after.sync & Rg::Sync::ComputeShading));
}

TEST(FrameFeatures, WaterReadsThisFramesCaptureThroughAPlannedBarrier)
{
    DeclaredHdrDrawFrame f;
    ASSERT_TRUE(f.run(true));
    ASSERT_TRUE(f.captured.valid());
    EXPECT_EQ(f.water.sceneColor, f.captured);
    EXPECT_FALSE(f.sky.sceneColor.valid());
    EXPECT_FALSE(f.clouds.sceneColor.valid());
    const std::vector<Rg::BarrierRec> b = f.into("DE.Ssr.SceneColor1", "Water");
    ASSERT_EQ(b.size(), 1u);
    EXPECT_EQ(b[0].before.layout, Rg::Layout::RenderTarget);
    EXPECT_EQ(b[0].after.layout, Rg::Layout::ShaderResource);
    EXPECT_FALSE(b[0].tracked);
    EXPECT_FALSE(f.hasPass("SsrCapture.Handoff"));
}

TEST(FrameFeatures, WaterWithoutACaptureReadsNoSceneColor)
{
    DeclaredHdrDrawFrame f;
    ASSERT_TRUE(f.run(false));
    ASSERT_TRUE(f.waterAdded);
    EXPECT_LT(f.at("SsrCapture.Skip"), f.at("Water"));
    EXPECT_FALSE(f.water.sceneColor.valid());
    EXPECT_EQ(f.water.hdr.version, f.sky.hdr.version + 1);
}

TEST(FrameFeatures, UnderwaterCopiesHdrThenDrawsOntoIt)
{
    DeclaredHdrDrawFrame f;
    ASSERT_TRUE(f.run(true, true));
    EXPECT_TRUE(f.host.features.orderOk());
    ASSERT_TRUE(f.underAdded);
    EXPECT_LT(f.at("Particles"), f.at("Underwater.Copy"));
    EXPECT_LT(f.at("Underwater.Copy"), f.at("Underwater"));
    EXPECT_LT(f.at("Underwater"), f.at("Post"));
    // The draw samples the copy and rewrites the HDR version the copy read.
    EXPECT_EQ(f.underCopy.hdr.index, f.clouds.hdr.index);
    EXPECT_EQ(f.under.copy, f.underCopy.copy);
    EXPECT_EQ(f.under.hdr.version, f.underCopy.hdr.version + 1);
    EXPECT_EQ(f.under.depth, f.clouds.depth);
    EXPECT_EQ(f.under.sceneColor, f.captured); // the water ceiling's SSR

    const std::vector<Rg::BarrierRec> hdrCopy = f.into("Hdr", "Underwater.Copy");
    ASSERT_EQ(hdrCopy.size(), 1u);
    EXPECT_EQ(hdrCopy[0].after.layout, Rg::Layout::CopySource);
    const std::vector<Rg::BarrierRec> dstCopy = f.into("DE.Underwater.HdrCopy", "Underwater.Copy");
    ASSERT_EQ(dstCopy.size(), 1u);
    EXPECT_EQ(dstCopy[0].before.layout, Rg::Layout::ShaderResource);
    EXPECT_EQ(dstCopy[0].after.layout, Rg::Layout::CopyDest);
    const std::vector<Rg::BarrierRec> hdrDraw = f.into("Hdr", "Underwater");
    ASSERT_EQ(hdrDraw.size(), 1u);
    EXPECT_EQ(hdrDraw[0].before.layout, Rg::Layout::CopySource);
    EXPECT_EQ(hdrDraw[0].after.layout, Rg::Layout::RenderTarget);
    const std::vector<Rg::BarrierRec> dstDraw = f.into("DE.Underwater.HdrCopy", "Underwater");
    ASSERT_EQ(dstDraw.size(), 1u);
    EXPECT_EQ(dstDraw[0].before.layout, Rg::Layout::CopyDest);
    EXPECT_EQ(dstDraw[0].after.layout, Rg::Layout::ShaderResource);
    const std::string depth = accessOf(f.accesses("Underwater"), f.host.graph.findResource("Depth"));
    EXPECT_NE(depth.find("\"use\":\"DepthRead\",\"read\":true,\"write\":false"), std::string::npos) << depth;
}

TEST(FrameFeatures, HdrDrawHelpersAddNothingWithoutHdrAndDepth)
{
    // Nothing imported, then HDR without depth (the read-only depth target is required too).
    for (const bool withHdr : { false, true })
    {
        HostFrame          host;
        Rg::RenderGraph&   g     = host.graph;
        uint32_t           count = 0;
        const FeaturePass* list  = sandboxFeatures(count);
        g.beginFrame(1280, 720);
        host.features.begin(list, count);
        if (withHdr)
            host.features.importResource(g, RenderResource::Hdr, fakeResource(1), absDesc(1280, 720, kRgba), Rg::Layout::RenderTarget);
        bool drawAdded = true, underAdded = true, camoAdded = true, particlesAdded = true;
        host.features.declare(PassId::Sky,
                              [&](FeatureBuilder& fb)
                              {
                                  drawAdded = addHdrDrawPass(fb, "Sky", true, [](Rg::PassContext&, const HdrDrawTargets&) {});
                                  fb.opaque("Sky", {}, {}, [](Rg::PassContext&) {});
                              });
        host.features.declare(PassId::Underwater,
                              [&](FeatureBuilder& fb)
                              {
                                  const Rg::TextureHandle copy =
                                      fb.graph().importTexture("DE.Underwater.HdrCopy", absDesc(1280, 720, kRgba), fakeResource(61), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
                                  underAdded = addUnderwaterPasses(fb, copy, [](Rg::PassContext&, const UnderwaterTargets&) {}, [](Rg::PassContext&, const UnderwaterTargets&) {});
                                  fb.opaque("Underwater", {}, {}, [](Rg::PassContext&) {});
                              });
        host.features.declare(PassId::Camouflage,
                              [&](FeatureBuilder& fb)
                              {
                                  const Rg::TextureHandle copy =
                                      fb.graph().importTexture("DE.Camouflage.SceneCopy", absDesc(1280, 720, kRgba), fakeResource(62), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
                                  camoAdded = addCamouflagePasses(fb, copy, [](Rg::PassContext&, const HdrCopyTargets&) {}, [](Rg::PassContext&, const HdrCopyTargets&) {});
                              });
        host.features.declare(PassId::Particles,
                              [&](FeatureBuilder& fb)
                              {
                                  particlesAdded = addHdrDrawPass(fb, "Particles", particlesUses(), [](Rg::PassContext&, const HdrDrawTargets&) {});
                                  fb.opaque("Particles", {}, {}, [](Rg::PassContext&) {});
                              });
        host.features.compileFrame(g, "test");
        EXPECT_FALSE(drawAdded) << withHdr;
        EXPECT_FALSE(underAdded) << withHdr;
        EXPECT_FALSE(camoAdded) << withHdr;
        EXPECT_FALSE(particlesAdded) << withHdr;
    }
}

TEST(FrameFeatures, ParticlesAddNothingWithoutAttrib)
{
    // HDR and depth but no G-buffer attrib: the compute sampling cannot be declared.
    HostFrame          host;
    Rg::RenderGraph&   g     = host.graph;
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    g.beginFrame(1280, 720);
    host.features.begin(list, count);
    host.features.importResource(g, RenderResource::Hdr, fakeResource(1), absDesc(1280, 720, kRgba), Rg::Layout::RenderTarget);
    host.features.importResource(g, RenderResource::Depth, fakeResource(2), absDesc(1280, 720, kD32), Rg::Layout::ShaderResource);
    bool withAttrib = true, plain = false;
    host.features.declare(PassId::Particles,
                          [&](FeatureBuilder& fb)
                          {
                              HdrDrawUses uses;
                              uses.computeDepth  = true;
                              uses.computeAttrib = true;

                              withAttrib = addHdrDrawPass(fb, "Particles", uses, [](Rg::PassContext&, const HdrDrawTargets&) {});
                              plain      = addHdrDrawPass(fb, "Particles.Plain", HdrDrawUses{}, [](Rg::PassContext&, const HdrDrawTargets&) {});
                          });
    host.features.compileFrame(g, "test");
    EXPECT_FALSE(withAttrib);
    EXPECT_TRUE(plain);
}

TEST(FrameFeatures, EditorGridAndGizmosDrawOntoHdrBeforeTheOverlays)
{
    HostFrame                host;
    Rg::RenderGraph&         g     = host.graph;
    uint32_t                 count = 0;
    const FeaturePass*       list  = editorFeatures(count);
    std::vector<std::string> ran;
    HdrDrawTargets           grid{}, gizmos{}, overlays{};
    g.beginFrame(1280, 720);
    host.features.begin(list, count);
    for (uint32_t r = 0; r < kRenderResourceCount; ++r)
    {
        const auto     res    = static_cast<RenderResource>(r);
        const uint32_t fmt    = res == RenderResource::Swap ? kBgra8 : (res == RenderResource::Depth ? kD32 : kRgba);
        Rg::Layout     layout = Rg::Layout::ShaderResource;
        if (res == RenderResource::Swap || res == RenderResource::Hdr)
            layout = Rg::Layout::RenderTarget;
        host.features.importResource(g, res, fakeResource(r), absDesc(1280, 720, fmt), layout);
    }
    const std::initializer_list<PassId> declared = { PassId::EditorGrid, PassId::EditorGizmos, PassId::DebugOverlays };
    for (const PassId id : without(listIds(list, count), declared))
        host.features.set(id, [&ran, id](Rg::PassContext&) { ran.push_back(passName(id)); });
    const auto draw = [&](PassId id, const char* name, HdrDrawTargets* out)
    {
        host.features.declare(id,
                              [&ran, name, out](FeatureBuilder& fb)
                              {
                                  addHdrDrawPass(fb, name, false,
                                                 [&ran, name, out](Rg::PassContext&, const HdrDrawTargets& d)
                                                 {
                                                     ran.push_back(name);
                                                     *out = d;
                                                 });
                              });
    };
    draw(PassId::EditorGrid, "EditorGrid", &grid);
    draw(PassId::EditorGizmos, "EditorGizmos", &gizmos);
    draw(PassId::DebugOverlays, "DebugOverlays", &overlays);
    ASSERT_TRUE(host.features.compileFrame(g, "test"));
    ASSERT_TRUE(g.execute());
    EXPECT_TRUE(host.features.orderOk());
    const auto at = [&](const char* n) { return std::find(ran.begin(), ran.end(), std::string(n)) - ran.begin(); };
    EXPECT_LT(at("Clouds"), at("EditorGrid"));
    EXPECT_LT(at("EditorGrid"), at("EditorGizmos"));
    EXPECT_LT(at("EditorGizmos"), at("DebugOverlays"));
    EXPECT_LT(at("DebugOverlays"), at("Translucent"));
    ASSERT_TRUE(grid.valid() && gizmos.valid() && overlays.valid());
    EXPECT_EQ(gizmos.hdr.version, grid.hdr.version + 1);
    EXPECT_EQ(overlays.hdr.version, gizmos.hdr.version + 1);
    EXPECT_EQ(gizmos.depth, grid.depth);
    EXPECT_EQ(overlays.depth, grid.depth);
    // Opaque Clouds before the grid: one tracked barrier into read-only depth, none after it.
    const std::vector<Rg::BarrierRec> depth    = recordsOn(g, "Depth");
    uint32_t                          intoGrid = 0, intoOthers = 0;
    for (const Rg::BarrierRec& b : depth)
    {
        if (b.pass == Rg::kNoPass || b.pass >= g.passCount())
            continue;
        const std::string name = g.passName(b.pass);
        if (name == "EditorGrid")
        {
            ++intoGrid;
            EXPECT_EQ(b.after.layout, Rg::Layout::DirectQueueGenericRead);
            EXPECT_TRUE(b.tracked);
        }
        else if (name == "EditorGizmos" || name == "DebugOverlays")
            ++intoOthers;
    }
    EXPECT_EQ(intoGrid, 1u);
    EXPECT_EQ(intoOthers, 0u);
}
