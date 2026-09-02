#pragma once

#include <cstdint>

namespace Dark
{

    // Live 3D color path. Config requests this; Renderer::scenePath() is authoritative after enable.
    enum class ScenePath : uint8_t
    {
        SwapChainForward = 0, // color = swap chain UNORM (-forward)
        HybridDeferred,       // G-buffer + fullscreen lighting + sky last
    };

} // namespace Dark
