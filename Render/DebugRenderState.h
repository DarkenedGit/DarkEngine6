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
        DebugFill fill     = DebugFill::Solid;
        bool      lighting = true;
        bool      shadows  = true;
        bool      aces        = false; // PR3 soak: Narkowicz display curve on HDR tonemap
        bool      motionBlur  = true;
        bool      taa         = true;

        void cycleFill();
    };

    const char* toString(DebugFill fill);

} // namespace Dark
