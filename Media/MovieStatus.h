#pragma once

#include <cstdint>

namespace Dark
{

    enum class MovieStatus : uint8_t
    {
        Ok = 0,
        FileMissing,
        UnsupportedContainer,
        UnsupportedCodec,
        ResolutionUnsupported,
        DecodeError,
        Busy,
        DeviceLost,
    };

    enum class MoviePlayState : uint8_t
    {
        Closed = 0,
        Opening,
        Ready,
        Playing,
        Paused,
        Seeking,
        Ended,
        Failed,
    };

} // namespace Dark
