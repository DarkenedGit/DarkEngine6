#pragma once

#include "Assets/AssetHandle.h"
#include "Audio/AudioSystem.h"
#include "Audio/SpeechSynth.h"
#include "ECS/Entity.h"
#include "Play/DialogueGraph.h"
#include "Play/DialogueRuntime.h"

#include <string>
#include <vector>

namespace Dark
{

    class AssetManager;
    class World;

    namespace Audio
    {
        class AudioSystem;
        class SoundClip;
    }

    // Own speech worker. A missing wav falls through to the synth and is not logged.
    class DialogueSpeech
    {
    public:
        struct ClipHooks
        {
            void* user = nullptr;
            bool (*exists)(void* user, const std::string& clip)                                      = nullptr;
            AssetRef<Audio::SoundClip> (*load)(void* user, const std::string& clip)                  = nullptr;
        };

        void setServices(AssetManager* assets, Audio::AudioSystem* audio, World* world);
        void setClipHooks(ClipHooks hooks);
        bool startSynth();
        void stop();

        DialogueLineAudio hook();
        bool              acquire(const DialogueLine& line, Entity speaker, float waited, float& shown);

    private:
        AssetRef<Audio::SoundClip> takeClip(const DialogueLine& line, Entity speaker, float& shown);
        void                       playAtSpeaker(const AssetRef<Audio::SoundClip>& clip, Entity speaker);

        AssetManager*          m_assets = nullptr;
        Audio::AudioSystem*    m_audio  = nullptr;
        World*                 m_world  = nullptr;
        ClipHooks              m_hooks{};
        Audio::SpeechSynth     m_synth;
        Audio::VoiceId         m_playing = 0;
        uint64_t               m_request = 0;
        std::string            m_requestKey;
        std::vector<std::string> m_synthWarned;
    };

} // namespace Dark
