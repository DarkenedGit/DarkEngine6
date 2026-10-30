#include "Play/DialogueSpeech.h"

#include "Assets/AssetManager.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"

#include <format>

namespace Dark
{
    namespace
    {
        void warnSynth(std::vector<std::string>& warned, const std::string& lineId)
        {
            for (const std::string& seen : warned)
            {
                if (seen == lineId)
                    return;
            }
            if (warned.size() < 64)
                warned.push_back(lineId);
            DE_LOG_WARN(LogCategory::AI, "Dialogue: speech synth failed for \"{}\"", lineId);
        }
    } // namespace

    void DialogueSpeech::setServices(AssetManager* assets, Audio::AudioSystem* audio, World* world)
    {
        m_assets = assets;
        m_audio  = audio;
        m_world  = world;
    }

    void DialogueSpeech::setClipHooks(ClipHooks hooks)
    {
        m_hooks = hooks;
    }

    bool DialogueSpeech::startSynth()
    {
        return m_synth.start();
    }

    void DialogueSpeech::stop()
    {
        if (m_audio && m_playing != 0)
            m_audio->stop(m_playing);
        m_playing = 0;
        m_request = 0;
        m_requestKey.clear();
    }

    DialogueLineAudio DialogueSpeech::hook()
    {
        DialogueLineAudio audio;
        audio.user = this;
        audio.acquire = [](void* user, const DialogueLine& line, Entity speaker, float waited, float& shown) {
            return static_cast<DialogueSpeech*>(user)->acquire(line, speaker, waited, shown);
        };
        audio.stop = [](void* user) { static_cast<DialogueSpeech*>(user)->stop(); };
        return audio;
    }

    void DialogueSpeech::playAtSpeaker(const AssetRef<Audio::SoundClip>& clip, Entity speaker)
    {
        if (!m_audio || !clip)
            return;
        if (m_playing != 0)
            m_audio->stop(m_playing);
        Audio::PlayDesc desc;
        desc.volume      = 1.0f;
        desc.spatial     = true;
        desc.doppler     = false;
        desc.minDistance = 3.0f;
        desc.maxDistance = 12.0f;
        desc.sourceId    = 0;
        desc.position    = Math::Vector3f{ 0.0f, 1.6f, 0.0f };
        if (m_world && speaker.valid())
        {
            if (const TransformComponent* xf = m_world->get<TransformComponent>(speaker))
                desc.position = xf->position + Math::Vector3f{ 0.0f, 1.6f, 0.0f };
        }
        m_playing = m_audio->play(clip, desc);
        if (m_playing == 0)
            DE_LOG_WARN(LogCategory::AI, "Dialogue: play() returned 0 for \"{}\"", clip->key());
    }

    AssetRef<Audio::SoundClip> DialogueSpeech::takeClip(const DialogueLine& line, Entity speaker, float& shown)
    {
        (void)speaker;
        if (line.clip.empty())
            return {};
        bool exists = false;
        if (m_hooks.exists)
            exists = m_hooks.exists(m_hooks.user, line.clip);
        else if (m_assets)
            exists = !m_assets->resolve(line.clip).empty();
        if (!exists)
            return {};
        AssetRef<Audio::SoundClip> clip;
        if (m_hooks.load)
            clip = m_hooks.load(m_hooks.user, line.clip);
        else if (m_assets)
            clip = m_assets->loadAudio(line.clip);
        if (!clip || !clip->valid())
            return {};
        if (clip->sampleRate() > 0)
            shown = static_cast<float>(clip->frameCount()) / static_cast<float>(clip->sampleRate()) + 0.6f;
        else
            shown = 2.5f;
        return clip;
    }

    bool DialogueSpeech::acquire(const DialogueLine& line, Entity speaker, float waited, float& shown)
    {
        if (AssetRef<Audio::SoundClip> clip = takeClip(line, speaker, shown))
        {
            playAtSpeaker(clip, speaker);
            return true;
        }

        const uint32_t h     = speaker.id() * 2654435761u;
        const int      voice = static_cast<int>(h >> 7);
        const int      pitch = static_cast<int>((h >> 12) % 6) - 3;
        const int      rate  = static_cast<int>((h >> 17) % 3) - 1;
        const std::string key = std::format("dlg:{}:{}:{}:{}:{}", voice, pitch, rate, line.id, line.text);

        if (m_assets)
        {
            if (AssetRef<Audio::SoundClip> ready = m_assets->findAudio(key))
            {
                if (ready->sampleRate() > 0)
                    shown = static_cast<float>(ready->frameCount()) / static_cast<float>(ready->sampleRate()) + 0.6f;
                else
                    shown = 2.5f;
                playAtSpeaker(ready, speaker);
                return true;
            }
        }

        if (!m_synth.running())
        {
            warnSynth(m_synthWarned, line.id);
            shown = 2.5f;
            return true;
        }

        if (m_request == 0 || m_requestKey != key)
        {
            m_request = m_synth.request(line.text, voice, rate, pitch);
            m_requestKey = key;
            if (m_request == 0)
            {
                warnSynth(m_synthWarned, line.id);
                shown = 2.5f;
                return true;
            }
        }

        Audio::SpeechResult result;
        while (m_synth.poll(result))
        {
            if (result.id != m_request || !result.ok)
                continue;
            auto made = std::make_shared<Audio::SoundClip>();
            if (!made->createFromPcm(std::move(result.pcm)))
                continue;
            made->setKey(key);
            AssetRef<Audio::SoundClip> clip = m_assets ? m_assets->internSoundClip(std::move(made), key) : AssetRef<Audio::SoundClip>{ made };
            m_request = 0;
            if (clip && clip->sampleRate() > 0)
                shown = static_cast<float>(clip->frameCount()) / static_cast<float>(clip->sampleRate()) + 0.6f;
            else
                shown = 2.5f;
            playAtSpeaker(clip, speaker);
            return true;
        }

        if (waited >= 4.0f)
        {
            warnSynth(m_synthWarned, line.id);
            m_request = 0;
            shown = 2.5f;
            return true;
        }
        return false;
    }

} // namespace Dark
