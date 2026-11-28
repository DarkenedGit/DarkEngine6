#include "RenderGraphTestUtil.h"

#include <cstdint>
#include <string>
#include <vector>

// Transient placement (plan §3.8): sizes from the device sizer, aligned regions in one heap,
// usage bits for the resource flags, and handing placed resources to the plan.

using namespace Dark::Rg;
using namespace Dark::Rg::Test;

namespace
{
    // T1 [A,B], T2 [B,C], T3 [C,D]: T1 and T3 share memory, T2 overlaps both. Depth is a transient
    // depth target read by D.
    void chain(RenderGraph& g, bool withDepth = false)
    {
        g.beginFrame(512, 512);
        const TextureHandle bb = g.importBackBuffer(nullptr, 512, 512, kBgra8);
        TextureHandle       t1, t2, t3, d;
        pass(g, "A",
             [&](PassBuilder& b)
             {
                 t1 = b.write(b.create("T1", tex()));
                 if (withDepth)
                     d = b.write(b.create("Depth", tex(kD32)), Use::DepthWrite);
             });
        pass(g, "B",
             [&](PassBuilder& b)
             {
                 b.read(t1);
                 t2 = b.write(b.create("T2", tex()));
             });
        pass(g, "C",
             [&](PassBuilder& b)
             {
                 b.read(t2);
                 t3 = b.write(b.create("T3", tex()));
             });
        pass(g, "D",
             [&](PassBuilder& b)
             {
                 b.read(t3);
                 if (withDepth)
                     b.read(d, Use::PixelSrv);
                 b.write(bb);
             });
    }

    struct SizerLog
    {
        std::vector<uint8_t> usages;
        uint64_t             bytes     = 3'000'000; // not a multiple of any alignment
        uint64_t             alignment = 4ull << 20;
    };

    TransientSize testSizer(void* user, uint32_t width, uint32_t height, const TextureDesc&, uint8_t usage)
    {
        SizerLog* log = static_cast<SizerLog*>(user);
        log->usages.push_back(usage);
        return { width && height ? log->bytes : 0, log->alignment };
    }

    ResourceInfo info(const RenderGraph& g, const char* name)
    {
        return g.resourceInfo(g.findResource(name));
    }
} // namespace

TEST(RenderGraphPlacement, RegionsAreAlignedDisjointAndInsideTheHeap)
{
    RenderGraph g;
    chain(g);
    ASSERT_TRUE(compiles(g));
    const ResourceInfo t1 = info(g, "T1"), t2 = info(g, "T2"), t3 = info(g, "T3");
    EXPECT_EQ(t1.heapOffset, t3.heapOffset); // same region, disjoint in time
    EXPECT_NE(t1.heapOffset, t2.heapOffset);
    ASSERT_EQ(g.aliasSlots().size(), 2u);
    uint64_t end = 0;
    for (const AliasSlot& s : g.aliasSlots())
    {
        EXPECT_EQ(s.offset % s.alignment, 0u);
        EXPECT_GE(s.offset, end);
        end = s.offset + s.bytes;
    }
    EXPECT_EQ(g.transientHeapBytes(), end);
    EXPECT_EQ(g.transientHeapAlignment(), kPlacementAlignment);
    EXPECT_EQ(info(g, "backbuffer").heapOffset, kNoHeapOffset);
    std::string why;
    EXPECT_TRUE(g.validateCompiled(&why)) << why;
}

TEST(RenderGraphPlacement, SizerSetsBytesAndAlignment)
{
    SizerLog    log;
    RenderGraph g;
    g.setTransientSizer(&testSizer, &log);
    chain(g);
    ASSERT_TRUE(compiles(g));
    const ResourceInfo t1 = info(g, "T1");
    EXPECT_EQ(t1.alignment, log.alignment);
    EXPECT_EQ(t1.bytes, log.alignment); // 3 MB rounded to the 4 MiB alignment
    EXPECT_EQ(g.transientHeapAlignment(), log.alignment);
    for (const AliasSlot& s : g.aliasSlots())
    {
        EXPECT_EQ(s.offset % log.alignment, 0u);
        EXPECT_EQ(s.bytes % log.alignment, 0u);
    }
    EXPECT_EQ(g.transientHeapBytes(), 2 * log.alignment);
    EXPECT_EQ(log.usages.size(), 3u); // one call per live transient
    std::string why;
    EXPECT_TRUE(g.validateCompiled(&why)) << why;
}

TEST(RenderGraphPlacement, UsageBitsFollowTheLiveAccesses)
{
    RenderGraph g;
    chain(g, true);
    ASSERT_TRUE(compiles(g));
    EXPECT_EQ(info(g, "T1").usage, kUsageRenderTarget | kUsageShaderResource);
    EXPECT_EQ(info(g, "Depth").usage, kUsageDepthStencil | kUsageShaderResource);
}

TEST(RenderGraphPlacement, NoAliasingGivesEveryTransientItsOwnRegion)
{
    RenderGraph    g;
    CompileOptions o;
    o.noAliasing = true;
    g.setOptions(o);
    chain(g);
    ASSERT_TRUE(compiles(g));
    EXPECT_EQ(g.aliasSlots().size(), 3u);
    EXPECT_NE(info(g, "T1").heapOffset, info(g, "T3").heapOffset);
    EXPECT_EQ(g.transientHeapBytes(), g.transientBytesUnaliased());
}

TEST(RenderGraphPlacement, SettingTheSizerInvalidatesTheCachedPlan)
{
    SizerLog    log;
    RenderGraph g;
    chain(g);
    ASSERT_TRUE(compiles(g));
    chain(g);
    ASSERT_TRUE(compiles(g));
    EXPECT_TRUE(g.compiledFromCache());
    g.setTransientSizer(&testSizer, &log);
    chain(g);
    ASSERT_TRUE(compiles(g));
    EXPECT_FALSE(g.compiledFromCache());
    EXPECT_EQ(info(g, "T1").alignment, log.alignment);
}

TEST(RenderGraphPlacement, PlaceTransientOnlyTakesLiveTransients)
{
    RenderGraph g;
    chain(g);
    auto* fake = reinterpret_cast<ID3D12Resource*>(uintptr_t{ 0x1000 });
    EXPECT_FALSE(g.placeTransient(g.findResource("T1"), fake, {})); // not compiled yet
    ASSERT_TRUE(compiles(g));
    TextureViews v;
    v.rtv = 0x40;
    v.srv = 0x80;
    EXPECT_TRUE(g.placeTransient(g.findResource("T1"), fake, v));
    EXPECT_EQ(g.nativeResource(g.findResource("T1")), fake);
    EXPECT_EQ(g.views(TextureHandle{ g.findResource("T1"), 0 }).rtv, 0x40u);
    EXPECT_FALSE(g.placeTransient(g.findResource("backbuffer"), fake, v)); // imported
    EXPECT_FALSE(g.placeTransient(g.findResource("T1"), nullptr, v));
    EXPECT_FALSE(g.placeTransient(9999, fake, v));
}

TEST(RenderGraphPlacement, UnplaceableTransientHasNoBytes)
{
    SizerLog log;
    log.bytes = 0;
    RenderGraph g;
    g.setTransientSizer(&testSizer, &log);
    chain(g);
    ASSERT_TRUE(compiles(g));
    EXPECT_EQ(info(g, "T1").bytes, 0u);
    EXPECT_EQ(g.aliasSlots().size(), 3u); // nothing shares a zero-byte region
}
