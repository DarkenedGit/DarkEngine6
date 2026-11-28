#include "Game/VisionModes.h"

namespace Dark
{

    namespace
    {
        float clamp01(float v)
        {
            return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f;
        }
    } // namespace

    VisionLut makeHeatLut()
    {
        VisionLut lut{};
        lut.fill(kVisionHeatAmbient);
        lut[static_cast<uint32_t>(SurfaceCategory::StaticMesh)]  = kVisionHeatAmbient;
        lut[static_cast<uint32_t>(SurfaceCategory::SkinnedMesh)] = kVisionHeatBody;
        lut[static_cast<uint32_t>(SurfaceCategory::Terrain)]     = kVisionHeatAmbient;
        lut[static_cast<uint32_t>(SurfaceCategory::Grass)]       = kVisionHeatVegetation;
        lut[static_cast<uint32_t>(SurfaceCategory::Foliage)]     = kVisionHeatVegetation;
        return lut;
    }

    ColorRemap beeShiftRemap(float aux, float strength)
    {
        ColorRemap r;
        r.m[0][0] = 0.0f, r.m[0][1] = 1.0f, r.m[0][2] = 0.0f, r.m[0][3] = 0.0f;
        r.m[1][0] = 0.0f, r.m[1][1] = 0.0f, r.m[1][2] = 1.0f, r.m[1][3] = 0.0f;
        r.m[2][0] = 0.0f, r.m[2][1] = 0.0f, r.m[2][2] = 0.0f, r.m[2][3] = 1.0f;
        r.aux      = clamp01(aux);
        r.strength = clamp01(strength);
        return r;
    }

    VisionDecision decideVision(const VisionModes& modes, bool heatPassReady)
    {
        const float uv = clamp01(modes.ultraviolet);
        const float ir = clamp01(modes.infrared);
        if (uv > 0.0f)
            return { VisionPicture::Ultraviolet, uv };
        if (ir > 0.0f && heatPassReady)
            return { VisionPicture::Heat, ir };
        return {};
    }

    bool visionPictureActive(const VisionDecision& decision, float echo)
    {
        return decision.picture != VisionPicture::Off || echo > 0.0f;
    }

    void applyVisionModes(const VisionDecision& decision, const VisionLut& heatLut, FrameSetup& setup)
    {
        if (decision.picture == VisionPicture::Off || !(decision.strength > 0.0f))
            return;
        const float s = clamp01(decision.strength);
        // RT0.a is emissive; neither picture shows it as light.
        setup.emissiveGain = 0.0f;
        if (decision.picture == VisionPicture::Ultraviolet)
        {
            // The shift happens before lighting, so it is a lighting parameter, not a post pass.
            setup.disabled.bits |= kVisionAnyDisabled;
            setup.albedoRemap = beeShiftRemap(kVisionUvSurface, s);
            setup.fogRemap    = beeShiftRemap(kVisionUvFog, s);
            setup.skyRemap    = beeShiftRemap(kVisionUvSky, s);
            setup.waterRemap  = beeShiftRemap(kVisionUvWater, s);
            return;
        }
        // Heat: the game pass draws it. At full strength nothing lit is left to see.
        setup.disabled.bits |= s >= 1.0f ? kVisionHeatFullDisabled : kVisionAnyDisabled;
        setup.writeSurfaceTag   = true;
        setup.surfaceTagLut     = heatLut.data();
        setup.surfaceTagLutSize = static_cast<uint32_t>(heatLut.size());
    }

} // namespace Dark
