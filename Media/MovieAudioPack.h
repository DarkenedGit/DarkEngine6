#pragma once

#include <cstdint>
#include <vector>

namespace Dark
{

    inline constexpr uint32_t kMovieAudioPacketFrames = 960;
    inline constexpr float    kMovieSurroundGain      = 0.707f;

    struct MovieAudioPackInput
    {
        const void* samples    = nullptr;
        uint32_t    frameCount = 0;
        uint32_t    channels   = 0;
        bool        float32    = false;
    };

    // Int16 stereo packets. Remainder is interleaved stereo carried across calls, shorter than one packet.
    // 5.1 is FL, FR, FC, LFE, BL, BR. LFE is dropped. Mix accumulates in float before the int16 clamp.
    bool packMovieAudio(const MovieAudioPackInput& in, std::vector<int16_t>& remainder, std::vector<std::vector<int16_t>>& packets, uint32_t packetFrames = kMovieAudioPacketFrames);

} // namespace Dark
