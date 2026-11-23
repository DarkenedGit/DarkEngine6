#pragma once

#include "Audio/SoundStimulus.h"
#include "ECS/Entity.h"

namespace Dark
{

    class AssetManager;
    class AssetPinTable;
    class World;

    namespace Audio
    {
        class AudioSystem;
    }

    namespace Terrain
    {
        class TerrainGround;
    }

    enum class CueOwner
    {
        Player,
        Hunter
    };

    // Keyed by owner and cue name. Hunter "fire" is empty so a status tick is not a gunshot.
    Audio::SoundStimulus stimulusForCue(CueOwner owner, const char* name);

    void attachPlayerSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e, const Terrain::TerrainGround& ground);
    void attachHunterSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e, const Terrain::TerrainGround& ground);

} // namespace Dark
