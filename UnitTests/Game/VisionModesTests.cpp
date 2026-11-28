#include <gtest/gtest.h>

#include "Game/VisionModes.h"
#include "Render/FrameFeatures.h"
#include "Render/FrameSetup.h"
#include "Render/FrameView.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// VisionModes (Game/VisionModes.h): what each vision picture asks of the engine through FrameSetup,
// and the heat pass's graph uses.

using namespace Dark;

namespace
{
    // A setup with every field away from its default, so "unchanged" is a real check.
    FrameSetup busySetup()
    {
        FrameSetup s;
        s.disabled.set(EnginePass::Bloom);
        s.shadowStrength    = 0.5f;
        s.aoStrength        = 0.25f;
        s.ssrStrength       = 0.75f;
        s.emissiveGain      = 0.5f;
        s.fogRemap.strength = 0.3f;
        s.albedoRemap.aux   = 0.2f;
        return s;
    }

    struct ApplyHook : IFrameSetupHook
    {
        VisionDecision decision;
        VisionLut      lut = makeHeatLut();
        const char*    name() const override
        {
            return "VisionModes";
        }
        void setupFrame(const FrameView&, FrameSetup& setup) override
        {
            applyVisionModes(decision, lut, setup);
        }
    };

    struct OtherHook : IFrameSetupHook
    {
        const char* name() const override
        {
            return "Other";
        }
        void setupFrame(const FrameView&, FrameSetup& setup) override
        {
            setup.disabled.set(EnginePass::MotionBlur);
        }
    };

