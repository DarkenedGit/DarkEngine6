#pragma once

#include "Audio/SoundClip.h"
#include "Math/Vector3f.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
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
        Math::Vector3f forward{ 0.0f, 0.0f, 1.0f };
        Math::Vector3f up{ 0.0f, 1.0f, 0.0f };
        Math::Vector3f velocity;
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
    };

    using VoiceId = uint32_t; // 0 = invalid

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

        std::shared_ptr<SoundClip> loadWav(const std::filesystem::path& path);
        std::shared_ptr<SoundClip> loadWav(AssetManager& assets, const char* virtualPath);
        std::shared_ptr<SoundClip> createTone(float freqHz, float durationSec, float amplitude = 0.35f);
        std::shared_ptr<SoundClip> createBlip(float freqHz, float durationSec, float amplitude = 0.45f);
        std::shared_ptr<SoundClip> loadOrBlip(
            AssetManager& assets,
            const char* virtualPath,
            float fallbackFreqHz,
            float fallbackDurationSec,
            float fallbackAmp = 0.45f);

        VoiceId play(const std::shared_ptr<SoundClip>& clip, const PlayDesc& desc = {});
        VoiceId play2D(const std::shared_ptr<SoundClip>& clip, float volume = 1.0f, bool loop = false);
        VoiceId play3D(const std::shared_ptr<SoundClip>& clip, const Math::Vector3f& position, float volume = 1.0f);

        void setMusic(const std::shared_ptr<SoundClip>& clip, float volume = 0.18f);
        void stopMusic();

        void stop(VoiceId id);
        void stopAll();
        bool isPlaying(VoiceId id) const;

        void setMasterVolume(float volume);
        float masterVolume() const { return m_masterVolume; }

        void setListener(const AudioListener& listener) { m_listener = listener; }
        const AudioListener& listener() const { return m_listener; }

        // Apply 3D and reclaim finished one-shots. Call once per frame after gameplay.
        void tick();

    private:
        struct VoiceSlot;

        bool createDevice();
        void apply3D(VoiceSlot& slot) const;
        int  allocSlot(bool allowSteal);
        VoiceId makeId(int index) const;
        int  decodeIndex(VoiceId id) const;
        bool matches(VoiceId id, int index) const;
        bool voiceIdle(const VoiceSlot& slot) const;
        bool ensureVoice(VoiceSlot& slot, const SoundClip& clip);

        struct Device;
        std::unique_ptr<Device> m_device;
        std::vector<std::unique_ptr<VoiceSlot>> m_voices;

        std::unordered_map<std::string, std::shared_ptr<SoundClip>> m_clips;
        AudioListener m_listener{};
        float         m_masterVolume = 1.0f;
        bool          m_valid        = false;
        bool          m_comInited    = false;
        VoiceId       m_musicId      = 0;
    };

} // namespace Dark::Audio
