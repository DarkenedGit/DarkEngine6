#include "Audio/SoundClip.h"
#include "Core/Log.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"

#include <cmath>
#include <cstdint>

namespace Dark
{

    bool SoundClip::loadWav(const std::filesystem::path& path)
    {
        m_wav = PcmWav{};
        if (!loadPcmWav(path, m_wav))
            return false;
        DE_LOG_INFO(LogCategory::Audio, 
            "SoundClip: '{}'  {} Hz  {} ch  {} frames",
            path.string(),
            m_wav.sampleRate,
            m_wav.channels,
            m_wav.frameCount());
        return true;
    }

    bool SoundClip::createFromPcm(PcmWav pcm)
    {
        if (!pcm.valid())
            return false;
        m_wav = std::move(pcm);
        return true;
    }

    bool SoundClip::createTone(float freqHz, float durationSec, float amplitude, uint32_t sampleRate)
    {
        m_wav = PcmWav{};
        if (freqHz <= 0.0f || durationSec <= 0.0f || sampleRate < 8000)
            return false;
        if (amplitude < 0.0f)
            amplitude = 0.0f;
        if (amplitude > 1.0f)
            amplitude = 1.0f;

        const uint32_t frames = static_cast<uint32_t>(durationSec * static_cast<float>(sampleRate) + 0.5f);
        if (frames < 8)
            return false;

        m_wav.channels   = 1;
        m_wav.sampleRate = sampleRate;
        m_wav.samples.resize(frames);
        const float fade = Math::Min(0.01f, durationSec * 0.2f);
        const float twoPiF = 2.0f * Math::Pi * freqHz;
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
            float env = 1.0f;
            if (t < fade)
                env = t / fade;
            const float tail = durationSec - t;
            if (tail < fade)
                env = tail / fade;
            const float s = std::sinf(twoPiF * t) * amplitude * env;
            m_wav.samples[i] = static_cast<int16_t>(s * 32767.0f);
        }
        return true;
    }

    bool SoundClip::createBlip(float freqHz, float durationSec, float amplitude, uint32_t sampleRate)
    {
        m_wav = PcmWav{};
        if (freqHz <= 0.0f || durationSec <= 0.0f || sampleRate < 8000)
            return false;
        if (amplitude < 0.0f)
            amplitude = 0.0f;
        if (amplitude > 1.0f)
            amplitude = 1.0f;

        const uint32_t frames = static_cast<uint32_t>(durationSec * static_cast<float>(sampleRate) + 0.5f);
        if (frames < 8)
            return false;

        m_wav.channels   = 1;
        m_wav.sampleRate = sampleRate;
        m_wav.samples.resize(frames);
        const float twoPiF = 2.0f * Math::Pi * freqHz;
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float t   = static_cast<float>(i) / static_cast<float>(sampleRate);
            const float env = std::expf(-6.0f * t / durationSec);
            const float s   = std::sinf(twoPiF * t) * amplitude * env;
            m_wav.samples[i] = static_cast<int16_t>(s * 32767.0f);
        }
        return true;
    }

} // namespace Dark
