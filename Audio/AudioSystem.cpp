#include "Audio/AudioSystem.h"
#include "Assets/AssetManager.h"
#include "Assets/TextureCache.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <xaudio2.h>
#include <x3daudio.h>
#include <objbase.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace Dark
{

    using namespace Math;

    namespace
    {

        float clampf(float v, float lo, float hi)
        {
            return Max(lo, Min(hi, v));
        }

        void orthonormalize(Vector3f& forward, Vector3f& up)
        {
            if (forward.MagnitudeSqrd() < 1.0e-8f)
                forward = Vector3f(0.0f, 0.0f, 1.0f);
            else
                forward.Normalize();

            Vector3f right = up.Cross(forward);
            if (right.MagnitudeSqrd() < 1.0e-8f)
            {
                up = std::fabs(forward.y) > 0.9f ? Vector3f(0.0f, 0.0f, 1.0f) : Vector3f(0.0f, 1.0f, 0.0f);
                right = up.Cross(forward);
            }
            right.Normalize();
            up = forward.Cross(right);
            up.Normalize();
        }

        WAVEFORMATEX makePcmFormat(const SoundClip& clip)
        {
            WAVEFORMATEX w{};
            w.wFormatTag      = WAVE_FORMAT_PCM;
            w.nChannels       = clip.channels();
            w.nSamplesPerSec  = clip.sampleRate();
            w.wBitsPerSample  = 16;
            w.nBlockAlign     = static_cast<WORD>(w.nChannels * (w.wBitsPerSample / 8));
            w.nAvgBytesPerSec = w.nSamplesPerSec * w.nBlockAlign;
            return w;
        }

        bool sameFormat(const WAVEFORMATEX& a, const WAVEFORMATEX& b)
        {
            return a.nChannels == b.nChannels && a.nSamplesPerSec == b.nSamplesPerSec
                && a.wBitsPerSample == b.wBitsPerSample && a.wFormatTag == b.wFormatTag;
        }

    } // namespace

    struct AudioSystem::VoiceSlot
    {
        IXAudio2SourceVoice*       voice = nullptr;
        WAVEFORMATEX               format{};
        std::shared_ptr<SoundClip> clip;
        PlayDesc                   desc{};
        uint32_t                   generation = 1;
        bool                       inUse      = false;
        bool                       music      = false;
    };

    struct AudioSystem::Device
    {
        IXAudio2*               xaudio      = nullptr;
        IXAudio2MasteringVoice* master      = nullptr;
        X3DAUDIO_HANDLE         x3d{};
        UINT32                  dstChannels = 2;
        DWORD                   channelMask = SPEAKER_STEREO;
        bool                    x3dReady    = false;

        ~Device()
        {
            if (master)
            {
                master->DestroyVoice();
                master = nullptr;
            }
            if (xaudio)
            {
                xaudio->Release();
                xaudio = nullptr;
            }
        }
    };

    AudioSystem::AudioSystem() = default;

    bool AudioSystem::create()
    {
        shutdown();
        return createDevice();
    }

    bool AudioSystem::createDevice()
    {
        m_device = std::make_unique<Device>();

        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (hr == S_OK || hr == S_FALSE)
            m_comInited = true;
        else if (hr == RPC_E_CHANGED_MODE)
            m_comInited = false; // already inited with a different model; still usable
        else if (FAILED(hr))
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: CoInitializeEx failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
            m_device.reset();
            return false;
        }

        UINT32 flags = 0;
#if defined(_DEBUG)
        flags |= XAUDIO2_DEBUG_ENGINE;
#endif
        hr = XAudio2Create(&m_device->xaudio, flags, XAUDIO2_DEFAULT_PROCESSOR);
        if (FAILED(hr))
        {
            // Debug engine is optional; retry without it.
            hr = XAudio2Create(&m_device->xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR);
        }
        if (FAILED(hr) || !m_device->xaudio)
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: XAudio2Create failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
            m_device.reset();
            return false;
        }

#if defined(_DEBUG)
        XAUDIO2_DEBUG_CONFIGURATION dbg{};
        dbg.TraceMask = XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS;
        m_device->xaudio->SetDebugConfiguration(&dbg, nullptr);
