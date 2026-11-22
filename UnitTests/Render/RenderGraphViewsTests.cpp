#include "RenderGraphTestUtil.h"

#include <cstdint>
#include <vector>

// PassContext views and descriptor tables (render-graph plan Phase 6 part 2): a converted pass gets
// the CPU views given with an import or a history slot, only for uses it declared, and srvTable
// hands the executor the declared SRVs in order to copy into the descriptor ring.

using namespace Dark::Rg;
using namespace Dark::Rg::Test;

namespace
{
    ID3D12Resource* fake(uintptr_t v)
    {
        return reinterpret_cast<ID3D12Resource*>(v);
    }

    TextureViews views(uint64_t base)
    {
        TextureViews v;
        v.srv         = base + 1;
        v.rtv         = base + 2;
        v.dsv         = base + 3;
        v.dsvReadOnly = base + 4;
        return v;
    }

    // Copies tables into a fake ring: returns 0x10000 + the call index, and keeps the sources.
    class TableExecutor : public Executor
    {
    public:
        uint64_t descriptorTable(const uint64_t* cpu, uint32_t count) override
        {
            tables.emplace_back(cpu, cpu + count);
            return 0x10000 + tables.size();
        }
        std::vector<std::vector<uint64_t>> tables;
    };

    struct Seen
    {
        uint64_t rtv = 0, dsv = 0, dsvRo = 0, srv = 0, table = 0, undeclared = 0;
    };
} // namespace

TEST(RenderGraphViews, DeclaredUsesGetTheImportViews)
{
    RenderGraph g;
    g.beginFrame(64, 64);
    const TextureHandle bb    = g.importBackBuffer(fake(0x10), 64, 64, kBgra8);
    TextureHandle       color = g.importTexture("Color", absTex(64, 64), fake(0x20), Layout::RenderTarget, Layout::Undefined);
    TextureHandle       depth = g.importTexture("Depth", absTex(64, 64, kD32), fake(0x30), Layout::DepthStencilWrite, Layout::Undefined);
    g.setViews(color, views(0x100));
    g.setViews(depth, views(0x200));
    g.setViews(bb, views(0x300));
    EXPECT_EQ(g.views(color).rtv, 0x102u);

    Seen draw, post;
    g.addPass<NoData>(
        "Draw",
        [&](PassBuilder& b, NoData&)
        {
            color = b.write(color, Use::RenderTarget);
            b.read(depth, Use::DepthRead);
        },
        [&](PassContext& c, const NoData&)
        {
            draw.rtv        = c.rtv(color);
            draw.dsvRo      = c.dsv(depth, true);
            draw.dsv        = c.dsv(depth); // only read-only depth was declared
            draw.undeclared = c.srvCpu(color); // written, not read
        });
    g.addPass<NoData>(
        "Post",
        [&](PassBuilder& b, NoData&)
        {
            b.read(color, Use::PixelSrv);
            b.read(depth, Use::PixelSrv);
            b.write(bb, Use::RenderTarget);
        },
        [&](PassContext& c, const NoData&)
        {
            post.rtv   = c.rtv(bb);
            post.srv   = c.srvCpu(color);
            post.table = c.srvTable({ color, depth });
        });
    ASSERT_TRUE(compiles(g));
    TableExecutor exec;
    ASSERT_TRUE(g.execute(&exec));

    EXPECT_EQ(draw.rtv, 0x102u);
    EXPECT_EQ(draw.dsvRo, 0x204u);
    EXPECT_EQ(draw.dsv, 0u);
    EXPECT_EQ(draw.undeclared, 0u);
    EXPECT_EQ(post.rtv, 0x302u);
    EXPECT_EQ(post.srv, 0x101u);
    EXPECT_EQ(post.table, 0x10001u);
    ASSERT_EQ(exec.tables.size(), 1u);
    EXPECT_EQ(exec.tables[0], (std::vector<uint64_t>{ 0x101, 0x201 }));
}

