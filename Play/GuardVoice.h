#pragma once

#include "AI/Awareness.h"
#include "Assets/AssetHandle.h"
#include "Audio/AudioSystem.h"
#include "Audio/SpeechSynth.h"
#include "ECS/Entity.h"
#include "Math/Vector3f.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Dark
{

    class AssetManager;
    class World;

    // content/ai/barks.json. Each list is the pool one line is drawn from.
    struct GuardBarks
    {
        bool                                             enabled     = true;
        float                                            volume      = 1.0f;
        float                                            maxDistance = 40.0f;
        float                                            hearRange   = 30.0f;
        float                                            idleMin     = 25.0f;
        float                                            idleMax     = 55.0f;
        float                                            chatRange   = 8.0f;
        float                                            chatEvery   = 40.0f;
        std::vector<std::string>                         suspicious;
        std::vector<std::string>                         investigate;
        std::vector<std::string>                         alert;
        std::vector<std::string>                         answer;
        std::vector<std::string>                         lost;
        std::vector<std::string>                         giveUp;
        std::vector<std::string>                         foundBody;
        std::vector<std::string>                         idle;
        std::vector<std::string>                         missing;
        std::vector<std::pair<std::string, std::string>> chats;
    };

    bool parseGuardBarks(std::string_view jsonText, GuardBarks& out, std::string* error = nullptr);
    bool loadGuardBarks(const std::filesystem::path& path, GuardBarks& out, std::string* error = nullptr);

    struct GuardSubtitle
    {
        Entity      speaker{};
        std::string text;
        float       timeLeft = 0.0f;
    };

    class GuardVoice
    {
    public:
        void start(GuardBarks barks);
        void stop();
        void reset();
        void tick(World& world, Audio::AudioSystem& audio, AssetManager& assets, const Math::Vector3f& listener, float dt);
        void say(Entity guard, const std::string& text, float delay = 0.0f, Entity after = {});

        const std::vector<GuardSubtitle>& subtitles() const { return m_subtitles; }
        bool                              speaking() const { return m_synth.running(); }

    private:
        struct Guard
        {
            AI::Alertness  last     = AI::Alertness::Calm;
            float          cooldown = 0.0f;
            float          idleIn   = 0.0f;
            int            voice    = 0;
            int            pitch    = 0;
            int            rate     = 0;
            Audio::VoiceId playing  = 0;
        };
        struct Line
        {
            Entity      guard{};
            Entity      after{};
            std::string text;
            std::string key;
            float       delay   = 0.0f;
            float       waited  = 0.0f;
            uint64_t    request = 0;
        };

        const std::string& pickLine(const std::vector<std::string>& pool);
        float              random01();
        Guard&             guardFor(Entity e);

        GuardBarks                                  m_barks;
        Audio::SpeechSynth                          m_synth;
        std::unordered_map<uint32_t, Guard>         m_guards;
        std::unordered_map<uint64_t, std::string>   m_requests;
        std::unordered_map<std::string, AssetRef<Audio::SoundClip>> m_ready;
        std::vector<Line>                           m_lines;
        std::vector<GuardSubtitle>                  m_subtitles;
        std::string                                 m_lastLine;
        float                                       m_chatIn = 20.0f;
        float                                       m_clock  = 0.0f;
        float                                       m_lastAlertAt = -100.0f;
        uint32_t                                    m_rng    = 0x9E3779B9u;
    };

} // namespace Dark
