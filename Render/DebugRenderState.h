#pragma once

#include <cstdint>

namespace Dark
{

    enum class DebugFill : uint8_t
    {
        Solid = 0,
        Wireframe,
        Points,
    };

    struct DebugRenderState
    {
        DebugFill fill        = DebugFill::Solid;
        bool      lighting    = true;
        bool      localLights = true;
        bool      bloom       = true;
        bool      shadows     = true;
        bool      aces        = false; // Narkowicz display curve on HDR tonemap
        bool      motionBlur  = true;
        bool      taa         = true;
        bool      legacyUnormAlbedo = false; // pack albedo as raw UNORM (skip sRGB decode)

        void cycleFill();
    };

    const char* toString(DebugFill fill);

} // namespace Dark
