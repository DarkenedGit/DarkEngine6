#include <gtest/gtest.h>

#include "Render/FrameFeatures.h"
#include "Render/HdrDrawInputs.h"
#include "Render/LightingInputs.h"
#include "Render/RenderGraphBarriers.h"
#include "Render/ResourceStateTracker.h"
#include "Render/ShadowInputs.h"
#include "RenderGraphTestUtil.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// The shadow entries with declared uses (Render/ShadowInputs.h): the Sandbox list with Shadows,
// LocalShadows and every shadow reader declared the way SceneRenderer declares them, and the
// frame-start invariant FrameComposer keeps with endShadowFrame.

using namespace Dark;

namespace
{
    constexpr uint32_t kBgra8 = 87;
    constexpr uint32_t kD32   = 40;
    constexpr uint32_t kRgba  = 10;

    ID3D12Resource* fakeResource(uint32_t r)
    {
        return reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0x100 + r * 0x10));
    }
    ID3D12Resource* mapResource()
    {
        return fakeResource(static_cast<uint32_t>(RenderResource::Shadow));
    }
    ID3D12Resource* localResource()
    {
        return fakeResource(static_cast<uint32_t>(RenderResource::LocalShadow));
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

    Rg::UseState depthWrite()
    {
        return Rg::useState(Rg::Use::DepthWrite, Rg::ResourceKind::Texture, Rg::Queue::Graphics);
    }
    Rg::UseState pixelSrv()
    {
        return Rg::useState(Rg::Use::PixelSrv, Rg::ResourceKind::Texture, Rg::Queue::Graphics);
    }

    std::vector<PassId> listIds(const FeaturePass* list, uint32_t count)
    {
        std::vector<PassId> ids;
        for (uint32_t i = 0; i < count; ++i)
            ids.push_back(list[i].id);
        return ids;
    }

    struct ShadowFrame
    {
        // What the host has this frame.
        bool       importShadow = true;
        bool       importLocal  = true;
        bool       localFaces   = true;                          // LocalShadowSystem selected faces
        bool       underwater   = false;                         // the Underwater copy path
        Rg::Layout shadowLayout = Rg::Layout::DepthStencilWrite; // as the tracker holds them at begin()
        Rg::Layout localLayout  = Rg::Layout::ShaderResource;

        Rg::RenderGraph          graph;
        FeatureFrame             features;
        std::vector<std::string> ran;
        ShadowTargets            shadows{}, local{};
        LightingReads            lit{}, vol{};
        HdrDrawTargets           sky{}, water{}, translucent{}, overlays{};
        UnderwaterTargets        under{};
        bool                     localAdded = false;

        bool run()
        {
            uint32_t           count = 0;
            const FeaturePass* list  = sandboxFeatures(count);
            ran.clear();
            shadows = local = {};
            lit = vol = {};
            sky = water = translucent = overlays = {};
            under                                = {};
            localAdded                           = false;
            graph.beginFrame(1280, 720);
            features.begin(list, count);
            for (uint32_t r = 0; r < kRenderResourceCount; ++r)
            {
                const auto     res    = static_cast<RenderResource>(r);
                const uint32_t fmt    = res == RenderResource::Swap ? kBgra8 : (res == RenderResource::Depth ? kD32 : kRgba);
                Rg::Layout     layout = Rg::Layout::ShaderResource;
                if (res == RenderResource::Swap || res == RenderResource::Hdr)
                    layout = Rg::Layout::RenderTarget;
                if (res == RenderResource::Shadow)
                {
                    if (!importShadow)
                        continue;
                    layout = shadowLayout;
                }
                if (res == RenderResource::LocalShadow)
                {
                    if (!importLocal)
                        continue;
                    layout = localLayout;
                }
                features.importResource(graph, res, fakeResource(r), absDesc(1280, 720, fmt), layout);
            }
            const std::vector<PassId> declared = { PassId::Shadows, PassId::LocalShadows, PassId::Lighting,      PassId::LocalVolumes, PassId::Sky,
                                                   PassId::Water,   PassId::Translucent,  PassId::DebugOverlays, PassId::Underwater };
            for (const PassId id : listIds(list, count))
                if (std::find(declared.begin(), declared.end(), id) == declared.end())
                    features.set(id, [this, id](Rg::PassContext&) { ran.push_back(passName(id)); });

            features.declare(PassId::Shadows,
                             [this](FeatureBuilder& b)
                             {
                                 addShadowPass(b,
                                               [this](Rg::PassContext&, const ShadowTargets& d)
                                               {
                                                   ran.push_back("Shadows");
                                                   shadows = d;
                                               });
                             });
            features.declare(PassId::LocalShadows,
                             [this](FeatureBuilder& b)
                             {
                                 // SceneRenderer::addLocalShadowPasses: no pass on a frame without faces.
                                 if (!localFaces)
                                     return;
                                 localAdded = addLocalShadowPass(b,
                                                                 [this](Rg::PassContext&, const ShadowTargets& d)
                                                                 {
                                                                     ran.push_back("LocalShadows");
                                                                     local = d;
                                                                 });
                             });
            const auto lighting = [this](LightingPass pass, const char* name, LightingReads* out)
            {
                features.declare(pass == LightingPass::Lighting ? PassId::Lighting : PassId::LocalVolumes,
                                 [this, pass, name, out](FeatureBuilder& b)
                                 {
                                     const LightingReads in = lightingReads(b);
                                     Rg::TextureHandle   hdr;
                                     b.addPass<LightingReads>(
                                         name, [&](Rg::PassBuilder& pb, LightingReads& d) { hdr = declareLightingUses(pb, in, pass, d); },
                                         [this, name, out](Rg::PassContext&, const LightingReads& d)
                                         {
                                             ran.push_back(name);
                                             *out = d;
                                         });
                                     b.set(RenderResource::Hdr, hdr);
                                 });
            };
            lighting(LightingPass::Lighting, "Lighting", &lit);
            lighting(LightingPass::LocalVolumes, "LocalVolumes", &vol);
            const auto draw = [this](PassId id, const char* name, HdrDrawUses uses, HdrDrawTargets* out)
            {
                features.declare(id,
                                 [this, name, uses, out](FeatureBuilder& b)
                                 {
                                     addHdrDrawPass(b, name, uses,
                                                    [this, name, out](Rg::PassContext&, const HdrDrawTargets& d)
                                                    {
                                                        ran.push_back(name);
                                                        *out = d;
                                                    });
                                 });
            };
            draw(PassId::Sky, "Sky", skyUses(), &sky);
            draw(PassId::Water, "Water", waterUses(), &water);
            draw(PassId::Translucent, "Translucent", translucentUses(), &translucent);
            draw(PassId::DebugOverlays, "DebugOverlays", HdrDrawUses{}, &overlays);
            features.declare(PassId::Underwater,
                             [this](FeatureBuilder& b)
                             {
                                 const auto ceiling = [this](Rg::PassContext&, const UnderwaterTargets& d)
                                 {
                                     ran.push_back("Underwater");
                                     under = d;
                                 };
                                 if (underwater)
                                 {
                                     const Rg::TextureHandle copy =
                                         b.graph().importTexture("DE.Underwater.HdrCopy", absDesc(1280, 720, kRgba), fakeResource(61), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
                                     addUnderwaterPasses(b, copy, [](Rg::PassContext&, const UnderwaterTargets&) {}, ceiling);
                                     return;
                                 }
                                 addHdrDrawPass(b, "Underwater", waterUses(),
                                                [this](Rg::PassContext&, const HdrDrawTargets& d)
                                                {
                                                    ran.push_back("Underwater");
                                                    under.hdr    = d.hdr;
                                                    under.depth  = d.depth;
                                                    under.shadow = d.shadow;
                                                });
                             });
            if (!features.compileFrame(graph, "test"))
                return false;
            return graph.execute();
        }

        uint32_t passIndex(const char* name) const
        {
            for (uint32_t i = 0; i < graph.passCount(); ++i)
                if (std::string(graph.passName(i)) == name)
                    return i;
            return Rg::kNoPass;
        }

        std::vector<Rg::BarrierRec> on(const char* resource) const
        {
            return Rg::Test::barriersFor(graph, resource);
        }

        std::vector<Rg::BarrierRec> into(const char* resource, const char* pass) const
        {
            std::vector<Rg::BarrierRec> out;
            const uint32_t              p = passIndex(pass);
            for (const Rg::BarrierRec& b : on(resource))
                if (b.placement == Rg::BarrierPlacement::BeforePass && b.pass == p)
                    out.push_back(b);
            return out;
        }

        long at(const char* name) const
        {
            return static_cast<long>(std::find(ran.begin(), ran.end(), std::string(name)) - ran.begin());
        }
    };

    // What the executor does with the plan: each barrier's before-state comes from the tracker and its
    // after-state goes back in. The layouts must agree (rg.validate); a barrier after a declared use
    // carries that use's sync and access, which must agree too. A first barrier from an import
    // carries NO_ACCESS as a placeholder (the tracker's state is recorded).
    void applyPlan(const Rg::RenderGraph& g, const char* resource, ResourceStateTracker& tracker, const void* native)
    {
        for (const Rg::BarrierRec& b : Rg::Test::barriersFor(g, resource))
        {
            Rg::UseState before;
            ASSERT_TRUE(tracker.transition(native, b.after, before)) << resource << ": the plan records a barrier the tracker would skip";
            EXPECT_EQ(before.layout, b.before.layout) << resource << " into pass " << b.pass;
            if (b.before.access != Rg::Access::NoAccess)
            {
                EXPECT_EQ(before, b.before) << resource << " into pass " << b.pass;
            }
        }
    }
} // namespace

TEST(ShadowInputs, FrameStartStateIsTheDepthWriteUse)
{
    const Rg::UseState s = shadowFrameStartState();
    EXPECT_EQ(s.layout, Rg::Layout::DepthStencilWrite);
    EXPECT_EQ(s.sync, Rg::Sync::DepthStencil);
    EXPECT_EQ(s.access, Rg::Access::DepthStencilWrite);
    EXPECT_EQ(s, depthWrite());
}

TEST(ShadowInputs, EndShadowFrameReturnsAReadMapToItsWriteState)
{
    ResourceStateTracker tracker;
    const void*          map = mapResource();
    Rg::UseState         before;
    EXPECT_FALSE(endShadowFrame(tracker, map, before)) << "untracked";
    EXPECT_FALSE(endShadowFrame(tracker, nullptr, before));

    tracker.track(map, Rg::ResourceKind::Texture, shadowFrameStartState());
    EXPECT_FALSE(endShadowFrame(tracker, map, before)) << "already writable (shadows on, no reader ran)";

    ASSERT_TRUE(tracker.transition(map, pixelSrv(), before));
    EXPECT_TRUE(endShadowFrame(tracker, map, before));
    EXPECT_EQ(before, pixelSrv());
    ASSERT_NE(tracker.current(map), nullptr);
    EXPECT_EQ(*tracker.current(map), shadowFrameStartState());
}

TEST(ShadowInputs, ShadowsWritesWithoutABarrierAndTheFirstReaderWaitsForIt)
{
    ShadowFrame f;
    ASSERT_TRUE(f.run());
    EXPECT_TRUE(f.features.orderOk());
    ASSERT_TRUE(f.shadows.map.valid());
    EXPECT_LT(f.at("Shadows"), f.at("GBuffer"));

    // The map starts the frame writable: nothing before the pass (its cascades may be on the
    // shadow list, which no main-list barrier precedes).
    EXPECT_TRUE(f.into("Shadow", "Shadows").empty());
    // One barrier for the whole frame: depth writes -> pixel-shader reads, before Lighting.
    const std::vector<Rg::BarrierRec> all = f.on("Shadow");
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].pass, f.passIndex("Lighting"));
    EXPECT_EQ(all[0].placement, Rg::BarrierPlacement::BeforePass);
    EXPECT_FALSE(all[0].tracked);
    EXPECT_EQ(all[0].before, shadowFrameStartState());
    EXPECT_EQ(all[0].after, pixelSrv());

    // Every sampler reads the version Shadows wrote; LocalVolumes does not read it.
    for (const Rg::TextureHandle h : { f.lit.shadow, f.sky.shadow, f.water.shadow, f.translucent.shadow, f.under.shadow })
    {
        ASSERT_TRUE(h.valid());
        EXPECT_EQ(h.index, f.shadows.map.index);
        EXPECT_EQ(h.version, f.shadows.map.version);
    }
    EXPECT_FALSE(f.vol.shadow.valid());
    EXPECT_FALSE(f.overlays.shadow.valid());
    EXPECT_FALSE(f.lit.localShadow.valid());
}

