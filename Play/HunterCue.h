#pragma once

#include "ECS/Entity.h"

namespace Dark
{

    class AssetManager;
    class World;

    namespace Audio
    {
        class AudioSystem;
    }

    using HunterStepFn = bool (*)(void* user, Entity hunter, const char* cue);
    using HunterMarkFn = void (*)(void* user, Entity hunter);

    void triggerJumpAttackAnim(World& world, Entity e);
    void playHunterCue(World& world, Audio::AudioSystem& audio, AssetManager& assets, Entity hunter, const char* cue, HunterStepFn isStep, HunterMarkFn mark, void* user);

} // namespace Dark
