#pragma once

#include "Assets/AssetHandle.h"
#include "Audio/AudioSystem.h"
#include "ECS/Entity.h"

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

    // Starts/stops voices from play flags. Does not call AudioSystem::tick (Application does).
    void tickSoundEmitters(World& world, Audio::AudioSystem& audio, AssetManager& assets);

} // namespace Dark
