#include <gtest/gtest.h>

#include "Render/FrameFeatures.h"
#include "Render/RenderGraph.h"
#include "Render/Renderer.h"
#include "Water/WaterRendering.h"

#include <string>

// The host-owned water passes (Water/WaterRendering.h): which create failures stop a host, and the
// graph entries addWaterPasses / addUnderwaterPasses declare with and without the scene targets.

using namespace Dark;

namespace
{
    Rg::TextureDesc absDesc(uint32_t format)
    {
        Rg::TextureDesc d;
        d.sizeMode = Rg::SizeMode::Absolute;
        d.width    = 320;
        d.height   = 180;
        d.format   = format;
        return d;
    }

    // Declares one entry on the Sandbox list (with or without Depth + HDR imported), compiles it and
    // returns the graph so the test can read its passes. Nothing executes.
    struct WaterFrame
    {
        Rg::RenderGraph graph;
        FeatureFrame    features;

        template <class Declare> bool compile(bool sceneTargets, PassId id, Declare&& declare)
        {
            uint32_t           count = 0;
            const FeaturePass* list  = sandboxFeatures(count);
            graph.beginFrame(320, 180);
            features.begin(list, count);
            if (sceneTargets)
            {
                features.importResource(graph, RenderResource::Depth, nullptr, absDesc(40));
                features.importResource(graph, RenderResource::Hdr, nullptr, absDesc(10));
            }
            features.declare(id, std::forward<Declare>(declare));
            return features.compileFrame(graph, "water");
        }

        std::string passJson(const char* pass) const
        {
            std::string json;
            graph.dumpJson(json);
            const size_t at = json.find(std::string("\"name\":\"") + pass + "\"");
            if (at == std::string::npos)
                return {};
            return json.substr(at, json.find("{\"index\":", at) - at);
        }
        bool hasPass(const char* pass) const
        {
            return graph.findPass(pass) != Rg::kNoPass;
        }
        bool declaresUses(const char* pass) const
        {
            return passJson(pass).find("\"resource\":") != std::string::npos;
        }
    };
} // namespace

TEST(WaterRendering, RequiredWaterThatFailsToCreateFailsCreate)
{
    // No device: both pipelines fail. Sandbox passes waterRequired, so create fails and the host quits.
    Renderer       renderer(RendererTestDevice{});
    WaterRendering water;
    EXPECT_FALSE(water.create(renderer, "Test", true));
    EXPECT_FALSE(water.pipeline().isValid());
    EXPECT_FALSE(water.underwater().isValid());
    water.release();
}

TEST(WaterRendering, OptionalWaterAndUnderwaterThatFailToCreateStillCreate)
{
    // The Editor's water is optional and the underwater view is optional in both hosts: create
    // succeeds with neither pipeline, so the host keeps running without water.
    Renderer       renderer(RendererTestDevice{});
    WaterRendering water;
    EXPECT_TRUE(water.create(renderer, "Test", false));
    EXPECT_TRUE(water.create(renderer, nullptr, false));
    EXPECT_FALSE(water.pipeline().isValid());
    EXPECT_FALSE(water.underwater().isValid());
    water.release();
    EXPECT_FALSE(water.pipeline().isValid());
}

TEST(WaterRendering, WaterDrawsOnTheSceneTargetsWithDeclaredUses)
{
    Renderer       renderer(RendererTestDevice{});
    WaterRendering water;
    WaterFrame     f;
    ASSERT_TRUE(f.compile(true, PassId::Water, [&](FeatureBuilder& b) { water.addWaterPasses(b, renderer, [](Rg::PassContext&, const WaterTables&) {}); }));
    EXPECT_EQ(f.graph.passCount(), 1u);
    ASSERT_TRUE(f.hasPass("Water"));
    EXPECT_TRUE(f.declaresUses("Water"));
}

TEST(WaterRendering, WaterWithoutSceneTargetsIsOneOpaquePass)
{
    Renderer       renderer(RendererTestDevice{});
    WaterRendering water;
    WaterFrame     f;
    ASSERT_TRUE(f.compile(false, PassId::Water, [&](FeatureBuilder& b) { water.addWaterPasses(b, renderer, [](Rg::PassContext&, const WaterTables&) {}); }));
    EXPECT_EQ(f.graph.passCount(), 1u);
    ASSERT_TRUE(f.hasPass("Water"));
    EXPECT_FALSE(f.declaresUses("Water"));
}

TEST(WaterRendering, UnderwaterWithoutItsPipelineDrawsOnlyTheCeiling)
{
    // No underwater pipeline means no HDR copy: no Underwater.Copy pass, and the one Underwater pass
    // draws the ceiling on the scene targets.
    Renderer       renderer(RendererTestDevice{});
    WaterRendering water;
    WaterFrame     f;
    ASSERT_TRUE(f.compile(true, PassId::Underwater,
                          [&](FeatureBuilder& b) { water.addUnderwaterPasses(b, renderer, [](Rg::PassContext&) { return UnderwaterFrame{}; }, [](Rg::PassContext&, const WaterTables&) {}); }));
    EXPECT_EQ(f.graph.passCount(), 1u);
    EXPECT_FALSE(f.hasPass("Underwater.Copy"));
    ASSERT_TRUE(f.hasPass("Underwater"));
    EXPECT_TRUE(f.declaresUses("Underwater"));
}

TEST(WaterRendering, UnderwaterWithoutSceneTargetsIsOneOpaquePass)
{
    Renderer       renderer(RendererTestDevice{});
    WaterRendering water;
    WaterFrame     f;
    ASSERT_TRUE(f.compile(false, PassId::Underwater,
                          [&](FeatureBuilder& b) { water.addUnderwaterPasses(b, renderer, [](Rg::PassContext&) { return UnderwaterFrame{}; }, [](Rg::PassContext&, const WaterTables&) {}); }));
    EXPECT_EQ(f.graph.passCount(), 1u);
    EXPECT_FALSE(f.hasPass("Underwater.Copy"));
    ASSERT_TRUE(f.hasPass("Underwater"));
    EXPECT_FALSE(f.declaresUses("Underwater"));
}
