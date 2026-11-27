#include <gtest/gtest.h>

#include "Render/FrameFeatures.h"
#include "Render/GBufferInputs.h"
#include "Render/MeshConstants.h"
#include "Render/SceneBuffers.h"
#include "Render/SurfaceTag.h"
#include "RenderGraphTestUtil.h"

#include <cstdint>

// SurfaceTag G-buffer target (render-graph plan §3.4.2): a default frame does not declare the write;
// writeSurfaceTag adds a fifth R8G8 MRT. MeshGBufferConstants spends the last skinned G-buffer DWORD.

using namespace Dark;
using Rg::Test::absTex;

namespace
{
    ID3D12Resource* fakeResource(uint32_t r)
    {
        return reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(r + 1) << 12);
    }

    void importAll(FeatureFrame& features, Rg::RenderGraph& g)
    {
        for (uint32_t r = 0; r < kRenderResourceCount; ++r)
        {
            const auto res = static_cast<RenderResource>(r);
            const uint32_t fmt = res == RenderResource::Depth ? Rg::Test::kD32 : Rg::Test::kRgba16F;
            features.importResource(g, res, fakeResource(r), absTex(1280, 720, fmt), Rg::Layout::RenderTarget);
        }
    }
}

TEST(SurfaceTag, EngineCategoriesAreStable)
{
    EXPECT_EQ(static_cast<uint8_t>(SurfaceCategory::StaticMesh), 0u);
    EXPECT_EQ(static_cast<uint8_t>(SurfaceCategory::SkinnedMesh), 1u);
    EXPECT_EQ(static_cast<uint8_t>(SurfaceCategory::Terrain), 2u);
    EXPECT_EQ(static_cast<uint8_t>(SurfaceCategory::Grass), 3u);
    EXPECT_EQ(static_cast<uint8_t>(SurfaceCategory::Foliage), 4u);
    EXPECT_EQ(kSurfaceTagLutSize, 256u);
    EXPECT_STREQ(renderResourceName(RenderResource::SurfaceTag), "SurfaceTag");
    EXPECT_EQ(static_cast<uint32_t>(RenderResource::SurfaceTag) + 1u, kRenderResourceCount);
}

TEST(SurfaceTag, PackOverrideUsesBitEight)
{
    EXPECT_FALSE(surfaceTagOverridePacked(0));
    const uint32_t packed = packSurfaceTagOverride(7);
    EXPECT_TRUE(surfaceTagOverridePacked(packed));
    EXPECT_EQ(surfaceTagPackedValue(packed), 7u);
    EXPECT_EQ(surfaceTagPackedValue(packSurfaceTagOverride(0)), 0u);
    EXPECT_TRUE(surfaceTagOverridePacked(packSurfaceTagOverride(0)));
}

TEST(SurfaceTag, MeshGBufferConstantsSpendsTheLastSkinnedDword)
{
    EXPECT_EQ(sizeof(MeshGBufferConstants), 59u * sizeof(float));
    EXPECT_EQ(59u + 1u + 2u + 2u, 64u);
}

TEST(SurfaceTag, TargetClearAndFormatStayOffTheLightingTable)
{
    EXPECT_FLOAT_EQ(SceneBuffers::kSurfaceTagClear[0], 0.0f);
    EXPECT_FLOAT_EQ(SceneBuffers::kSurfaceTagClear[1], 0.0f);
    EXPECT_EQ(SceneBuffers::kLightingCount, 11u);
    EXPECT_EQ(SceneBuffers::kLightingColorRemap, 10u);
}

TEST(SurfaceTag, DefaultGBufferPassDoesNotDeclareTheTagWrite)
{
    Rg::RenderGraph    graph;
    FeatureFrame       features;
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    graph.beginFrame(1280, 720);
    features.begin(list, count);
    importAll(features, graph);
    GBufferTargets off{};
    features.declare(PassId::GBuffer, [&](FeatureBuilder& fb) { addGBufferPass(fb, [&](Rg::PassContext&, const GBufferTargets& d) { off = d; }, false); });
    ASSERT_TRUE(features.compileFrame(graph, "test"));
    ASSERT_TRUE(graph.execute());
    EXPECT_TRUE(off.albedo.valid());
    EXPECT_FALSE(off.surfaceTag.valid());
}

TEST(SurfaceTag, WriteSurfaceTagDeclaresTheFifthMrt)
{
    Rg::RenderGraph    graph;
    FeatureFrame       features;
    uint32_t           count = 0;
    const FeaturePass* list  = sandboxFeatures(count);
    graph.beginFrame(1280, 720);
    features.begin(list, count);
    importAll(features, graph);
    GBufferTargets on{};
    features.declare(PassId::GBuffer, [&](FeatureBuilder& fb) { addGBufferPass(fb, [&](Rg::PassContext&, const GBufferTargets& d) { on = d; }, true); });
    ASSERT_TRUE(features.compileFrame(graph, "test"));
    ASSERT_TRUE(graph.execute());
    EXPECT_TRUE(on.albedo.valid());
    EXPECT_TRUE(on.surfaceTag.valid());
    EXPECT_NE(on.surfaceTag, on.albedo);
}

TEST(SurfaceTag, PlanCacheMissesWhenTheTagWriteAppears)
{
    Rg::RenderGraph graph;
    auto            compile = [&](bool write) {
        FeatureFrame       features;
        uint32_t           count = 0;
        const FeaturePass* list  = sandboxFeatures(count);
        graph.beginFrame(1280, 720);
        features.begin(list, count);
        importAll(features, graph);
        features.declare(PassId::GBuffer, [write](FeatureBuilder& fb) { addGBufferPass(fb, [](Rg::PassContext&, const GBufferTargets&) {}, write); });
        return features.compileFrame(graph, "test");
    };
    ASSERT_TRUE(compile(false));
    const uint64_t missesAfterOff = graph.planCacheMisses();
    ASSERT_GE(missesAfterOff, 1u);
    ASSERT_TRUE(compile(true));
    EXPECT_EQ(graph.planCacheMisses(), missesAfterOff + 1u);
    ASSERT_TRUE(compile(true));
    EXPECT_EQ(graph.planCacheMisses(), missesAfterOff + 1u);
}
