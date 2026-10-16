#pragma once

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

    void attachPlayerSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e, const Terrain::TerrainGround& ground);
    void attachHunterSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e, const Terrain::TerrainGround& ground);

} // namespace Dark
