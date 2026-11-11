#include "Audio/AudioSystem.h"
#include "Audio/StreamQueue.h"
#include "Assets/AssetManager.h"
#include "Character/SkillSense.h"
#include "Assets/TextureCache.h"
#include "Core/Log.h"
#include "Core/MainThread.h"
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
#include <new>

namespace Dark::Audio
{
    using namespace Math;

    struct ClipKeep;

    // Friend of AudioSystem. The XAudio2 callback thread only queues the pointer.
    void noteSourceBufferEnd(void* user, void* context)
    {
        if (!user || !context)
            return;
        static_cast<AudioSystem*>(user)->noteBufferEnd(context);
    }

    struct SourceBufferCallback : IXAudio2VoiceCallback
    {
        void (*fn)(void* user, void* context) = nullptr;
        void* user                            = nullptr;

        void OnVoiceProcessingPassStart(UINT32) override {}
        void OnVoiceProcessingPassEnd() override {}
        void OnStreamEnd() override {}
        void OnBufferStart(void*) override {}
        void OnBufferEnd(void* context) override
        {
            if (fn && context)
                fn(user, context);
        }
        void OnLoopEnd(void*) override {}
        void OnVoiceError(void*, HRESULT) override {}
    };

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

    struct AudioSystem::VoiceSlot
    {
        IXAudio2SourceVoice*       voice = nullptr;
        WAVEFORMATEX               format{};
        std::shared_ptr<SoundClip> clip;
        PlayDesc                   desc{};
        uint32_t                   generation = 1;
        bool                       inUse      = false;
        bool                       music      = false;
        float                      occlusion  = 0.0f;
        float                      filterFreq = 1.0f;
        bool                       occlusionFresh = false;
        ClipKeep*                  keep = nullptr;
    };

    struct ClipKeep
    {
        std::shared_ptr<SoundClip> clip;
        int                        slotIndex = -1;
    };

    struct AudioSystem::Device
    {
        // Outside m_voices so allocSlot and the 24-voice loops never see it.
        struct StreamVoice
        {
            static constexpr uint32_t kPackets   = 8;
            static constexpr uint32_t kMaxFrames = 960; // 20 ms at 48 kHz. Budget is 8 * 960 * stereo int16.
            static_assert(kPackets * kMaxFrames * 2 * sizeof(int16_t) == 30720u);

            IXAudio2SourceVoice* voice      = nullptr;
            uint32_t             id         = 0;
            uint32_t             nextId     = 1;
            uint32_t             writeIndex = 0;
            float                volume     = 1.0f;
            bool                 paused     = false;
            int16_t              packets[kPackets][kMaxFrames * 2]{};
        };

        IXAudio2*               xaudio      = nullptr;
        IXAudio2MasteringVoice* master      = nullptr;
        X3DAUDIO_HANDLE         x3d{};
        UINT32                  dstChannels = 2;
        DWORD                   channelMask = SPEAKER_STEREO;
        bool                    x3dReady    = false;
        StreamVoice             stream{};
        // Submitted clip buffers. OnBufferEnd only pushes to finished.
        std::mutex              keepMutex;
        std::vector<ClipKeep*>  keeps;
        std::vector<ClipKeep*>  finished;
        SourceBufferCallback    bufferCallback;

