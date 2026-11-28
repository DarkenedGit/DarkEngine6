#pragma once

#include "Render/FrameFeatures.h"
#include "Render/FrameSetup.h"
#include "Render/SurfaceTag.h"

#include <array>
#include <cstdint>
#include <utility>

// The game's vision modes as frame setup (render-graph plan §3.4.2). CPU-only: what a mode asks
// of the engine through FrameSetup, the heat classes per surface tag, and the heat pass's graph
// uses. VisionModesFeature (Game/VisionModesFeature.h) owns the GPU side.

namespace Dark
{

    // Strengths 0..1. Ultraviolet wins when both are above 0 (the host keeps them exclusive).
    struct VisionModes
    {
        float infrared    = 0.0f;
        float ultraviolet = 0.0f;
    };

    enum class VisionPicture : uint8_t
    {
        Off,
        Heat,
        Ultraviolet,
    };

    struct VisionDecision
    {
        VisionPicture picture  = VisionPicture::Off;
        float         strength = 0.0f; // 0..1, the picture's blend over the lit frame
    };

    // Heat classes. Must match the Thermal.hlsli palette stops; VisionModesTests compares them.
    inline constexpr float kVisionHeatAmbient    = 0.41f; // static meshes, terrain, unknown tags
    inline constexpr float kVisionHeatBody       = 0.72f; // skinned meshes
    inline constexpr float kVisionHeatVegetation = 0.40f; // grass and foliage
    inline constexpr float kVisionHeatSky        = 0.15f; // kThermalSky
    inline constexpr float kVisionHeatMetal      = 0.28f; // kThermalMetal

    // Ultraviolet reflectance fed to the bee shift's aux channel. Must match Ultraviolet.hlsli.
    inline constexpr float kVisionUvSky   = 0.75f;
    inline constexpr float kVisionUvWater = 0.05f;
    inline constexpr float kVisionUvFog   = 0.80f;
    // Lighting's remap takes one aux for every surface: the terrain class.
    inline constexpr float kVisionUvSurface = 0.12f;

    // Passes a full-strength heat picture replaces. Every bit is an EnginePass a hook may turn off.
    inline constexpr uint32_t kVisionHeatFullDisabled = EnginePassMask::bit(EnginePass::DirectionalShadows) | EnginePassMask::bit(EnginePass::LocalShadows) | EnginePassMask::bit(EnginePass::Gtao) |
                                                        EnginePassMask::bit(EnginePass::Ssr) | EnginePassMask::bit(EnginePass::DeferredLighting) | EnginePassMask::bit(EnginePass::LocalLights) |
                                                        EnginePassMask::bit(EnginePass::SsrCapture) | EnginePassMask::bit(EnginePass::Underwater);
    // Any vision picture draws above the water line only.
    inline constexpr uint32_t kVisionAnyDisabled = EnginePassMask::bit(EnginePass::Underwater);
    static_assert((kVisionHeatFullDisabled & ~kEnginePassesHonoured) == 0, "every heat bit is a pass a hook may turn off");

    using VisionLut = std::array<float, kSurfaceTagLutSize>;

    // Heat class per SurfaceTag: the engine categories, kVisionHeatAmbient for any other tag.
    VisionLut makeHeatLut();

    // Bee shift as a ColorRemap: (r, g, b) -> (g, b, aux), blended by strength.
    ColorRemap beeShiftRemap(float aux, float strength);

    // `heatPassReady`: the heat pass will draw this frame. Without it heat asks for nothing, so an
    // optional feature that cannot draw never turns a core pass off.
    VisionDecision decideVision(const VisionModes& modes, bool heatPassReady);

    // Writes `decision` into `setup`. Off leaves `setup` unchanged. Only sets disabled bits, lowers
    // emissiveGain, and writes remaps, writeSurfaceTag and the LUT; `heatLut` must outlive the frame.
    void applyVisionModes(const VisionDecision& decision, const VisionLut& heatLut, FrameSetup& setup);

    // True when a vision picture replaces the lit frame, so the host drops the underwater column and
    // holds exposure. Driven by what the feature decided, not by the raw infrared strength: a heat
    // request the feature declined (no pipeline, no camera) leaves the default frame, and the host
    // must not treat it as a picture. `echo` is the host's own pulse effect.
    bool visionPictureActive(const VisionDecision& decision, float echo);

    // The heat pass's graph uses in the game slot: the G-buffer, depth and SurfaceTag as pixel SRVs,
    // HDR as a render target (read-write, so a fade blends over it). The G-buffer pass must have
    // written SurfaceTag this frame. `exec(PassContext&, const VisionHeatUses&)`. False (no pass)
    // when a handle is missing.
    struct VisionHeatUses
    {
        Rg::TextureHandle albedo, attrib, depth, surfaceTag, hdr;
    };
    inline constexpr const char* kVisionHeatPassName = "VisionModes.Heat";

    template <class Exec> bool declareVisionHeatPass(FeatureBuilder& b, Exec&& exec)
    {
        const VisionHeatUses in{ b.handle(RenderResource::Albedo), b.handle(RenderResource::Attrib), b.handle(RenderResource::Depth), b.handle(RenderResource::SurfaceTag),
                                 b.handle(RenderResource::Hdr) };
        if (!in.albedo.valid() || !in.attrib.valid() || !in.depth.valid() || !in.surfaceTag.valid() || !in.hdr.valid())
            return false;
        Rg::TextureHandle out;
        b.addPass<VisionHeatUses>(
            kVisionHeatPassName,
            [&](Rg::PassBuilder& pb, VisionHeatUses& d)
            {
                d.albedo     = pb.read(in.albedo, Rg::Use::PixelSrv);
                d.attrib     = pb.read(in.attrib, Rg::Use::PixelSrv);
                d.depth      = pb.read(in.depth, Rg::Use::PixelSrv);
                d.surfaceTag = pb.read(in.surfaceTag, Rg::Use::PixelSrv);
                d.hdr        = pb.readWrite(in.hdr, Rg::Use::RenderTarget);
                out          = d.hdr;
            },
            std::forward<Exec>(exec));
        b.set(RenderResource::Hdr, out);
        return true;
    }

} // namespace Dark
