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

        void cycleFill();
    };

    const char* toString(DebugFill fill);

} // namespace Dark