TEST(ShadowInputs, TheUnderwaterCopyPathReadsTheMapToo)
{
    ShadowFrame f;
    f.underwater = true;
    ASSERT_TRUE(f.run());
    ASSERT_TRUE(f.under.shadow.valid());
    EXPECT_EQ(f.under.shadow.index, f.shadows.map.index);
    EXPECT_EQ(f.on("Shadow").size(), 1u);
}

TEST(ShadowInputs, LocalShadowsMoveTheArrayIntoDepthWritesAndBackForLocalVolumes)
{
    ShadowFrame f; // the array left as last frame's LocalVolumes read it
    ASSERT_TRUE(f.run());
    ASSERT_TRUE(f.localAdded);
    EXPECT_LT(f.at("LocalShadows"), f.at("GBuffer"));
    const std::vector<Rg::BarrierRec> all = f.on("LocalShadow");
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].pass, f.passIndex("LocalShadows"));
    EXPECT_EQ(all[0].before.layout, Rg::Layout::ShaderResource);
    EXPECT_EQ(all[0].after, depthWrite());
    EXPECT_EQ(all[1].pass, f.passIndex("LocalVolumes"));
    EXPECT_EQ(all[1].before, depthWrite());
    EXPECT_EQ(all[1].after, pixelSrv());
    ASSERT_TRUE(f.vol.localShadow.valid());
    EXPECT_EQ(f.vol.localShadow.index, f.local.map.index);
    EXPECT_EQ(f.vol.localShadow.version, f.local.map.version);
}

