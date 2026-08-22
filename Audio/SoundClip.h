#pragma once

#include "Audio/WavFile.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Dark
{

    class SoundClip
    {
    public:
        bool loadWav(const std::filesystem::path& path);
        bool createFromPcm(PcmWav pcm);
        bool createTone(float freqHz, float durationSec, float amplitude = 0.35f, uint32_t sampleRate = 44100);
        bool createBlip(float freqHz, float durationSec, float amplitude = 0.45f, uint32_t sampleRate = 44100);

        bool valid() const { return m_wav.valid(); }

        const int16_t* samples() const { return m_wav.samples.data(); }
        uint32_t       sampleCount() const { return static_cast<uint32_t>(m_wav.samples.size()); }
        uint32_t       frameCount() const { return m_wav.frameCount(); }
        uint16_t       channels() const { return m_wav.channels; }
        uint32_t       sampleRate() const { return m_wav.sampleRate; }
        uint32_t       byteSize() const { return sampleCount() * sizeof(int16_t); }

        const PcmWav& pcm() const { return m_wav; }
        const std::string& key() const { return m_key; }
        void setKey(std::string key) { m_key = std::move(key); }

    private:
        PcmWav      m_wav;
        std::string m_key;
    };

} // namespace Dark
