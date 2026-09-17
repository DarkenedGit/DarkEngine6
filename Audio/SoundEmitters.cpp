#include "Audio/SoundComponents.h"

#include "Assets/AssetManager.h"
#include "Audio/SoundClip.h"
#include "ECS/Components.h"
#include "ECS/World.h"

namespace Dark
{

    const SoundCueDesc* findSoundCue(const SoundBankComponent& bank, const char* name)
    {
        if (!name || name[0] == '\0')
            return nullptr;
        for (const SoundCueDesc& cue : bank.cues)
        {
            if (cue.name == name)
                return &cue;
        }
        return nullptr;
    }

    void addSoundCue(SoundBankComponent& bank, const char* name, AssetID clipId, float volume, bool spatial)
    {
        if (!name || name[0] == '\0' || clipId == NULL_ASSET)
            return;
        for (SoundCueDesc& cue : bank.cues)
        {
            if (cue.name != name)
                continue;
            cue.clipId  = clipId;
            cue.volume  = volume;
            cue.spatial = spatial;
            return;
        }
        SoundCueDesc cue;
        cue.name    = name;
        cue.clipId  = clipId;
        cue.volume  = volume;
        cue.spatial = spatial;
        bank.cues.push_back(std::move(cue));
    }

    namespace
    {
        AssetRef<Audio::SoundClip> cueClip(AssetManager& assets, const SoundCueDesc& cue)
        {
            return assets.getAs<Audio::SoundClip>(cue.clipId);
        }
    } // namespace

    bool playSoundCue(World& world, Audio::AudioSystem& audio, AssetManager& assets, Entity e, const char* name)
    {
        const SoundBankComponent* bank = e.valid() ? world.get<SoundBankComponent>(e) : nullptr;
        const SoundCueDesc*       cue  = bank ? findSoundCue(*bank, name) : nullptr;
        const auto                clip = cue ? cueClip(assets, *cue) : AssetRef<Audio::SoundClip>{};
        if (!clip || !clip->valid())
            return false;
        if (cue->spatial)
        {
            Math::Vector3f pos{};
            if (const TransformComponent* xf = world.get<TransformComponent>(e))
                pos = xf->position;
            audio.play3D(clip, pos, cue->volume);
        }
        else
            audio.play2D(clip, cue->volume);
        return true;
    }

    bool playSoundCueAt(World& world, Audio::AudioSystem& audio, AssetManager& assets, Entity e, const char* name, const Math::Vector3f& position)
    {
        const SoundBankComponent* bank = e.valid() ? world.get<SoundBankComponent>(e) : nullptr;
        const SoundCueDesc*       cue  = bank ? findSoundCue(*bank, name) : nullptr;
        const auto                clip = cue ? cueClip(assets, *cue) : AssetRef<Audio::SoundClip>{};
        if (!clip || !clip->valid())
            return false;
        audio.play3D(clip, position, cue->volume);
        return true;
    }

    bool playMusicCue(World& world, Audio::AudioSystem& audio, AssetManager& assets, Entity e, const char* name)
    {
        const SoundBankComponent* bank = e.valid() ? world.get<SoundBankComponent>(e) : nullptr;
        const SoundCueDesc*       cue  = bank ? findSoundCue(*bank, name) : nullptr;
        const auto                clip = cue ? cueClip(assets, *cue) : AssetRef<Audio::SoundClip>{};
        if (!clip || !clip->valid())
            return false;
        audio.setMusic(clip, cue->volume);
        return true;
    }

    void tickSoundEmitters(World& world, Audio::AudioSystem& audio, AssetManager& assets)
    {
        world.each<SoundEmitterComponent>([&](Entity e, SoundEmitterComponent& se) {
            if (se.playing && se.voice != 0 && !audio.isPlaying(se.voice))
            {
                se.playing = false;
                se.voice   = 0;
                if (!se.looping)
                    se.play = false;
            }

            if (se.play && !se.playing)
            {
                const AssetRef<Audio::SoundClip> clip = assets.getAs<Audio::SoundClip>(se.clipId);
                if (!clip || !clip->valid())
                    return;
                Audio::PlayDesc d{};
                d.volume      = se.volume;
                d.loop        = se.looping;
                d.spatial     = se.spatial;
                d.minDistance = se.minDistance;
                d.maxDistance = se.maxDistance;
                if (se.spatial)
                {
                    if (const TransformComponent* xf = world.get<TransformComponent>(e))
                        d.position = xf->position;
                }
                se.voice   = audio.play(clip, d);
                se.playing = se.voice != 0;
            }
            else if (!se.play && se.playing)
            {
                audio.stop(se.voice);
                se.voice   = 0;
                se.playing = false;
            }
            else if (se.playing && se.spatial)
            {
                if (const TransformComponent* xf = world.get<TransformComponent>(e))
                    audio.setVoicePosition(se.voice, xf->position);
            }
        });
    }

} // namespace Dark
