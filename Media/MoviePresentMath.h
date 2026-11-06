#pragma once

#include <cstdint>

namespace Dark::MoviePresentMath
{

    struct MovieLetterbox
    {
        float x0 = 0.f;
        float y0 = 0.f;
        float x1 = 0.f;
        float y1 = 0.f;
    };

    // Window UV, origin top-left. Fits the coded frame inside the window, preserves aspect, centers it.
    bool letterbox(uint32_t windowWidth, uint32_t windowHeight, uint32_t videoWidth, uint32_t videoHeight, MovieLetterbox& out);

} // namespace Dark::MoviePresentMath
