#pragma once

#include "Assets/AssetHandle.h"
#include "Audio/SoundClip.h"
#include "Audio/SoundStimulus.h"
#include "Math/Vector3f.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Dark
{
    class AssetManager;
}

namespace Dark::Audio
{
    struct AudioListener
    {
        Math::Vector3f position;
        Math::Vector3f forward{ Math::Vect3f::Z_AXIS };
        Math::Vector3f up{ Math::Vect3f::Y_AXIS };
        Math::Vector3f velocity;
        float          distanceScale = 1.0f; // Hear skill scalar, not the X3DAudio curve.
    };

    struct PlayDesc
    {
        float          volume      = 1.0f;
        float          pitch       = 1.0f;
        bool           loop        = false;
        bool           spatial     = false;
        Math::Vector3f position;
        float          minDistance = 2.0f;
        float          maxDistance = 64.0f;
        uint32_t       sourceId    = 0; // 0 is unattributed and does not train Hear.
        bool           doppler     = true;
        bool           ui          = false; // Interface click. World hold does not stop it.
        SoundStimulus  stimulus{};          // Empty cues do not alert guards. play3D leaves this empty.
    };

    using VoiceId = uint32_t; // 0 = invalid

    struct StreamFormat
    {
        uint32_t sampleRate    = 48000;
        uint16_t channels      = 2;
        uint16_t bitsPerSample = 16;
    };

    struct StreamDesc
    {
        float        volume = 1.0f;
        StreamFormat format{};
    };

    using StreamId = uint32_t; // 0 invalid. Non-zero generation. One slot.

    // 0 = clear, 1 = behind walls.
    using OcclusionQuery = float (*)(void* user, const Math::Vector3f& listener, const Math::Vector3f& source);

    // Gain and XAudio2 low-pass setting for an occlusion amount. A blocked voice is quieter and muffled.
    float occludedGain(float occlusion);
    float occludedFilterFrequency(float occlusion, float sampleRate);
    // Moves current toward target, a full swing in about 0.2 s.
    float smoothOcclusion(float current, float target, float dt);
    // Occlusion a voice starts at. A 2D voice, or a missing query, stays clear.
    float startingOcclusion(bool spatial, OcclusionQuery query, void* user, const Math::Vector3f& listener, const Math::Vector3f& source);

    class AudioSystem
    {
    public:
        static constexpr uint32_t kMaxVoices = 24;

        AudioSystem();
        ~AudioSystem();

        AudioSystem(const AudioSystem&)            = delete;
        AudioSystem& operator=(const AudioSystem&) = delete;

        bool create();
        void shutdown();
        bool isValid() const { return m_valid; }

        ::Dark::AssetRef<SoundClip> loadWav(AssetManager& assets, const char* virtualPath);
        ::Dark::AssetRef<SoundClip> createTone(AssetManager& assets, float freqHz, float durationSec, float amplitude = 0.35f);
        ::Dark::AssetRef<SoundClip> createBlip(AssetManager& assets, float freqHz, float durationSec, float amplitude = 0.45f);
        ::Dark::AssetRef<SoundClip> loadOrBlip(
            AssetManager& assets,
            const char* virtualPath,
            float fallbackFreqHz,
            float fallbackDurationSec,
            float fallbackAmp = 0.45f);

        VoiceId play(const ::Dark::AssetRef<SoundClip>& clip, const PlayDesc& desc = {});
        VoiceId play2D(const ::Dark::AssetRef<SoundClip>& clip, float volume = 1.0f, bool loop = false);
        // No stimulus and no sourceId. Place sounds and throwable impacts do not alert guards.
        VoiceId play3D(const ::Dark::AssetRef<SoundClip>& clip, const Math::Vector3f& position, float volume = 1.0f);

        // Gameplay hook. play() calls it before the device, the clip, and the voice cap are checked.
        // Null fn clears the hook. UI, music, loops, and non-spatial plays do not call it.
        using StimulusReport = void (*)(void* user, const Math::Vector3f& position, uint32_t sourceId, const SoundStimulus& stimulus);
        void setStimulusReport(StimulusReport fn, void* user);

