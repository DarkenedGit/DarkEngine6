#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Dark
{

    struct PcmWav
    {
        std::vector<int16_t> samples; // interleaved
        uint16_t             channels   = 0;
        uint32_t             sampleRate = 0;

        bool valid() const { return !samples.empty() && channels >= 1 && sampleRate > 0; }
        uint32_t frameCount() const
        {
            return channels ? static_cast<uint32_t>(samples.size() / channels) : 0;
        }
    };

    bool loadPcmWav(const std::filesystem::path& path, PcmWav& out);
    bool writePcmWav(const std::filesystem::path& path, const PcmWav& wav);

} // namespace Dark