        ~Device()
        {
            if (stream.voice)
            {
                stream.voice->DestroyVoice();
                stream.voice = nullptr;
            }
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
        m_device->bufferCallback.fn   = &noteSourceBufferEnd;
        m_device->bufferCallback.user = this;

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

    void AudioSystem::noteBufferEnd(void* context)
    {
        if (!context || !m_device)
            return;
        std::lock_guard<std::mutex> lock(m_device->keepMutex);
        ClipKeep* keep = static_cast<ClipKeep*>(context);
        bool live = false;
        for (const ClipKeep* queued : m_device->keeps)
            live = live || queued == keep;
        if (!live)
            return;
        for (const ClipKeep* queued : m_device->finished)
        {
            if (queued == keep)
                return;
        }
        m_device->finished.push_back(keep);
    }

    void AudioSystem::clearSlotKeep(void* keepPtr)
    {
        const ClipKeep* keep = static_cast<const ClipKeep*>(keepPtr);
        if (!keep || keep->slotIndex < 0 || keep->slotIndex >= static_cast<int>(m_voices.size()))
            return;
        VoiceSlot* slot = m_voices[static_cast<size_t>(keep->slotIndex)].get();
        if (slot && slot->keep == keep)
            slot->keep = nullptr;
    }

    void* AudioSystem::claimKeep(void* keepPtr)
    {
        ClipKeep* keep = static_cast<ClipKeep*>(keepPtr);
        if (!keep || !m_device)
            return nullptr;
        std::lock_guard<std::mutex> lock(m_device->keepMutex);
        bool found = false;
        for (size_t i = 0; i < m_device->keeps.size(); ++i)
        {
            if (m_device->keeps[i] != keep)
                continue;
            m_device->keeps[i] = m_device->keeps.back();
            m_device->keeps.pop_back();
            found = true;
            break;
        }
        if (!found)
            return nullptr;
        for (size_t i = 0; i < m_device->finished.size(); ++i)
        {
            if (m_device->finished[i] != keep)
                continue;
            m_device->finished[i] = m_device->finished.back();
            m_device->finished.pop_back();
            break;
        }
        return keep;
    }

    void AudioSystem::drainFinishedClips()
    {
        if (!m_device)
            return;
        std::vector<ClipKeep*> done;
        {
            std::lock_guard<std::mutex> lock(m_device->keepMutex);
            done.swap(m_device->finished);
        }
        for (ClipKeep* keep : done)
        {
            ClipKeep* claimed = static_cast<ClipKeep*>(claimKeep(keep));
            if (!claimed)
                continue;
            clearSlotKeep(claimed);
            delete claimed;
        }
    }

    void AudioSystem::reclaimIdleKeeps()
    {
        if (!m_device)
            return;
        for (auto& ptr : m_voices)
        {
            if (!ptr || !ptr->keep || !ptr->voice)
                continue;
            XAUDIO2_VOICE_STATE state{};
            ptr->voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (state.BuffersQueued != 0)
                continue;
            ClipKeep* claimed = static_cast<ClipKeep*>(claimKeep(ptr->keep));
            ptr->keep = nullptr;
            if (!claimed)
                continue;
            delete claimed;
        }
    }

    void AudioSystem::releaseKeepsFor(VoiceSlot* slot)
    {
        if (!m_device || !slot)
            return;
        int slotIndex = -1;
        for (int i = 0; i < static_cast<int>(m_voices.size()); ++i)
        {
            if (m_voices[static_cast<size_t>(i)].get() == slot)
            {
                slotIndex = i;
                break;
            }
        }
        std::vector<ClipKeep*> drop;
        {
            std::lock_guard<std::mutex> lock(m_device->keepMutex);
            for (size_t i = 0; i < m_device->keeps.size();)
            {
                ClipKeep* keep = m_device->keeps[i];
                if (!keep || keep->slotIndex != slotIndex)
                {
                    ++i;
                    continue;
                }
                drop.push_back(keep);
                m_device->keeps[i] = m_device->keeps.back();
                m_device->keeps.pop_back();
            }
            for (size_t i = 0; i < m_device->finished.size();)
            {
                bool doomed = false;
                for (const ClipKeep* keep : drop)
                    doomed = doomed || m_device->finished[i] == keep;
                if (!doomed)
                {
                    ++i;
                    continue;
                }
                m_device->finished[i] = m_device->finished.back();
                m_device->finished.pop_back();
            }
        }
        for (ClipKeep* keep : drop)
            delete keep;
        slot->keep = nullptr;
    }

    void AudioSystem::abandonUnsubmitted(void* keepPtr)
    {
        ClipKeep* claimed = static_cast<ClipKeep*>(claimKeep(keepPtr));
        if (!claimed)
            return;
        clearSlotKeep(claimed);
        delete claimed;
    }

    void AudioSystem::destroyAllKeeps()
    {
        if (!m_device)
            return;
        std::vector<ClipKeep*> all;
        {
            std::lock_guard<std::mutex> lock(m_device->keepMutex);
            all.swap(m_device->keeps);
            for (ClipKeep* keep : m_device->finished)
            {
                bool found = false;
                for (const ClipKeep* kept : all)
                    found = found || kept == keep;
                if (!found)
                    all.push_back(keep);
            }
            m_device->finished.clear();
        }
        for (auto& slot : m_voices)
        {
            if (slot)
                slot->keep = nullptr;
        }
        for (ClipKeep* keep : all)
            delete keep;
    }

    void AudioSystem::destroySlotVoice(VoiceSlot& slot)
    {
        if (slot.voice)
        {
            slot.voice->Stop(0);
            slot.voice->FlushSourceBuffers();
            slot.voice->DestroyVoice();
            slot.voice = nullptr;
        }
        // DestroyVoice returns only after callbacks for this voice have finished.
        releaseKeepsFor(&slot);
        slot.clip.reset();
    }

    void AudioSystem::shutdown()
    {
        stopAll();
        // Submit may still be leaving the stream mutex. Destroy the device only after it drops that lock.
        std::lock_guard<std::mutex> lock(m_streamMutex);
        if (m_device)
        {
            for (auto& slot : m_voices)
            {
                if (!slot)
                    continue;
                destroySlotVoice(*slot);
            }
            destroyAllKeeps();
            m_voices.clear();
            m_device.reset();
        }
        m_musicId = 0;
        m_valid   = false;
        m_comInited = false;
        m_occlusionQuery = nullptr;
        m_occlusionUser  = nullptr;
    }

    ::Dark::AssetRef<SoundClip> AudioSystem::loadWav(AssetManager& assets, const char* virtualPath)
    {
        if (!virtualPath || virtualPath[0] == '\0')
            return {};
        ::Dark::AssetRef<SoundClip> clip = assets.loadAudio(virtualPath);
        if (!clip)
            DE_LOG_WARN(LogCategory::Audio, "Audio: could not load '{}'", virtualPath);
        return clip;
    }

    ::Dark::AssetRef<SoundClip> AudioSystem::createTone(AssetManager& assets, float freqHz, float durationSec, float amplitude)
    {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "tone:%.3f,%.3f,%.3f", freqHz, durationSec, amplitude);
        const std::string key(buf);
        if (::Dark::AssetRef<SoundClip> existing = assets.findAudio(key))
            return existing;
        auto clip = std::make_shared<SoundClip>();
        if (!clip->createTone(freqHz, durationSec, amplitude))
            return {};
        clip->setKey(key);
        return assets.internSoundClip(std::move(clip), key);
    }