TEST(RenderGraphViews, SrvTableWithAnUndeclaredOrMissingViewCopiesNothing)
{
    RenderGraph g;
    g.beginFrame(64, 64);
    const TextureHandle bb      = g.importBackBuffer(fake(0x10), 64, 64, kBgra8);
    const TextureHandle a       = g.importTexture("A", absTex(64, 64), fake(0x20), Layout::ShaderResource, Layout::Undefined);
    const TextureHandle noViews = g.importTexture("NoViews", absTex(64, 64), fake(0x30), Layout::ShaderResource, Layout::Undefined);
    const TextureHandle other   = g.importTexture("Other", absTex(64, 64), fake(0x40), Layout::ShaderResource, Layout::Undefined);
    g.setViews(a, views(0x100));
    g.setViews(other, views(0x400));
    uint64_t missing = 1, undeclared = 1, ok = 0;
    g.addPass<NoData>(
        "P",
        [&](PassBuilder& b, NoData&)
        {
            b.read(a, Use::PixelSrv);
            b.read(noViews, Use::PixelSrv);
            b.write(bb, Use::RenderTarget);
        },
        [&](PassContext& c, const NoData&)
        {
            missing    = c.srvTable({ a, noViews });
            undeclared = c.srvTable({ a, other });
            ok         = c.srvTable({ a });
        });
    ASSERT_TRUE(compiles(g));
    TableExecutor exec;
    ASSERT_TRUE(g.execute(&exec));
    EXPECT_EQ(missing, 0u);
    EXPECT_EQ(undeclared, 0u);
    EXPECT_NE(ok, 0u);
    ASSERT_EQ(exec.tables.size(), 1u); // only the complete table reached the ring
}

TEST(RenderGraphViews, NoExecutorMeansNoTable)
{
    RenderGraph g;
    g.beginFrame(64, 64);
    const TextureHandle bb = g.importBackBuffer(fake(0x10), 64, 64, kBgra8);
    const TextureHandle a  = g.importTexture("A", absTex(64, 64), fake(0x20), Layout::ShaderResource, Layout::Undefined);
    g.setViews(a, views(0x100));
    uint64_t table = 1;
    g.addPass<NoData>(
        "P",
        [&](PassBuilder& b, NoData&)
        {
            b.read(a, Use::PixelSrv);
            b.write(bb, Use::RenderTarget);
        },
        [&](PassContext& c, const NoData&) { table = c.srvTable({ a }); });
    ASSERT_TRUE(compiles(g));
    ASSERT_TRUE(g.execute());
    EXPECT_EQ(table, 0u);
}

TEST(RenderGraphViews, HistorySlotsTakeANativeResourceAndViews)
{
    RenderGraph g;
    g.beginFrame(64, 64);
    const TextureHandle bb   = g.importBackBuffer(fake(0x10), 64, 64, kBgra8);
    HistoryPair         hist = g.history("Taa", absTex(64, 64));
    ASSERT_TRUE(hist.curr.valid());
    g.setNative(hist.curr, fake(0x50));
    g.setViews(hist.curr, views(0x500));
    TextureHandle transient;
    uint64_t      rtv = 0;
    g.addPass<NoData>(
        "Taa",
        [&](PassBuilder& b, NoData&)
        {
            transient = b.create("T", tex());
            transient = b.write(transient);
            hist.curr = b.write(hist.curr, Use::RenderTarget);
        },
        [&](PassContext& c, const NoData&) { rtv = c.rtv(hist.curr); });
    g.addPass<NoData>(
        "Out",
        [&](PassBuilder& b, NoData&)
        {
            b.read(transient);
            b.write(bb);
        },
        [](PassContext&, const NoData&) {});
    g.setNative(transient, fake(0x60)); // transients get memory in Phase 9, not here
    ASSERT_TRUE(compiles(g));
    ASSERT_TRUE(g.execute());
    EXPECT_EQ(rtv, 0x502u);
    EXPECT_EQ(g.nativeResource(hist.curr.index), fake(0x50));
    EXPECT_EQ(g.nativeResource(transient.index), nullptr);
}

