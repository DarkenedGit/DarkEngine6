#pragma once
#include "Render/Renderer.h"

namespace Dark
{

// Passed down to every render system each frame
struct RenderContext 
{
    Renderer* renderer = nullptr;
    float     dt       = 0.0f;
    uint32_t  frameIdx = 0; // monotonically increasing
};

} // namespace DE
