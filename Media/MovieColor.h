#pragma once

#include <cstdint>

namespace Dark::MovieColor
{

    enum class Range : uint8_t
    {
        Limited,
        Full,
    };

    struct Bgra
    {
        uint8_t b = 0;
        uint8_t g = 0;
        uint8_t r = 0;
        uint8_t a = 255;
    };

    // Y, Cb, Cr are 0..255. Coded height above 576 selects BT.709, otherwise BT.601.
    Bgra sample(float Y, float Cb, float Cr, uint32_t codedHeight, Range range);

} // namespace Dark::MovieColor