    std::string readFile(const std::filesystem::path& p)
    {
        std::ifstream     f(p);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    // `static const float <name> = <value>f;` in an HLSL include; NaN when missing.
    float hlslConstant(const std::string& text, const char* name)
    {
        const std::string key = std::string("static const float ") + name;
        const size_t      at  = text.find(key);
        if (at == std::string::npos)
            return std::numeric_limits<float>::quiet_NaN();
        const size_t eq = text.find('=', at + key.size());
        if (eq == std::string::npos || text.find_first_not_of(' ', at + key.size()) != eq)
            return std::numeric_limits<float>::quiet_NaN();
        return std::strtof(text.c_str() + eq + 1, nullptr);
    }

    std::filesystem::path gameShaderDir()
    {
        return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "content" / "game" / "shaders";
    }
} // namespace

TEST(VisionModes, OffLeavesTheSetupUnchanged)
{
    const VisionLut lut = makeHeatLut();
    for (const bool ready : { false, true })
    {
        FrameSetup s = busySetup();
        applyVisionModes(decideVision({}, ready), lut, s);
        EXPECT_EQ(s, busySetup());
    }
    FrameSetup d;
    applyVisionModes(decideVision({ 0.0f, 0.0f }, true), lut, d);
    EXPECT_EQ(d, FrameSetup{});
}

TEST(VisionModes, HeatWithoutItsPassTurnsNothingOff)
{
    // An optional feature that cannot draw must not take a core pass away.
    const VisionDecision d = decideVision({ 1.0f, 0.0f }, false);
    EXPECT_EQ(d.picture, VisionPicture::Off);
    FrameSetup s = busySetup();
    applyVisionModes(d, makeHeatLut(), s);
    EXPECT_EQ(s, busySetup());
}

TEST(VisionModes, FullHeatReplacesTheLitPassesAndWritesSurfaceTag)
{
    const VisionLut      lut = makeHeatLut();
    const VisionDecision d   = decideVision({ 1.0f, 0.0f }, true);
    ASSERT_EQ(d.picture, VisionPicture::Heat);
    EXPECT_FLOAT_EQ(d.strength, 1.0f);
    FrameSetup s;
    applyVisionModes(d, lut, s);
    for (const EnginePass p : { EnginePass::DirectionalShadows, EnginePass::LocalShadows, EnginePass::Gtao, EnginePass::Ssr, EnginePass::DeferredLighting, EnginePass::LocalLights,
                                EnginePass::SsrCapture, EnginePass::Underwater })
        EXPECT_TRUE(s.disabled.has(p)) << enginePassName(p);
    // Sky stays for the sky pixels' depth; bloom carries the hot stop; post and the rest stay.
    for (const EnginePass p : { EnginePass::Sky, EnginePass::Clouds, EnginePass::Decals, EnginePass::Bloom, EnginePass::Taa, EnginePass::MotionBlur, EnginePass::AutoExposure })
        EXPECT_FALSE(s.disabled.has(p)) << enginePassName(p);
    EXPECT_TRUE(s.writeSurfaceTag);
    EXPECT_EQ(s.surfaceTagLut, lut.data());
    EXPECT_EQ(s.surfaceTagLutSize, kSurfaceTagLutSize);
    EXPECT_EQ(s.emissiveGain, 0.0f);
    EXPECT_EQ(s.albedoRemap, ColorRemap{});
    EXPECT_EQ(s.skyRemap, ColorRemap{});
}

TEST(VisionModes, HeatFadeKeepsLightingAndOnlyDropsUnderwater)
{
    FrameSetup s;
    applyVisionModes(decideVision({ 0.4f, 0.0f }, true), makeHeatLut(), s);
    EXPECT_EQ(s.disabled.bits, EnginePassMask::bit(EnginePass::Underwater));
    EXPECT_TRUE(s.writeSurfaceTag);
    EXPECT_FLOAT_EQ(decideVision({ 0.4f, 0.0f }, true).strength, 0.4f);
}

TEST(VisionModes, UltravioletShiftsBeforeLightingAndKeepsItOn)
{
    const float uv = 0.6f;
    FrameSetup  s;
    applyVisionModes(decideVision({ 0.0f, uv }, false), makeHeatLut(), s);
    EXPECT_EQ(s.disabled.bits, EnginePassMask::bit(EnginePass::Underwater));
    EXPECT_FALSE(s.writeSurfaceTag);
    EXPECT_EQ(s.surfaceTagLut, nullptr);
    EXPECT_EQ(s.emissiveGain, 0.0f);
    EXPECT_EQ(s.albedoRemap, beeShiftRemap(kVisionUvSurface, uv));
    EXPECT_EQ(s.fogRemap, beeShiftRemap(kVisionUvFog, uv));
    EXPECT_EQ(s.skyRemap, beeShiftRemap(kVisionUvSky, uv));
    EXPECT_EQ(s.waterRemap, beeShiftRemap(kVisionUvWater, uv));
}

TEST(VisionModes, BeeShiftRemapIsTheChannelShift)
{
    const float src[3] = { 0.9f, 0.4f, 0.2f };
    float       dst[3] = {};
    applyColorRemap(dst, src, beeShiftRemap(0.75f, 1.0f));
    EXPECT_FLOAT_EQ(dst[0], 0.4f);
    EXPECT_FLOAT_EQ(dst[1], 0.2f);
    EXPECT_FLOAT_EQ(dst[2], 0.75f);
    applyColorRemap(dst, src, beeShiftRemap(0.75f, 0.5f));
    EXPECT_FLOAT_EQ(dst[0], 0.5f * 0.9f + 0.5f * 0.4f);
    EXPECT_FLOAT_EQ(dst[2], 0.5f * 0.2f + 0.5f * 0.75f);
}

TEST(VisionModes, UltravioletWinsOverInfrared)
{
    const VisionDecision d = decideVision({ 1.0f, 0.3f }, true);
    EXPECT_EQ(d.picture, VisionPicture::Ultraviolet);
    EXPECT_FLOAT_EQ(d.strength, 0.3f);
    EXPECT_EQ(decideVision({ 2.0f, 0.0f }, true).strength, 1.0f);
    EXPECT_EQ(decideVision({ -1.0f, 0.0f }, true).picture, VisionPicture::Off);
}

TEST(VisionModes, HeatLutGivesEachSurfaceCategoryItsClass)
{
    const VisionLut lut = makeHeatLut();
    EXPECT_FLOAT_EQ(lut[static_cast<uint32_t>(SurfaceCategory::StaticMesh)], 0.41f);
    EXPECT_FLOAT_EQ(lut[static_cast<uint32_t>(SurfaceCategory::SkinnedMesh)], 0.72f);
    EXPECT_FLOAT_EQ(lut[static_cast<uint32_t>(SurfaceCategory::Terrain)], 0.41f);
    EXPECT_FLOAT_EQ(lut[static_cast<uint32_t>(SurfaceCategory::Grass)], 0.40f);
    EXPECT_FLOAT_EQ(lut[static_cast<uint32_t>(SurfaceCategory::Foliage)], 0.40f);
    EXPECT_FLOAT_EQ(lut[200], kVisionHeatAmbient);
    EXPECT_FLOAT_EQ(lut[255], kVisionHeatAmbient);
}

TEST(VisionModes, ResolvedThroughTheHooksItOnlyAddsBits)
{
    FrameSetupHooks hooks;
    ApplyHook       vision;
    OtherHook       other;
    ASSERT_TRUE(hooks.add(&other));
    ASSERT_TRUE(hooks.add(&vision));
    FrameView view;
    vision.decision     = decideVision({ 1.0f, 0.0f }, true);
    const FrameSetup on = hooks.resolve(view, 0.0);
    EXPECT_EQ(on.disabled.bits, kVisionHeatFullDisabled | EnginePassMask::bit(EnginePass::MotionBlur));
    EXPECT_TRUE(on.writeSurfaceTag);
    vision.decision = {};
    FrameSetup expectOff;
    expectOff.disabled.set(EnginePass::MotionBlur);
    EXPECT_EQ(hooks.resolve(view, 1.0), expectOff);
}

TEST(VisionModes, ConstantsMatchTheGameShaderIncludes)
{
    const std::string thermal = readFile(gameShaderDir() / "Thermal.hlsli");
    const std::string uv      = readFile(gameShaderDir() / "Ultraviolet.hlsli");
    ASSERT_FALSE(thermal.empty());
    ASSERT_FALSE(uv.empty());
    EXPECT_FLOAT_EQ(hlslConstant(thermal, "kThermalSky"), kVisionHeatSky);
    EXPECT_FLOAT_EQ(hlslConstant(thermal, "kThermalMetal"), kVisionHeatMetal);
    EXPECT_FLOAT_EQ(hlslConstant(uv, "kUvSky"), kVisionUvSky);
    EXPECT_FLOAT_EQ(hlslConstant(uv, "kUvWater"), kVisionUvWater);
    EXPECT_FLOAT_EQ(hlslConstant(uv, "kUvFog"), kVisionUvFog);
    // The palette's ground and body stops are the ambient and body classes.
    EXPECT_NE(thermal.find("if (h < 0.41f)"), std::string::npos);
    EXPECT_NE(thermal.find("if (h < 0.72f)"), std::string::npos);
}

namespace
{
    constexpr uint32_t kRgba = 10;
    constexpr uint32_t kD32  = 40;