#endif

        hr = m_device->xaudio->CreateMasteringVoice(&m_device->master);
        if (FAILED(hr) || !m_device->master)
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: CreateMasteringVoice failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
            m_device.reset();
            return false;
        }

        XAUDIO2_VOICE_DETAILS details{};
        m_device->master->GetVoiceDetails(&details);
        m_device->dstChannels = details.InputChannels ? details.InputChannels : 2;

        DWORD mask = 0;
        if (SUCCEEDED(m_device->master->GetChannelMask(&mask)) && mask != 0)
            m_device->channelMask = mask;

        X3DAudioInitialize(m_device->channelMask, X3DAUDIO_SPEED_OF_SOUND, m_device->x3d);
        m_device->x3dReady = true;

        m_voices.clear();
        m_voices.resize(kMaxVoices);
        for (uint32_t i = 0; i < kMaxVoices; ++i)
            m_voices[i] = std::make_unique<VoiceSlot>();

        m_valid = true;
        DE_LOG_INFO(LogCategory::Audio, "Audio: XAudio2 ready ({} output channels)", m_device->dstChannels);
        return true;
    }

    AudioSystem::~AudioSystem()
    {
        shutdown();
    }

    void AudioSystem::shutdown()
    {
        stopAll();
        if (m_device)
        {
            for (auto& slot : m_voices)
            {
                if (!slot)
                    continue;
                if (slot->voice)
                {
                    slot->voice->DestroyVoice();
                    slot->voice = nullptr;
                }
                slot->clip.reset();
            }
            m_voices.clear();
            m_device.reset();
        }
        m_clips.clear();
        m_musicId = 0;
        m_valid   = false;
        m_comInited = false;
    }

    std::shared_ptr<SoundClip> AudioSystem::loadWav(const std::filesystem::path& path)
    {
        const std::string key = std::string("f:") + TextureCache::normalizePath(path);
        const auto it = m_clips.find(key);
        if (it != m_clips.end())
            return it->second;

        auto clip = std::make_shared<SoundClip>();
        if (!clip->loadWav(path))
            return {};
        clip->setKey(key);
        m_clips[key] = clip;
        return clip;
    }

    std::shared_ptr<SoundClip> AudioSystem::loadWav(AssetManager& assets, const char* virtualPath)
    {
        if (!virtualPath || virtualPath[0] == '\0')
            return {};
        const std::filesystem::path resolved = assets.resolve(virtualPath);
        if (resolved.empty())
        {
            DE_LOG_WARN(LogCategory::Audio, "Audio: could not resolve '{}'", virtualPath);
            return {};
        }
        return loadWav(resolved);
    }

    std::shared_ptr<SoundClip> AudioSystem::createTone(float freqHz, float durationSec, float amplitude)
    {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "tone:%.3f,%.3f,%.3f", freqHz, durationSec, amplitude);
        const std::string key(buf);
        const auto it = m_clips.find(key);
        if (it != m_clips.end())
            return it->second;

        auto clip = std::make_shared<SoundClip>();
        if (!clip->createTone(freqHz, durationSec, amplitude))
            return {};
        clip->setKey(key);
        m_clips[key] = clip;
        return clip;
    }

    std::shared_ptr<SoundClip> AudioSystem::createBlip(float freqHz, float durationSec, float amplitude)
    {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "blip:%.3f,%.3f,%.3f", freqHz, durationSec, amplitude);
        const std::string key(buf);
        const auto it = m_clips.find(key);
        if (it != m_clips.end())
            return it->second;

        auto clip = std::make_shared<SoundClip>();
        if (!clip->createBlip(freqHz, durationSec, amplitude))
            return {};
        clip->setKey(key);
        m_clips[key] = clip;
        return clip;
    }

    std::shared_ptr<SoundClip> AudioSystem::loadOrBlip(
        AssetManager& assets,
        const char* virtualPath,
        float fallbackFreqHz,
        float fallbackDurationSec,
        float fallbackAmp)
    {
        if (auto clip = loadWav(assets, virtualPath))
            return clip;
        DE_LOG_WARN(LogCategory::Audio, "Audio: using tone fallback for '{}'", virtualPath ? virtualPath : "");
        return createBlip(fallbackFreqHz, fallbackDurationSec, fallbackAmp);
    }

    bool AudioSystem::ensureVoice(VoiceSlot& slot, const SoundClip& clip)
    {
        if (!m_device || !m_device->xaudio)
            return false;
        const WAVEFORMATEX fmt = makePcmFormat(clip);
        if (slot.voice && sameFormat(slot.format, fmt))
            return true;
        if (slot.voice)
        {
            slot.voice->DestroyVoice();
            slot.voice = nullptr;
        }
        const HRESULT hr = m_device->xaudio->CreateSourceVoice(&slot.voice, &fmt);
        if (FAILED(hr) || !slot.voice)
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: CreateSourceVoice failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
            return false;
        }
        slot.format = fmt;
        return true;
    }

    bool AudioSystem::voiceIdle(const VoiceSlot& slot) const
    {
        if (!slot.inUse || !slot.voice)
            return true;
        if (slot.desc.loop)
            return false;
        XAUDIO2_VOICE_STATE st{};
        slot.voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        return st.BuffersQueued == 0;
    }

    int AudioSystem::allocSlot(bool allowSteal)
    {
        for (int i = 0; i < static_cast<int>(m_voices.size()); ++i)
        {
            VoiceSlot& slot = *m_voices[static_cast<size_t>(i)];
            if (!slot.inUse || voiceIdle(slot))
            {
                if (slot.inUse && slot.voice)
                    slot.voice->Stop(0);
                slot.inUse = false;
                slot.music = false;
                return i;
            }
        }
        if (!allowSteal)
            return -1;
        for (int i = static_cast<int>(m_voices.size()) - 1; i >= 0; --i)
        {
            VoiceSlot& slot = *m_voices[static_cast<size_t>(i)];
            if (slot.music || slot.desc.loop)
                continue;
            if (slot.voice)
                slot.voice->Stop(0);
            slot.inUse = false;
            return i;
        }
        return -1;
    }

    VoiceId AudioSystem::makeId(int index) const
    {
        if (index < 0 || index >= static_cast<int>(m_voices.size()))
            return 0;
        const uint32_t gen = m_voices[static_cast<size_t>(index)]->generation;
        return (gen << 8) | (static_cast<uint32_t>(index) + 1u);
    }

    int AudioSystem::decodeIndex(VoiceId id) const
    {
        if (id == 0)
            return -1;
        const int index = static_cast<int>((id & 0xffu) - 1u);
        if (index < 0 || index >= static_cast<int>(m_voices.size()))
            return -1;
        return index;
    }

    bool AudioSystem::matches(VoiceId id, int index) const
    {
        if (index < 0)
            return false;
        const uint32_t gen = id >> 8;
        return m_voices[static_cast<size_t>(index)]->generation == gen;
    }

    void AudioSystem::apply3D(VoiceSlot& slot) const
    {
        if (!m_device || !m_device->x3dReady || !slot.voice || !slot.desc.spatial)
            return;

        Vector3f fwd = m_listener.forward;
        Vector3f up  = m_listener.up;
        orthonormalize(fwd, up);

        X3DAUDIO_LISTENER listener{};
        listener.OrientFront = { fwd.x, fwd.y, fwd.z };
        listener.OrientTop   = { up.x, up.y, up.z };
        listener.Position    = { m_listener.position.x, m_listener.position.y, m_listener.position.z };
        listener.Velocity    = { m_listener.velocity.x, m_listener.velocity.y, m_listener.velocity.z };

        X3DAUDIO_EMITTER emitter{};
        emitter.ChannelCount       = 1;
        emitter.CurveDistanceScaler = 1.0f;
        emitter.DopplerScaler      = 1.0f;
        emitter.InnerRadius        = slot.desc.minDistance;
        emitter.InnerRadiusAngle   = X3DAUDIO_PI / 4.0f;
        emitter.Position = { slot.desc.position.x, slot.desc.position.y, slot.desc.position.z };

        X3DAUDIO_DISTANCE_CURVE_POINT pts[2]{};
        pts[0].Distance = 0.0f;
        pts[0].DSPSetting = 1.0f;
        pts[1].Distance = 1.0f;
        pts[1].DSPSetting = 0.0f;
        X3DAUDIO_DISTANCE_CURVE curve{};
        curve.PointCount = 2;
        curve.pPoints    = pts;
        emitter.pVolumeCurve = &curve;
        emitter.CurveDistanceScaler = Max(slot.desc.maxDistance, 1.0f);

        float matrix[8]{};
        X3DAUDIO_DSP_SETTINGS dsp{};
        dsp.SrcChannelCount      = 1;
        dsp.DstChannelCount      = m_device->dstChannels;
        dsp.pMatrixCoefficients  = matrix;

        X3DAudioCalculate(
            m_device->x3d,
            &listener,
            &emitter,
            X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER,
            &dsp);

        slot.voice->SetOutputMatrix(nullptr, 1, m_device->dstChannels, matrix);
        slot.voice->SetFrequencyRatio(clampf(slot.desc.pitch * dsp.DopplerFactor, 0.5f, 2.0f));
    }

    VoiceId AudioSystem::play(const std::shared_ptr<SoundClip>& clip, const PlayDesc& desc)
    {
        if (!m_valid || !clip || !clip->valid() || !m_device)
            return 0;

        const int index = allocSlot(true);
        if (index < 0)
            return 0;

        VoiceSlot& slot = *m_voices[static_cast<size_t>(index)];
        if (!ensureVoice(slot, *clip))
            return 0;

        slot.voice->Stop(0);
        slot.voice->FlushSourceBuffers();
        slot.clip = clip;
        slot.desc = desc;
        slot.desc.volume = clampf(desc.volume, 0.0f, 1.0f);
        slot.desc.pitch  = clampf(desc.pitch, 0.5f, 2.0f);
        slot.inUse       = true;
        slot.music       = false;
        ++slot.generation;
        if (slot.generation == 0)
            slot.generation = 1;

        XAUDIO2_BUFFER buf{};
        buf.AudioBytes = clip->byteSize();
        buf.pAudioData = reinterpret_cast<const BYTE*>(clip->samples());
        buf.Flags      = XAUDIO2_END_OF_STREAM;
        buf.LoopCount  = desc.loop ? XAUDIO2_LOOP_INFINITE : 0;

        const HRESULT hr = slot.voice->SubmitSourceBuffer(&buf);
        if (FAILED(hr))
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: SubmitSourceBuffer failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
            slot.inUse = false;
            slot.clip.reset();
            return 0;
        }

        slot.voice->SetVolume(slot.desc.volume);
        slot.voice->SetFrequencyRatio(slot.desc.pitch);
        if (slot.desc.spatial)
            apply3D(slot);
        else if (m_device->dstChannels >= 2)
        {
            // Center a mono clip on stereo/surround.
            if (clip->channels() == 1)
            {
                float matrix[8]{};
                matrix[0] = 1.0f;
                matrix[1] = 1.0f;
                slot.voice->SetOutputMatrix(nullptr, 1, m_device->dstChannels, matrix);
            }
        }

        slot.voice->Start(0);
        return makeId(index);
    }

    VoiceId AudioSystem::play2D(const std::shared_ptr<SoundClip>& clip, float volume, bool loop)
    {
        PlayDesc d{};
        d.volume = volume;
        d.loop   = loop;
        return play(clip, d);
    }

    VoiceId AudioSystem::play3D(const std::shared_ptr<SoundClip>& clip, const Vector3f& position, float volume)
    {
        PlayDesc d{};
        d.volume   = volume;
        d.spatial  = true;
        d.position = position;
        return play(clip, d);
    }

    void AudioSystem::setMusic(const std::shared_ptr<SoundClip>& clip, float volume)
    {
        stopMusic();
        if (!clip)
            return;
        PlayDesc d{};
        d.volume = volume;
        d.loop   = true;
        m_musicId = play(clip, d);
        const int index = decodeIndex(m_musicId);
        if (index >= 0)
            m_voices[static_cast<size_t>(index)]->music = true;
    }

    void AudioSystem::stopMusic()
    {
        stop(m_musicId);
        m_musicId = 0;
    }

    void AudioSystem::stop(VoiceId id)
    {
        const int index = decodeIndex(id);
        if (!matches(id, index))
            return;
        VoiceSlot& slot = *m_voices[static_cast<size_t>(index)];
        if (slot.voice)
            slot.voice->Stop(0);
        slot.inUse = false;
        slot.music = false;
        slot.clip.reset();
        ++slot.generation;
        if (slot.generation == 0)
            slot.generation = 1;
    }

    void AudioSystem::stopAll()
    {
        for (int i = 0; i < static_cast<int>(m_voices.size()); ++i)
        {
            if (!m_voices[static_cast<size_t>(i)])
                continue;
            VoiceSlot& slot = *m_voices[static_cast<size_t>(i)];
            if (slot.voice && slot.inUse)
                slot.voice->Stop(0);
            slot.inUse = false;
            slot.music = false;
            slot.clip.reset();
            ++slot.generation;
            if (slot.generation == 0)
                slot.generation = 1;
        }
        m_musicId = 0;
    }

    bool AudioSystem::isPlaying(VoiceId id) const
    {
        const int index = decodeIndex(id);
        if (!matches(id, index))
            return false;
        return !voiceIdle(*m_voices[static_cast<size_t>(index)]);
    }

    void AudioSystem::setMasterVolume(float volume)
    {
        m_masterVolume = clampf(volume, 0.0f, 1.0f);
        if (m_device && m_device->master)
            m_device->master->SetVolume(m_masterVolume);
    }

    void AudioSystem::tick()
    {
        if (!m_valid)
            return;
        for (auto& ptr : m_voices)
        {
            if (!ptr)
                continue;
            VoiceSlot& slot = *ptr;
            if (!slot.inUse)
                continue;
            if (voiceIdle(slot))
            {
                slot.inUse = false;
                slot.music = false;
                slot.clip.reset();
                ++slot.generation;
                if (slot.generation == 0)
                    slot.generation = 1;
                continue;
            }
            if (slot.desc.spatial)
                apply3D(slot);
        }
    }

} // namespace Dark
