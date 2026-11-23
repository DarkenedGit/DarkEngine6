#include <gtest/gtest.h>

#include "Render/DebugTileInputs.h"
#include "Render/FrameFeatures.h"
#include "Render/LightingInputs.h"
#include "Render/RenderGraphBarriers.h"
#include "RenderGraphTestUtil.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// The Overlay entry (Render/DebugTileInputs.h): the debug tiles as one declared pass on the Editor
// list, reading what the request names, and the tile layout both hosts share.

using namespace Dark;

namespace
{
    ID3D12Resource* fakeResource(uint32_t r)
    {
        return reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0x100 + r * 0x10));
    }

    Rg::UseState pixelSrv()
    {
        return Rg::useState(Rg::Use::PixelSrv, Rg::ResourceKind::Texture, Rg::Queue::Graphics);
    }

    struct TileFrame
    {
        DebugTileRequest tiles{};
        bool             gtaoOut = true; // GTAO published its full-resolution AO
        bool             ssrOut  = true; // SSR published Full
        bool             ssrConf = true; // and ran its confidence debug

        Rg::RenderGraph   graph;
        FeatureFrame      features;
        DebugTileReads    tile{};
        bool              added = false;
        Rg::TextureHandle ao, ssrFull, conf, swapIn, swapOut;

        bool run()
        {
            uint32_t           count = 0;
            const FeaturePass* list  = editorFeatures(count);
            tile                     = {};
            added                    = false;
            ao = ssrFull = conf = swapIn = swapOut = {};
            graph.beginFrame(1280, 720);
            features.begin(list, count);
            for (uint32_t r = 0; r < kRenderResourceCount; ++r)
            {
                const auto res    = static_cast<RenderResource>(r);
                Rg::Layout layout = Rg::Layout::ShaderResource;
                if (res == RenderResource::Swap || res == RenderResource::Hdr)
                    layout = Rg::Layout::RenderTarget;
                if (res == RenderResource::Shadow || res == RenderResource::LocalShadow || res == RenderResource::Depth)
                    layout = Rg::Layout::DepthStencilWrite;
                features.importResource(graph, res, fakeResource(r), Rg::Test::absTex(1280, 720), layout);
            }
            for (uint32_t i = 0; i < count; ++i)
                if (list[i].id != PassId::Gtao && list[i].id != PassId::Ssr && list[i].id != PassId::Overlay)
                    features.set(list[i].id, [](Rg::PassContext&) {});
            features.declare(PassId::Gtao,
                             [this](FeatureBuilder& b)
                             {
                                 Rg::TextureHandle full = b.graph().importTexture("DE.Gtao.Full", Rg::Test::absTex(1280, 720), fakeResource(70), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
                                 Rg::Test::pass(b.graph(), "Gtao", [&](Rg::PassBuilder& pb) { full = pb.write(full, Rg::Use::RenderTarget); });
                                 if (gtaoOut)
                                     b.graph().blackboard().set(GtaoOutput{ {}, full });
                                 ao = full;
                             });
            features.declare(PassId::Ssr,
                             [this](FeatureBuilder& b)
                             {
                                 Rg::TextureHandle full = b.graph().importTexture("DE.Ssr.Full", Rg::Test::absTex(1280, 720), fakeResource(71), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
                                 Rg::TextureHandle c    = b.graph().importTexture("DE.Ssr.Debug", Rg::Test::absTex(1280, 720), fakeResource(72), Rg::Layout::ShaderResource, Rg::Layout::Undefined);
                                 Rg::Test::pass(b.graph(), "Ssr",
                                                [&](Rg::PassBuilder& pb)
                                                {
                                                    full = pb.write(full, Rg::Use::RenderTarget);
                                                    if (ssrConf)
                                                        c = pb.write(c, Rg::Use::RenderTarget);
                                                });
                                 if (ssrOut)
                                     b.graph().blackboard().set(SsrOutput{ full, ssrConf ? c : Rg::TextureHandle{} });
                                 ssrFull = full;
                                 conf    = ssrConf ? c : Rg::TextureHandle{};
                             });
            features.declare(PassId::Overlay,
                             [this](FeatureBuilder& b)
                             {
                                 swapIn  = b.handle(RenderResource::Swap);
                                 added   = addDebugTilePass(b, tiles, [this](Rg::PassContext&, const DebugTileReads& d) { tile = d; });
                                 swapOut = b.handle(RenderResource::Swap);
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

        std::vector<Rg::BarrierRec> into(const char* resource, const char* pass) const
        {
            std::vector<Rg::BarrierRec> out;
            const uint32_t              p = passIndex(pass);
            for (const Rg::BarrierRec& b : Rg::Test::barriersFor(graph, resource))
                if (b.placement == Rg::BarrierPlacement::BeforePass && b.pass == p)
                    out.push_back(b);
            return out;
        }
    };

    bool same(Rg::TextureHandle a, Rg::TextureHandle b)
    {
        return a.valid() && b.valid() && a.index == b.index && a.version == b.version;
    }
} // namespace

TEST(DebugTiles, NothingRequestedAddsNoPass)
{
    TileFrame f;
    ASSERT_TRUE(f.run());
    EXPECT_TRUE(f.features.orderOk());
    EXPECT_FALSE(f.added);
    EXPECT_EQ(f.passIndex("Overlay"), Rg::kNoPass);
}

TEST(DebugTiles, EveryRequestedTextureIsReadAsAPixelShaderResource)
{
    TileFrame f;
    f.tiles.gbuffer     = true;
    f.tiles.ssao        = true;
    f.tiles.ssrConf     = true;
    f.tiles.depth       = true;
    f.tiles.sunShadow   = true;
    f.tiles.localShadow = true;
    ASSERT_TRUE(f.run());
    EXPECT_TRUE(f.features.orderOk());
    ASSERT_TRUE(f.added);
    ASSERT_NE(f.passIndex("Overlay"), Rg::kNoPass);
    for (const Rg::TextureHandle h : { f.tile.albedo, f.tile.attrib, f.tile.velocity, f.tile.depth, f.tile.shadow, f.tile.localShadow })
        EXPECT_TRUE(h.valid());
    EXPECT_TRUE(same(f.tile.ao, f.ao));
    EXPECT_TRUE(same(f.tile.ssr, f.conf));
    // The back buffer: read and written (the tiles draw over Post's image).
    ASSERT_TRUE(f.tile.swap.valid());
    EXPECT_EQ(f.tile.swap.index, f.swapIn.index);
    EXPECT_GT(f.tile.swap.version, f.swapIn.version);
    EXPECT_TRUE(same(f.swapOut, f.tile.swap)) << "later passes see the tiles' version";
    // GTAO and SSR wrote their targets as render targets: the graph moves them for the tiles.
    for (const char* name : { "DE.Gtao.Full", "DE.Ssr.Debug" })
    {
        const std::vector<Rg::BarrierRec> b = f.into(name, "Overlay");
        ASSERT_EQ(b.size(), 1u) << name;
        EXPECT_EQ(b[0].before.layout, Rg::Layout::RenderTarget) << name;
        EXPECT_EQ(b[0].after, pixelSrv()) << name;
    }
    // Shadow maps in DEPTH_WRITE (nothing else read them here): moved for the tiles.
    for (const char* name : { "Shadow", "LocalShadow" })
    {
        const std::vector<Rg::BarrierRec> b = f.into(name, "Overlay");
        ASSERT_EQ(b.size(), 1u) << name;
        EXPECT_EQ(b[0].after, pixelSrv()) << name;
    }
}

TEST(DebugTiles, OnlyTheRequestedTexturesAreRead)
{
    TileFrame f;
    f.tiles.velocity = true;
    ASSERT_TRUE(f.run());
    ASSERT_TRUE(f.added);
    EXPECT_TRUE(f.tile.velocity.valid());
    for (const Rg::TextureHandle h : { f.tile.albedo, f.tile.attrib, f.tile.depth, f.tile.ao, f.tile.ssr, f.tile.shadow, f.tile.localShadow })
        EXPECT_FALSE(h.valid());
    EXPECT_TRUE(f.into("DE.Gtao.Full", "Overlay").empty());
}

TEST(DebugTiles, TheSsrTileReadsFullOrConfidenceAsAsked)
{
    TileFrame f;
    f.tiles.ssrFull = true;
    ASSERT_TRUE(f.run());
    EXPECT_TRUE(same(f.tile.ssr, f.ssrFull));

    f.tiles         = {};
    f.tiles.ssrConf = true;
    f.ssrConf       = false; // the confidence debug did not run: no tile
    ASSERT_TRUE(f.run());
    ASSERT_TRUE(f.added);
    EXPECT_FALSE(f.tile.ssr.valid());
}

TEST(DebugTiles, WithoutPublishedOutputsTheirTilesAreSkipped)
{
    TileFrame f;
    f.tiles.ssao    = true;
    f.tiles.ssrFull = true;
    f.gtaoOut       = false;
    f.ssrOut        = false;
    ASSERT_TRUE(f.run());
    ASSERT_TRUE(f.added) << "the pass still draws onto the back buffer (nothing to show)";
    EXPECT_FALSE(f.tile.ao.valid());
    EXPECT_FALSE(f.tile.ssr.valid());
}

TEST(DebugTileLayout, TheTileIsAFifthOfTheHeightWithinLimits)
{
    DebugTileRequest r;
    r.depth = true;
    EXPECT_EQ(layoutDebugTiles(r, 1280, 720, 0, 0, 0).depth.w, 144);
    EXPECT_EQ(layoutDebugTiles(r, 640, 360, 0, 0, 0).depth.w, 96);
    EXPECT_EQ(layoutDebugTiles(r, 3840, 2160, 0, 0, 0).depth.w, 220);
    EXPECT_EQ(layoutDebugTiles(r, 3840, 2160, 160, 0, 0).depth.w, 160);
}

TEST(DebugTileLayout, TheTopRowPacksFromTheLeft)
{
    DebugTileRequest r;
    r.gbuffer = r.ssao = r.ssrFull = true;
    const DebugTileLayout l        = layoutDebugTiles(r, 1920, 1080, 160, 0, 0);
    EXPECT_EQ(l.albedo.x, 12);
    EXPECT_EQ(l.albedo.y, 12);
    EXPECT_EQ(l.attrib.x, 12 + 168);
    EXPECT_EQ(l.velocity.x, 12 + 2 * 168) << "velocity comes with the G-buffer";
    EXPECT_EQ(l.ao.x, 12 + 3 * 168);
    EXPECT_EQ(l.ssr.x, 12 + 4 * 168);

    DebugTileRequest v;
    v.velocity = v.ssrConf   = true;
    const DebugTileLayout lv = layoutDebugTiles(v, 1920, 1080, 160, 0, 0);
    EXPECT_EQ(lv.velocity.x, 12);
    EXPECT_EQ(lv.ssr.x, 12 + 168);
    EXPECT_EQ(lv.albedo.w, 0);
}

TEST(DebugTileLayout, TheBottomRowHasDepthThenTheCascadesShrunkToFit)
{
    DebugTileRequest r;
    r.depth = r.sunShadow   = true;
    const DebugTileLayout l = layoutDebugTiles(r, 600, 600, 120, 4, 0);
    EXPECT_EQ(l.depth.x, 12);
    EXPECT_EQ(l.depth.y, 600 - 12 - 120);
    ASSERT_EQ(l.cascadeCount, 4);
    const int32_t x0 = 12 + 120 + 12;
    EXPECT_EQ(l.cascades[0].x, x0);
    const int32_t tw = (600 - 12 - x0 - 3 * 8) / 4; // 4 x 120 + gaps does not fit
    EXPECT_LT(tw, 120);
    EXPECT_EQ(l.cascades[0].w, tw);
    EXPECT_EQ(l.cascades[0].h, 120);
    EXPECT_EQ(l.cascades[3].x, x0 + 3 * (tw + 8));
    EXPECT_LE(l.cascades[3].x + l.cascades[3].w, 600 - 12);
    EXPECT_EQ(layoutDebugTiles(r, 1920, 600, 120, 4, 0).cascades[0].w, 120) << "fits: full size";
}

TEST(DebugTileLayout, LocalSlicesSitAboveAUsedBottomRowElseOnIt)
{
    DebugTileRequest r;
    r.localShadow = true;
    EXPECT_EQ(layoutDebugTiles(r, 1920, 1080, 160, 0, 2).localSlices[0].y, 1080 - 12 - 160);
    r.sunShadow             = true;
    const DebugTileLayout l = layoutDebugTiles(r, 1920, 1080, 160, 4, 2);
    EXPECT_EQ(l.localSlices[0].y, 1080 - 12 - 160 - 8 - 160);
    EXPECT_EQ(l.localSlices[0].x, 12);
    EXPECT_EQ(l.localSlices[1].x, 12 + 168);
    EXPECT_EQ(l.localCount, 2);
    EXPECT_EQ(layoutDebugTiles(r, 1920, 1080, 160, 20, 0).cascadeCount, DebugTileLayout::kMaxSlices);
}
