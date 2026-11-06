#include "Media/MovieFrameCopy.h"

#include "Media/MoviePath.h"

#include <cstring>

namespace Dark
{

    bool copyMovieFrameBgra(const uint8_t* src, size_t srcBytes, int32_t strideBytes, uint32_t width, uint32_t height, uint8_t* dst, size_t dstBytes)
    {
        if (!movieSoftwareFallbackAllowed(width, height))
            return false;
        if (!src || !dst)
            return false;
        if (strideBytes == 0 || strideBytes == INT32_MIN)
            return false;

        const uint32_t absStride = static_cast<uint32_t>(strideBytes < 0 ? -strideBytes : strideBytes);
        const uint32_t tightRow  = width * 4u;
        if (absStride < tightRow)
            return false;

        const uint64_t srcSpan = static_cast<uint64_t>(absStride) * static_cast<uint64_t>(height);
        const uint64_t dstSpan = static_cast<uint64_t>(tightRow) * static_cast<uint64_t>(height);
        if (srcSpan > srcBytes || dstSpan > dstBytes)
            return false;

        const bool bottomUp = strideBytes < 0;
        for (uint32_t y = 0; y < height; ++y)
        {
            const uint32_t srcRow = bottomUp ? (height - 1u - y) : y;
            const uint8_t* s      = src + static_cast<size_t>(srcRow) * absStride;
            uint8_t*       d      = dst + static_cast<size_t>(y) * tightRow;
            std::memcpy(d, s, tightRow);
            for (uint32_t x = 0; x < width; ++x)
                d[x * 4u + 3u] = 255;
        }
        return true;
    }

} // namespace Dark
