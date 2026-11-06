#pragma once

#include <cstddef>
#include <cstdint>

namespace Dark
{

    // Tight top-down BGRA. Negative stride is bottom-up: the buffer base is the bottom row.
    // Alpha is forced to 255. Refuses sizes above the 1920x1080 software cap and spans that do not fit.
    bool copyMovieFrameBgra(const uint8_t* src, size_t srcBytes, int32_t strideBytes, uint32_t width, uint32_t height, uint8_t* dst, size_t dstBytes);

} // namespace Dark
