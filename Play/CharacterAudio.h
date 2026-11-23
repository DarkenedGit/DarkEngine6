#pragma once

#include "Audio/SoundStimulus.h"
#include "ECS/Entity.h"

#include <string>
#include <string_view>

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

    // Rows live in content/ai/sound-stimuli.json. A name that is not listed does not alert.
    // The file is re-read when it changes. The bank copies the row when the character is attached.
    Audio::SoundStimulus stimulusForCue(CueOwner owner, const char* name);

    // Replaces the live table. A failed parse leaves the live table as it was.
    bool loadCueStimuli(std::string_view json, std::string* error = nullptr);

    // Reads content/ai/sound-stimuli.json and drops a table installed by loadCueStimuli.
    bool reloadCueStimuli();

    void attachPlayerSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e, const Terrain::TerrainGround& ground);
    void attachHunterSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e, const Terrain::TerrainGround& ground);

} // namespace Dark