// The Post entry's shape (SceneRenderer::addPostPasses): metering, bloom (additive steps read their
// render target), then tonemap. The compiled order must be the insertion order (the umbrella
// profiler scopes open in the first pass and close in the last), nothing is culled (the readback
// copy is a SideEffect; every other pass writes an import), and an additive step gets the RTV of
// the target it read-writes.
TEST(RenderGraphViews, PostChainKeepsInsertionOrderAndAdditiveStepsGetTheirRtv)
{
    RenderGraph g;
    g.beginFrame(64, 64);
    const TextureHandle bb    = g.importBackBuffer(fake(0x10), 64, 64, kBgra8);
    TextureHandle       hdr   = g.importTexture("Hdr", absTex(64, 64), fake(0x20), Layout::RenderTarget, Layout::Undefined);
    const TextureHandle depth = g.importTexture("Depth", absTex(64, 64, kD32), fake(0x30), Layout::DepthStencilWrite, Layout::Undefined);
    TextureHandle       lvl[2] = { g.importTexture("L0", absTex(16, 16), fake(0x40), Layout::RenderTarget, Layout::Undefined),
                                   g.importTexture("L1", absTex(1, 1), fake(0x50), Layout::RenderTarget, Layout::Undefined) };
    TextureHandle       mip[2] = { g.importTexture("M0", absTex(32, 32), fake(0x60), Layout::RenderTarget, Layout::Undefined),
                                   g.importTexture("M1", absTex(16, 16), fake(0x70), Layout::RenderTarget, Layout::Undefined) };
    g.setViews(bb, views(0x100));
    g.setViews(hdr, views(0x200));
    g.setViews(depth, views(0x300));
    g.setViews(lvl[0], views(0x400));
    g.setViews(lvl[1], views(0x500));
    g.setViews(mip[0], views(0x600));
    g.setViews(mip[1], views(0x700));

    std::vector<const char*> ran;
    std::vector<uint64_t>    rtvs;
    const auto step = [&](const char* name, TextureHandle src, TextureHandle& dst, bool additive)
    {
        g.addPass<NoData>(
            name,
            [&](PassBuilder& b, NoData&)
            {
                b.read(src, Use::PixelSrv);
                dst = additive ? b.readWrite(dst, Use::RenderTarget) : b.write(dst, Use::RenderTarget);
            },
            [&ran, &rtvs, name, out = dst](PassContext& c, const NoData&)
            {
                ran.push_back(name);
                rtvs.push_back(c.rtv(out));
            });
    };
    g.addPass<NoData>(
        "AE.Reduce",
        [&](PassBuilder& b, NoData&)
        {
            b.read(hdr, Use::PixelSrv);
            b.read(depth, Use::PixelSrv);
            lvl[0] = b.write(lvl[0], Use::RenderTarget);
        },
        [&](PassContext&, const NoData&) { ran.push_back("AE.Reduce"); });
    step("AE.Down", lvl[0], lvl[1], false);
    g.addPass<NoData>(
        "AE.Readback",
        [&](PassBuilder& b, NoData&)
        {
            b.flags(PassFlags::SideEffect);
            b.read(lvl[1], Use::CopySrc);
        },
        [&](PassContext&, const NoData&) { ran.push_back("AE.Readback"); });
    step("Bloom.Extract", hdr, mip[0], false);
    step("Bloom.Down", mip[0], mip[1], false);
    step("Bloom.Up", mip[1], mip[0], true);
    step("Bloom.Composite", mip[0], hdr, true);
    g.addPass<NoData>(
        "Tonemap",
        [&](PassBuilder& b, NoData&)
        {
            b.read(hdr, Use::PixelSrv);
            b.read(depth, Use::PixelSrv);
            b.write(bb, Use::RenderTarget);
        },
        [&](PassContext& c, const NoData&)
        {
            ran.push_back("Tonemap");
            rtvs.push_back(c.rtv(bb));
        });
    ASSERT_TRUE(compiles(g));
    EXPECT_EQ(g.order(), (std::vector<uint32_t>{ 0, 1, 2, 3, 4, 5, 6, 7 }));
    TableExecutor exec;
    ASSERT_TRUE(g.execute(&exec));
    ASSERT_EQ(ran.size(), 8u);
    EXPECT_STREQ(ran[2], "AE.Readback");
    EXPECT_STREQ(ran[7], "Tonemap");
    // AE.Down, Extract, Down, Up (mip 0), Composite (HDR), Tonemap (back buffer).
    EXPECT_EQ(rtvs, (std::vector<uint64_t>{ 0x502, 0x602, 0x702, 0x602, 0x202, 0x102 }));
}