TEST(ShadowInputs, ANewLocalArrayNeedsNoBarrierBeforeItsFirstFaces)
{
    ShadowFrame f;
    f.localLayout = Rg::Layout::DepthStencilWrite; // created in DEPTH_WRITE
    ASSERT_TRUE(f.run());
    EXPECT_TRUE(f.into("LocalShadow", "LocalShadows").empty());
    EXPECT_EQ(f.into("LocalShadow", "LocalVolumes").size(), 1u);
}

TEST(ShadowInputs, NoFacesMeansNoLocalShadowsPassButLocalVolumesStillReadsTheArray)
{
    ShadowFrame f;
    f.localFaces = false;
    ASSERT_TRUE(f.run());
    EXPECT_TRUE(f.features.orderOk());
    EXPECT_EQ(f.passIndex("LocalShadows"), Rg::kNoPass);
    ASSERT_TRUE(f.vol.localShadow.valid());
    EXPECT_TRUE(f.on("LocalShadow").empty()) << "already a shader resource";

    f.localLayout = Rg::Layout::DepthStencilWrite; // never written yet: moved for the read
    ASSERT_TRUE(f.run());
    ASSERT_EQ(f.on("LocalShadow").size(), 1u);
    EXPECT_EQ(f.on("LocalShadow")[0].pass, f.passIndex("LocalVolumes"));
}