    ::Dark::AssetRef<SoundClip> AudioSystem::createBlip(AssetManager& assets, float freqHz, float durationSec, float amplitude)
    {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "blip:%.3f,%.3f,%.3f", freqHz, durationSec, amplitude);
        const std::string key(buf);
        if (::Dark::AssetRef<SoundClip> existing = assets.findAudio(key))
            return existing;
        auto clip = std::make_shared<SoundClip>();
        if (!clip->createBlip(freqHz, durationSec, amplitude))
            return {};
        clip->setKey(key);
        return assets.internSoundClip(std::move(clip), key);
    }

    ::Dark::AssetRef<SoundClip> AudioSystem::loadOrBlip(
        AssetManager& assets,
        const char* virtualPath,
        float fallbackFreqHz,
        float fallbackDurationSec,
        float fallbackAmp)
    {
        if (auto clip = loadWav(assets, virtualPath))
            return clip;
        DE_LOG_WARN(LogCategory::Audio, "Audio: using tone fallback for '{}'", virtualPath ? virtualPath : "");
        return createBlip(assets, fallbackFreqHz, fallbackDurationSec, fallbackAmp);
    }

    bool AudioSystem::ensureVoice(VoiceSlot& slot, const SoundClip& clip)
    {
        if (!m_device || !m_device->xaudio)
            return false;
        const WAVEFORMATEX fmt = makePcmFormat(clip);
        if (slot.voice && sameFormat(slot.format, fmt))
            return true;
        destroySlotVoice(slot);
        const HRESULT hr = m_device->xaudio->CreateSourceVoice(
            &slot.voice, &fmt, XAUDIO2_VOICE_USEFILTER, XAUDIO2_DEFAULT_FREQ_RATIO, &m_device->bufferCallback);
        if (FAILED(hr) || !slot.voice)
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: CreateSourceVoice failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
            return false;
        }
        slot.format     = fmt;
        slot.filterFreq = XAUDIO2_MAX_FILTER_FREQUENCY;
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
        emitter.ChannelCount      = 1;
        emitter.DopplerScaler     = 1.0f;
        emitter.InnerRadius       = slot.desc.minDistance;
        emitter.InnerRadiusAngle  = X3DAUDIO_PI / 4.0f;
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
        emitter.CurveDistanceScaler = spatialDistanceScaler(slot.desc.maxDistance, m_listener.distanceScale);

