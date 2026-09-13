#include "Audio/SoundComponents.h"

#include "Assets/AssetManager.h"
#include "Audio/SoundClip.h"
#include "ECS/Components.h"
#include "ECS/World.h"

namespace Dark
{

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
