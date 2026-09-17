#pragma once

#include "Assets/AssetHandle.h"
#include "Audio/AudioSystem.h"
#include "ECS/Entity.h"
#include "Math/Vector3f.h"

#include <string>
#include <vector>

namespace Dark
{

    class AssetManager;
    class World;

    struct AudioListenerComponent
    {
        static constexpr const char* kTypeName = "AudioListener";
        bool                         primary   = true;
    };

    struct SoundEmitterComponent
    {
        static constexpr const char* kTypeName = "SoundEmitter";

        AssetID            clipId      = NULL_ASSET;
        float              volume      = 1.0f;
        float              minDistance = 2.0f;
        float              maxDistance = 64.0f;
        bool               looping     = false;
        bool               spatial     = true;
        bool               play        = false;
        bool               playing     = false;
        Audio::VoiceId     voice       = 0;
    };

    struct SoundCueDesc
    {
        std::string name;
        AssetID     clipId  = NULL_ASSET;
        float       volume  = 1.0f;
        bool        spatial = false;
    };

    // Named one-shot (and music) clips owned by an entity.
    struct SoundBankComponent
    {
        static constexpr const char* kTypeName = "SoundBank";

        std::vector<SoundCueDesc> cues;
    };

    const SoundCueDesc* findSoundCue(const SoundBankComponent& bank, const char* name);
    void                addSoundCue(SoundBankComponent& bank, const char* name, AssetID clipId, float volume = 1.0f, bool spatial = false);

    // Starts/stops voices from play flags. Does not call AudioSystem::tick (Application does).
    void tickSoundEmitters(World& world, Audio::AudioSystem& audio, AssetManager& assets);

    bool playSoundCue(World& world, Audio::AudioSystem& audio, AssetManager& assets, Entity e, const char* name);
    bool playSoundCueAt(World& world, Audio::AudioSystem& audio, AssetManager& assets, Entity e, const char* name, const Math::Vector3f& position);
    bool playMusicCue(World& world, Audio::AudioSystem& audio, AssetManager& assets, Entity e, const char* name);

} // namespace Dark