    struct GameSlotFrame
    {
        Rg::RenderGraph   graph;
        FeatureFrame      features;
        int               heatRuns = 0;
        bool              declared = false;
        VisionHeatUses    uses;
        Rg::TextureHandle hdrBefore, hdrAfter, tagSeen;

        bool run(bool withHeat)
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
                d.format   = static_cast<RenderResource>(r) == RenderResource::Depth ? kD32 : kRgba;
                features.importResource(graph, static_cast<RenderResource>(r), reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0x100 + r * 0x10)), d);
            }
            for (uint32_t i = 0; i < count; ++i)
                if (list[i].id != PassId::Game)
                    features.set(list[i].id, [](Rg::PassContext&) {});
            declared = false;
            features.declare(PassId::Game,
                             [this, withHeat](FeatureBuilder& b)
                             {
                                 hdrBefore = b.handle(RenderResource::Hdr);
                                 tagSeen   = b.handle(RenderResource::SurfaceTag);
                                 if (withHeat)
                                     declared = declareVisionHeatPass(b, [this](Rg::PassContext&, const VisionHeatUses& d) { ++heatRuns, uses = d; });
                                 hdrAfter = b.handle(RenderResource::Hdr);
                             });
            if (!features.compileFrame(graph, "vision-test"))
                return false;
            return graph.execute();
        }
    };
} // namespace

TEST(VisionModes, HeatPassReadsTheGBufferAndSurfaceTagAndWritesHdr)
{
    GameSlotFrame plain;
    ASSERT_TRUE(plain.run(false));
    const uint32_t plainPasses = plain.graph.passCount();
    EXPECT_EQ(plain.hdrAfter, plain.hdrBefore);

    GameSlotFrame heat;
    ASSERT_TRUE(heat.run(true));
    EXPECT_TRUE(heat.declared);
    EXPECT_EQ(heat.heatRuns, 1);
    EXPECT_EQ(heat.graph.passCount(), plainPasses + 1);
    bool found = false;
    for (uint32_t p = 0; p < heat.graph.passCount(); ++p)
        found = found || std::string(heat.graph.passName(p)) == kVisionHeatPassName;
    EXPECT_TRUE(found);
    EXPECT_NE(heat.hdrAfter, heat.hdrBefore); // later passes see the heat pass's HDR
    EXPECT_TRUE(heat.uses.albedo.valid());
    EXPECT_TRUE(heat.uses.attrib.valid());
    EXPECT_TRUE(heat.uses.depth.valid());
    EXPECT_TRUE(heat.uses.surfaceTag.valid());
    EXPECT_TRUE(heat.uses.hdr.valid());
    EXPECT_TRUE(heat.features.orderOk());
}