TEST(ShadowInputs, WithoutMapsTheReadersBindFallbacksAndShadowsStillRuns)
{
    ShadowFrame f;
    f.importShadow = false;
    f.importLocal  = false;
    ASSERT_TRUE(f.run());
    EXPECT_TRUE(f.features.orderOk());
    // The pass also records the frame's GPU cull, so it runs with nothing to write.
    EXPECT_LT(f.at("Shadows"), f.at("GBuffer"));
    EXPECT_FALSE(f.shadows.map.valid());
    EXPECT_FALSE(f.localAdded);
    EXPECT_EQ(f.passIndex("LocalShadows"), Rg::kNoPass);
    for (const Rg::TextureHandle h : { f.lit.shadow, f.sky.shadow, f.water.shadow, f.translucent.shadow, f.under.shadow, f.vol.localShadow })
        EXPECT_FALSE(h.valid());
}

TEST(ShadowInputs, TheTrackerAndThePlanAgreeFrameAfterFrame)
{
    // Both arrays are created in DEPTH_WRITE; FrameComposer imports them in the tracker's layout
    // and calls endShadowFrame after the graph.
    ResourceStateTracker tracker;
    tracker.track(mapResource(), Rg::ResourceKind::Texture, depthWrite());
    tracker.track(localResource(), Rg::ResourceKind::Texture, depthWrite());
    ShadowFrame f;
    for (int frame = 0; frame < 3; ++frame)
    {
        SCOPED_TRACE(frame);
        f.localFaces   = frame != 1; // a frame without local faces in the middle
        f.shadowLayout = tracker.current(mapResource())->layout;
        f.localLayout  = tracker.current(localResource())->layout;
        EXPECT_EQ(f.shadowLayout, Rg::Layout::DepthStencilWrite);
        ASSERT_TRUE(f.run());
        EXPECT_TRUE(f.into("Shadow", "Shadows").empty());
        applyPlan(f.graph, "Shadow", tracker, mapResource());
        applyPlan(f.graph, "LocalShadow", tracker, localResource());
        EXPECT_EQ(*tracker.current(localResource()), pixelSrv()) << "left for the debug tiles and next frame's faces";

        Rg::UseState before;
        ASSERT_TRUE(endShadowFrame(tracker, mapResource(), before));
        EXPECT_EQ(before, pixelSrv());
        EXPECT_EQ(*tracker.current(mapResource()), shadowFrameStartState());
    }
}