        void setMusic(const ::Dark::AssetRef<SoundClip>& clip, float volume = 0.18f);
        void setVoicePosition(VoiceId id, const Math::Vector3f& position);
        void setVoiceVolume(VoiceId id, float volume);
        void stopMusic();

        // Stops every in-use voice except music and interface clicks, without flushing. Clear resumes them.
        // A world clip started while held is stopped before it is heard. Music keeps playing.
        void setWorldHeld(bool held);
        bool worldHeld() const { return m_worldHeld; }

        void stop(VoiceId id);
        // Clip slots only. The streaming voice keeps playing.
        void stopClipVoices();
        void stopAll();
        bool isPlaying(VoiceId id) const;

        // One voice outside kMaxVoices, sent to the same mastering voice.
        StreamId openStream(const StreamDesc& desc);
        bool submitStreamPcm(StreamId id, const int16_t* interleaved, uint32_t frameCount);
        void pauseStream(StreamId id); // Stop, keep queued buffers.
        void resumeStream(StreamId id);
        void stopStream(StreamId id); // Stop, flush, and the id dies.
        void setStreamVolume(StreamId id, float volume);
        uint32_t streamBuffersQueued(StreamId id) const;
        uint64_t streamSamplesPlayed(StreamId id) const;
        bool streamActive(StreamId id) const;

        void setMasterVolume(float volume);
        float masterVolume() const { return m_masterVolume; }

        void setListener(const AudioListener& listener) { m_listener = listener; }
        const AudioListener& listener() const { return m_listener; }

        // In-use spatial voices whose source is neither 0 nor selfId. Music is not counted.
        int liveForeignSpatialVoices(uint32_t selfId) const;

        // Apply 3D and reclaim finished one-shots. Call once per frame after gameplay.
        // Also drops clip buffers XAudio2 has finished. stop() does not free those
        // buffers; the PCM stays alive until this drain runs.
        void tick();

        // Asks query about every live 3D voice and fades it toward the answer. A voice's first
        // update snaps. A null query clears every voice. play() keeps this query and uses it
        // before the voice starts, so a sound begun later in the frame is already occluded.
        void updateOcclusion(float dt, OcclusionQuery query, void* user);

    private:
        struct VoiceSlot;

        bool createDevice();
        void apply3D(VoiceSlot& slot) const;
        void applyMix(VoiceSlot& slot) const;
        int  allocSlot(bool allowSteal);
        VoiceId makeId(int index) const;
        int  decodeIndex(VoiceId id) const;
        bool matches(VoiceId id, int index) const;
        bool voiceIdle(const VoiceSlot& slot) const;
        bool ensureVoice(VoiceSlot& slot, const SoundClip& clip);
        bool streamLive(StreamId id) const;
        void stopStreamLocked(StreamId id); // Caller holds m_streamMutex.
        // XAudio2 reads SoundClip::samples() until OnBufferEnd. ClipKeep holds that ref.
        friend void noteSourceBufferEnd(void* user, void* context);
        void noteBufferEnd(void* context);
        void drainFinishedClips();
        void reclaimIdleKeeps();
        // Removes keep from the live lists. Null when another path already claimed it.
        void* claimKeep(void* keep);
        void clearSlotKeep(void* keep);
        void destroySlotVoice(VoiceSlot& slot);
        void releaseKeepsFor(VoiceSlot* slot);
        void abandonUnsubmitted(void* keep);
        void destroyAllKeeps();
        void reportStimulus(const PlayDesc& desc);

        struct Device;
        std::unique_ptr<Device> m_device;
        // Stream voice, write index, SubmitSourceBuffer, and GetState. Not held with MoviePlayer::m_mutex.
        mutable std::mutex m_streamMutex;
        std::vector<std::unique_ptr<VoiceSlot>> m_voices;

        AudioListener  m_listener{};
        OcclusionQuery m_occlusionQuery = nullptr;
        void*          m_occlusionUser  = nullptr;
        StimulusReport m_stimulusReport = nullptr;
        void*          m_stimulusUser   = nullptr;
        float          m_masterVolume   = 1.0f;
        bool           m_valid          = false;
        bool           m_comInited      = false;
        VoiceId        m_musicId        = 0;
        bool           m_worldHeld      = false;
    };

} // namespace Dark::Audio
