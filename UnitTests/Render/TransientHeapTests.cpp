#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <gtest/gtest.h>

#include "Render/TransientHeap.h"

using namespace Dark;

// Transient heap policy and descs (render-graph plan §3.8), device-free.

TEST(TransientHeap, CapacityStartsAt256GrowsBy64AndStopsAt768)
{
    constexpr uint64_t MiB = 1ull << 20;
    EXPECT_EQ(transientCapacity(1, 0), 256 * MiB);
    EXPECT_EQ(transientCapacity(256 * MiB, 0), 256 * MiB);
    EXPECT_EQ(transientCapacity(256 * MiB + 1, 0), 320 * MiB);
    EXPECT_EQ(transientCapacity(700 * MiB, 0), 704 * MiB);
    EXPECT_EQ(transientCapacity(768 * MiB, 0), 768 * MiB);
    EXPECT_EQ(transientCapacity(768 * MiB + 1, 0), 0u);
    // Held memory that fits is kept (no shrink on a smaller plan); growth starts from the first block.
    EXPECT_EQ(transientCapacity(100 * MiB, 384 * MiB), 384 * MiB);
    EXPECT_EQ(transientCapacity(400 * MiB, 384 * MiB), 448 * MiB);
}

TEST(TransientHeap, BudgetWarningAbove85Percent)
{
    EXPECT_FALSE(transientBudgetExceeded(85, 100));
    EXPECT_TRUE(transientBudgetExceeded(86, 100));
    EXPECT_FALSE(transientBudgetExceeded(7'000'000'000ull, 8'589'934'592ull));
    EXPECT_TRUE(transientBudgetExceeded(7'400'000'000ull, 8'589'934'592ull));
    EXPECT_FALSE(transientBudgetExceeded(1, 0)); // no budget reported
}

TEST(TransientHeap, DescFlagsFollowUsage)
{
    Rg::TextureDesc color;
    color.format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
    D3D12_RESOURCE_DESC rd = transientResourceDesc(640, 360, color, Rg::kUsageRenderTarget | Rg::kUsageShaderResource);
    EXPECT_EQ(rd.Width, 640u);
    EXPECT_EQ(rd.Height, 360u);
    EXPECT_EQ(rd.Format, DXGI_FORMAT_R16G16B16A16_FLOAT);
    EXPECT_EQ(rd.Flags, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    EXPECT_EQ(rd.MipLevels, 1u);

    rd = transientResourceDesc(64, 64, color, Rg::kUsageUnordered | Rg::kUsageShaderResource);
    EXPECT_EQ(rd.Flags, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    color.mips = Rg::kAllMips;
    EXPECT_EQ(transientResourceDesc(64, 64, color, Rg::kUsageShaderResource).MipLevels, 0u); // full chain
}

TEST(TransientHeap, SampledDepthIsTypelessWithDepthAndSrvViews)
{
    Rg::TextureDesc depth;
    depth.format                 = DXGI_FORMAT_D32_FLOAT;
    const D3D12_RESOURCE_DESC rd = transientResourceDesc(128, 128, depth, Rg::kUsageDepthStencil | Rg::kUsageShaderResource);
    EXPECT_EQ(rd.Format, DXGI_FORMAT_R32_TYPELESS);
    EXPECT_EQ(rd.Flags, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    EXPECT_EQ(transientDsvFormat(depth), DXGI_FORMAT_D32_FLOAT);
    EXPECT_EQ(transientSrvFormat(depth), DXGI_FORMAT_R32_FLOAT);

    const D3D12_RESOURCE_DESC only = transientResourceDesc(128, 128, depth, Rg::kUsageDepthStencil);
    EXPECT_EQ(only.Format, DXGI_FORMAT_D32_FLOAT);
    EXPECT_EQ(only.Flags, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
}

TEST(TransientHeap, UnplaceableDescsHaveNoWidth)
{
    Rg::TextureDesc color;
    color.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    EXPECT_EQ(transientResourceDesc(0, 64, color, Rg::kUsageRenderTarget).Width, 0u);
    EXPECT_EQ(transientResourceDesc(64, 64, color, Rg::kUsageDepthStencil).Width, 0u); // not a depth format
    Rg::TextureDesc none;
    EXPECT_EQ(transientResourceDesc(64, 64, none, Rg::kUsageRenderTarget).Width, 0u);
}

TEST(TransientHeap, WithoutADeviceNothingIsPlacedAndAnEmptyPlanIsFine)
{
    TransientHeap   heap;
    Rg::RenderGraph g;
    g.beginFrame(64, 64);
    const Rg::TextureHandle bb = g.importBackBuffer(nullptr, 64, 64, DXGI_FORMAT_B8G8R8A8_UNORM);
    g.addPass<Rg::NoData>("Present", [&](Rg::PassBuilder& b, Rg::NoData&) { b.write(bb, Rg::Use::RenderTarget); }, [](Rg::PassContext&, const Rg::NoData&) {});
    ASSERT_TRUE(g.compile());
    EXPECT_TRUE(heap.realize(g)); // no transients: nothing allocated
    EXPECT_EQ(heap.capacity(), 0u);
    EXPECT_EQ(heap.placedCount(), 0u);
    EXPECT_EQ(TransientHeap::sizeTransient(&heap, 64, 64, Rg::TextureDesc{}, Rg::kUsageRenderTarget).bytes, 0u);
}

TEST(TransientHeap, ATransientWithoutAnAllocatorFailsTheFrame)
{
    TransientHeap   heap;
    Rg::RenderGraph g;
    g.beginFrame(64, 64);
    const Rg::TextureHandle bb = g.importBackBuffer(nullptr, 64, 64, DXGI_FORMAT_B8G8R8A8_UNORM);
    Rg::TextureDesc         d;
    d.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    Rg::TextureHandle t;
    g.addPass<Rg::NoData>("A", [&](Rg::PassBuilder& b, Rg::NoData&) { t = b.write(b.create("T", d), Rg::Use::RenderTarget); }, [](Rg::PassContext&, const Rg::NoData&) {});
    g.addPass<Rg::NoData>(
        "B",
        [&](Rg::PassBuilder& b, Rg::NoData&)
        {
            b.read(t, Rg::Use::PixelSrv);
            b.write(bb, Rg::Use::RenderTarget);
        },
        [](Rg::PassContext&, const Rg::NoData&) {});
    ASSERT_TRUE(g.compile());
    EXPECT_FALSE(heap.realize(g)); // the frame must not run with an unplaced transient
    EXPECT_EQ(g.nativeResource(g.findResource("T")), nullptr);
}
