#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <gtest/gtest.h>

#include "Game/CamouflageFeature.h"
#include "Render/FrameFeatures.h"
#include "Render/Renderer.h"

#include <cstdint>
#include <string>

using namespace Dark;

namespace
{
    // One Sandbox frame with every list entry but the game slot set as an empty host pass, and the
    // slot running `feature` (when non-null). Returns the graph's pass count.
    uint32_t runFrame(Rg::RenderGraph& graph, FeatureFrame& features, Renderer& renderer, const WorldDrawContext& draws, CamouflageFeature* feature)
    {
        uint32_t           count = 0;
        const FeaturePass* list  = sandboxFeatures(count);
        graph.beginFrame(1280, 720);
        features.begin(list, count);
        for (uint32_t r = 0; r < kRenderResourceCount; ++r)
        {
            Rg::TextureDesc d;
            d.sizeMode = Rg::SizeMode::Absolute;
            d.width    = 1280;
            d.height   = 720;
            d.format   = static_cast<RenderResource>(r) == RenderResource::Depth ? 40u : 10u;
            features.importResource(graph, static_cast<RenderResource>(r), reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0x100 + r * 0x10)), d);
        }
        for (uint32_t i = 0; i < count; ++i)
            if (list[i].id != PassId::Game)
                features.set(list[i].id, [](Rg::PassContext&) {});
        if (feature)
            features.declare(PassId::Game, [&](FeatureBuilder& b) { feature->addPasses(b, renderer, draws); });
        EXPECT_TRUE(features.compileFrame(graph, "camouflage-test"));
        EXPECT_TRUE(graph.execute());
        return graph.passCount();
    }
} // namespace

TEST(CamouflageFeature, UncreatedFeatureAddsNoPassEvenWithADraw)
{
    MeshPipeline           mesh;
    MeshPipeline           meshTransparent;
    SkinnedMeshPipeline    skinned;
    SkinnedMeshPipeline    skinnedTransparent;
    SkinningUploadRing     ring;
    const WorldDrawContext draws{ mesh, meshTransparent, skinned, skinnedTransparent, ring };
    Renderer               renderer{ RendererTestDevice{} };
    Rg::RenderGraph        graph;
    FeatureFrame           features;
    const uint32_t         plain = runFrame(graph, features, renderer, draws, nullptr);

    CamouflageFeature feature;
    EXPECT_FALSE(feature.isValid());
    int  drawn = 0;
    auto body  = [&drawn](Rg::PassContext&, D3D12_GPU_DESCRIPTOR_HANDLE, const WorldDrawContext&) { ++drawn; };
    feature.setFrameDraw(CamouflageFeature::Draw::of(body));
    EXPECT_EQ(runFrame(graph, features, renderer, draws, &feature), plain);
    for (uint32_t p = 0; p < graph.passCount(); ++p)
        EXPECT_EQ(std::string(graph.passName(p)).find("Camouflage"), std::string::npos) << graph.passName(p);
    for (const PassId id : features.compiledOrder())
        EXPECT_NE(id, PassId::Game);
    EXPECT_TRUE(features.orderOk());
    EXPECT_EQ(drawn, 0);
    feature.release();
    EXPECT_FALSE(feature.isValid());
}