        float matrix[8]{};
        X3DAUDIO_DSP_SETTINGS dsp{};
        dsp.SrcChannelCount      = 1;
        dsp.DstChannelCount      = m_device->dstChannels;
        dsp.pMatrixCoefficients  = matrix;

        X3DAudioCalculate(
            m_device->x3d,
            &listener,
            &emitter,
            slot.desc.doppler ? (X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER) : X3DAUDIO_CALCULATE_MATRIX,
            &dsp);

        slot.voice->SetOutputMatrix(nullptr, 1, m_device->dstChannels, matrix);
        slot.voice->SetFrequencyRatio(clampf(slot.desc.pitch * (slot.desc.doppler ? dsp.DopplerFactor : 1.0f), 0.5f, 2.0f));
    }

    VoiceId AudioSystem::play(const ::Dark::AssetRef<SoundClip>& clip, const PlayDesc& desc)
    {
        assertMainThread();
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
        slot.inUse          = true;
        slot.music          = false;
        slot.occlusionFresh = desc.spatial;
        slot.occlusion      = startingOcclusion(desc.spatial, m_occlusionQuery, m_occlusionUser, m_listener.position, slot.desc.position);
        ++slot.generation;
        if (slot.generation == 0)
            slot.generation = 1;

        ClipKeep* keep = new (std::nothrow) ClipKeep;
        if (!keep)
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: out of memory holding a clip buffer");
            slot.inUse = false;
            slot.clip.reset();
            return 0;
        }
        keep->clip = clip;
        keep->slotIndex = index;
        {
            std::lock_guard<std::mutex> lock(m_device->keepMutex);
            m_device->keeps.push_back(keep);
        }
        slot.keep = keep;

        XAUDIO2_BUFFER buf{};
        buf.AudioBytes = clip->byteSize();
        buf.pAudioData = reinterpret_cast<const BYTE*>(clip->samples());
        buf.Flags      = XAUDIO2_END_OF_STREAM;
        buf.LoopCount  = desc.loop ? XAUDIO2_LOOP_INFINITE : 0;
        buf.pContext   = keep;

        const HRESULT hr = slot.voice->SubmitSourceBuffer(&buf);
        if (FAILED(hr))
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: SubmitSourceBuffer failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
            slot.inUse = false;
            slot.clip.reset();
            abandonUnsubmitted(keep);
            return 0;
        }

        applyMix(slot);
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

    VoiceId AudioSystem::play2D(const ::Dark::AssetRef<SoundClip>& clip, float volume, bool loop)
    {
        PlayDesc d{};
        d.volume = volume;
        d.loop   = loop;
        return play(clip, d);
    }

    VoiceId AudioSystem::play3D(const ::Dark::AssetRef<SoundClip>& clip, const Vector3f& position, float volume)
    {
        PlayDesc d{};
        d.volume   = volume;
        d.spatial  = true;
        d.position = position;
        return play(clip, d);
    }

    void AudioSystem::setMusic(const ::Dark::AssetRef<SoundClip>& clip, float volume)
    {
        assertMainThread();
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
        {
            slot.voice->Stop(0);
            slot.voice->FlushSourceBuffers();
        }
        slot.inUse = false;
        slot.music = false;
        // ClipKeep, not slot.clip, owns the PCM until OnBufferEnd is drained.
        slot.clip.reset();
        ++slot.generation;
        if (slot.generation == 0)
            slot.generation = 1;
    }

    void AudioSystem::stopClipVoices()
    {
        for (int i = 0; i < static_cast<int>(m_voices.size()); ++i)
        {
            if (!m_voices[static_cast<size_t>(i)])
                continue;
            VoiceSlot& slot = *m_voices[static_cast<size_t>(i)];
            if (slot.voice && (slot.inUse || slot.keep))
            {
                slot.voice->Stop(0);
                slot.voice->FlushSourceBuffers();
            }
            slot.inUse = false;
            slot.music = false;
            slot.clip.reset();
            ++slot.generation;
            if (slot.generation == 0)
                slot.generation = 1;
        }
        m_musicId = 0;
    }

    void AudioSystem::stopAll()
    {
        assertMainThread();
        stopClipVoices();
        std::lock_guard<std::mutex> lock(m_streamMutex);
        if (m_device)
            stopStreamLocked(m_device->stream.id);
    }

    bool AudioSystem::streamLive(StreamId id) const
    {
        return m_device && id != 0 && m_device->stream.id == id && m_device->stream.voice != nullptr;
    }

    StreamId AudioSystem::openStream(const StreamDesc& desc)
    {
        if (!m_valid || !m_device || !m_device->xaudio)
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: stream open failed, device not ready");
            return 0;
        }

        std::lock_guard<std::mutex> lock(m_streamMutex);
        Device::StreamVoice& stream = m_device->stream;
        if (stream.id != 0)
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: stream already open");
            return 0;
        }

        const StreamFormat& format = desc.format;
        if (format.channels != 2 || format.bitsPerSample != 16
            || format.sampleRate < XAUDIO2_MIN_SAMPLE_RATE
            || format.sampleRate > XAUDIO2_MAX_SAMPLE_RATE)
        {
            DE_LOG_ERROR(LogCategory::Audio,
                "Audio: stream format must be int16 stereo, {}-{} Hz (got {} ch, {} bits, {} Hz)",
                static_cast<uint32_t>(XAUDIO2_MIN_SAMPLE_RATE),
                static_cast<uint32_t>(XAUDIO2_MAX_SAMPLE_RATE),
                format.channels,
                format.bitsPerSample,
                format.sampleRate);
            return 0;
        }

        if (stream.voice)
        {
            stream.voice->DestroyVoice();
            stream.voice = nullptr;
        }

        WAVEFORMATEX fmt{};
        fmt.wFormatTag      = WAVE_FORMAT_PCM;
        fmt.nChannels       = 2;
        fmt.nSamplesPerSec  = format.sampleRate;
        fmt.wBitsPerSample  = 16;
        fmt.nBlockAlign     = 4;
        fmt.nAvgBytesPerSec = format.sampleRate * fmt.nBlockAlign;

        // No callback. A null send list routes to the existing mastering voice.
        const HRESULT hr = m_device->xaudio->CreateSourceVoice(&stream.voice, &fmt);
        if (FAILED(hr) || !stream.voice)
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: stream CreateSourceVoice failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
            if (stream.voice)
            {
                stream.voice->DestroyVoice();
                stream.voice = nullptr;
            }
            return 0;
        }

        const float volume = clampf(desc.volume, 0.0f, 1.0f);
        const HRESULT volHr = stream.voice->SetVolume(volume);
        if (FAILED(volHr))
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: stream SetVolume failed (HRESULT 0x{:08X})", static_cast<unsigned>(volHr));
            stream.voice->DestroyVoice();
            stream.voice = nullptr;
            return 0;
        }

        const HRESULT startHr = stream.voice->Start(0);
        if (FAILED(startHr))
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: stream Start failed (HRESULT 0x{:08X})", static_cast<unsigned>(startHr));
            stream.voice->DestroyVoice();
            stream.voice = nullptr;
            return 0;
        }

        if (stream.nextId == 0)
            stream.nextId = 1;
        const StreamId id = stream.nextId++;
        if (stream.nextId == 0)
            stream.nextId = 1;

        stream.id         = id;
        stream.volume     = volume;
        stream.writeIndex = 0;
        stream.paused     = false;
        return id;
    }

    bool AudioSystem::submitStreamPcm(StreamId id, const int16_t* interleaved, uint32_t frameCount)
    {
        if (frameCount > 0 && !interleaved)
        {
            DE_ASSERT(interleaved != nullptr);
            DE_LOG_ERROR(LogCategory::Audio, "Audio: stream submit rejected a null PCM buffer");
            return false;
        }
        if (frameCount == 0 || !interleaved)
            return false;
        if (frameCount > Device::StreamVoice::kMaxFrames)
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: stream packet is {} frames, max is {}", frameCount, Device::StreamVoice::kMaxFrames);
            return false;
        }

        std::lock_guard<std::mutex> lock(m_streamMutex);
        if (!streamLive(id))
            return false;

        Device::StreamVoice& stream = m_device->stream;
        XAUDIO2_VOICE_STATE state{};
        stream.voice->GetState(&state);
        uint32_t advanced = stream.writeIndex;
        if (!advanceStreamWriteIndex(Device::StreamVoice::kPackets, state.BuffersQueued, advanced))
            return false;

        const uint32_t bytes = frameCount * 2u * static_cast<uint32_t>(sizeof(int16_t));
        std::memcpy(stream.packets[stream.writeIndex], interleaved, bytes);

        XAUDIO2_BUFFER buf{};
        buf.AudioBytes = bytes;
        buf.pAudioData = reinterpret_cast<const BYTE*>(stream.packets[stream.writeIndex]);
        buf.Flags      = streamSubmitBufferFlags();

        const HRESULT hr = stream.voice->SubmitSourceBuffer(&buf);
        if (FAILED(hr))
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: stream SubmitSourceBuffer failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
            return false;
        }

        stream.writeIndex = advanced;
        return true;
    }

    void AudioSystem::pauseStream(StreamId id)
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        if (!streamLive(id))
            return;
        const HRESULT hr = m_device->stream.voice->Stop(0);
        if (FAILED(hr))
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: stream Stop failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
            return;
        }
        m_device->stream.paused = true;
    }

    void AudioSystem::resumeStream(StreamId id)
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        if (!streamLive(id) || !m_device->stream.paused)
            return;
        const HRESULT hr = m_device->stream.voice->Start(0);
        if (FAILED(hr))
        {
            DE_LOG_ERROR(LogCategory::Audio, "Audio: stream Start failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
            return;
        }
        m_device->stream.paused = false;
    }

    void AudioSystem::stopStream(StreamId id)
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        stopStreamLocked(id);
    }

    void AudioSystem::stopStreamLocked(StreamId id)
    {
        if (!streamLive(id))
            return;
        Device::StreamVoice& stream = m_device->stream;
        const HRESULT stopHr = stream.voice->Stop(0);
        if (FAILED(stopHr))
            DE_LOG_ERROR(LogCategory::Audio, "Audio: stream Stop failed (HRESULT 0x{:08X})", static_cast<unsigned>(stopHr));
        const HRESULT flushHr = stream.voice->FlushSourceBuffers();
        if (FAILED(flushHr))
            DE_LOG_ERROR(LogCategory::Audio, "Audio: stream FlushSourceBuffers failed (HRESULT 0x{:08X})", static_cast<unsigned>(flushHr));
        stream.voice->DestroyVoice();
        stream.voice      = nullptr;
        stream.id         = 0;
        stream.writeIndex = 0;
        stream.paused     = false;
    }

    void AudioSystem::setStreamVolume(StreamId id, float volume)
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        if (!streamLive(id))
            return;
        Device::StreamVoice& stream = m_device->stream;
        stream.volume = clampf(volume, 0.0f, 1.0f);
        const HRESULT hr = stream.voice->SetVolume(stream.volume);
        if (FAILED(hr))
            DE_LOG_ERROR(LogCategory::Audio, "Audio: stream SetVolume failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
    }

    uint32_t AudioSystem::streamBuffersQueued(StreamId id) const
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        if (!streamLive(id))
            return 0;
        XAUDIO2_VOICE_STATE state{};
        m_device->stream.voice->GetState(&state);
        return state.BuffersQueued;
    }

    uint64_t AudioSystem::streamSamplesPlayed(StreamId id) const
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        if (!streamLive(id))
            return 0;
        XAUDIO2_VOICE_STATE state{};
        m_device->stream.voice->GetState(&state);
        return state.SamplesPlayed;
    }

    bool AudioSystem::streamActive(StreamId id) const
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        return streamLive(id);
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

    void AudioSystem::setVoicePosition(VoiceId id, const Vector3f& position)
    {
        const int index = decodeIndex(id);
        if (!matches(id, index))
            return;
        VoiceSlot& slot = *m_voices[static_cast<size_t>(index)];
        slot.desc.position = position;
        if (slot.inUse && slot.desc.spatial && slot.voice)
            apply3D(slot);
    }

    void AudioSystem::setVoiceVolume(VoiceId id, float volume)
    {
        const int index = decodeIndex(id);
        if (!matches(id, index))
            return;
        VoiceSlot& slot = *m_voices[static_cast<size_t>(index)];
        slot.desc.volume = clampf(volume, 0.0f, 1.0f);
        if (slot.inUse && slot.voice)
            applyMix(slot);
    }

    float occludedGain(float occlusion)
    {
        return 1.0f - 0.7f * clampf(occlusion, 0.0f, 1.0f);
    }

    float occludedFilterFrequency(float occlusion, float sampleRate)
    {
        const float amount = clampf(occlusion, 0.0f, 1.0f);
        if (amount <= 0.0f || sampleRate <= 0.0f)
            return XAUDIO2_MAX_FILTER_FREQUENCY;
        const float low = clampf(2.0f * std::sin(3.14159265f * 900.0f / sampleRate), 0.01f, XAUDIO2_MAX_FILTER_FREQUENCY);
        return std::pow(low, amount);
    }

    float smoothOcclusion(float current, float target, float dt)
    {
        const float step = dt > 0.0f ? dt / 0.2f : 0.0f;
        if (current < target)
            return Math::Min(target, current + step);
        return Math::Max(target, current - step);
    }

    float startingOcclusion(bool spatial, OcclusionQuery query, void* user, const Vector3f& listener, const Vector3f& source)
    {
        if (!spatial || !query)
            return 0.0f;
        return clampf(query(user, listener, source), 0.0f, 1.0f);
    }

    void AudioSystem::applyMix(VoiceSlot& slot) const
    {
        if (!slot.voice)
            return;
        slot.voice->SetVolume(slot.desc.volume * occludedGain(slot.occlusion));
        const float freq = occludedFilterFrequency(slot.occlusion, static_cast<float>(slot.format.nSamplesPerSec));
        if (freq == slot.filterFreq)
            return;
        XAUDIO2_FILTER_PARAMETERS filter{ LowPassFilter, freq, 1.0f };
        if (SUCCEEDED(slot.voice->SetFilterParameters(&filter)))
            slot.filterFreq = freq;
    }

    void AudioSystem::updateOcclusion(float dt, OcclusionQuery query, void* user)
    {
        m_occlusionQuery = query;
        m_occlusionUser  = user;
        if (!m_valid)
            return;
        for (auto& ptr : m_voices)
        {
            if (!ptr || !ptr->inUse || ptr->music || !ptr->voice || !ptr->desc.spatial)
                continue;
            VoiceSlot&  slot   = *ptr;
            const float target = query ? query(user, m_listener.position, slot.desc.position) : 0.0f;
            slot.occlusion      = (slot.occlusionFresh || !query) ? target : smoothOcclusion(slot.occlusion, target, dt);
            slot.occlusionFresh = false;
            applyMix(slot);
        }
    }

    void AudioSystem::tick()
    {
        assertMainThread();
        drainFinishedClips();
        reclaimIdleKeeps();
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

    int AudioSystem::liveForeignSpatialVoices(uint32_t selfId) const
    {
        int count = 0;
        for (const std::unique_ptr<VoiceSlot>& slot : m_voices)
        {
            if (!slot || !slot->inUse || slot->music || !slot->desc.spatial)
                continue;
            const uint32_t sourceId = slot->desc.sourceId;
            if (sourceId != 0u && sourceId != selfId)
                ++count;
        }
        return count;
    }

} // namespace Dark::Audio
