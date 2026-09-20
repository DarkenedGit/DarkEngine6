#pragma once

#include "Combat/StatusFxEvent.h"
#include "ECS/Entity.h"

namespace Dark
{
    class AssetManager;
    class World;

    namespace Audio
    {
        class AudioSystem;
    }

    void tickStatusFx(World& world, Audio::AudioSystem* audio, AssetManager* assets, Combat::IStatusFx extra = {});
    void clearStatusFx(World& world, Audio::AudioSystem* audio, Entity pawn);
    void clearAllStatusFx(World& world, Audio::AudioSystem* audio);

} // namespace Dark
