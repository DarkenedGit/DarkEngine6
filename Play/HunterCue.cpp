#include "Play/HunterCue.h"

#include "Animation/AnimGraphComponent.h"
#include "Audio/SoundComponents.h"
#include "ECS/World.h"

#include <cstring>

namespace Dark
{

    void triggerJumpAttackAnim(World& world, Entity e)
    {
        AnimGraphComponent* ag = e.valid() ? world.get<AnimGraphComponent>(e) : nullptr;
        if (ag)
            ag->graph.setTrigger("jump_attack");
    }

    void playHunterCue(World& world, Audio::AudioSystem& audio, AssetManager& assets, Entity hunter, const char* cue, HunterStepFn isStep, HunterMarkFn mark, void* user)
    {
        if (!cue || cue[0] == '\0')
            return;
        if (std::strcmp(cue, "grunt") == 0)
            triggerJumpAttackAnim(world, hunter);
        // The bank often has the cue, so the mark has to land before the sound returns.
        if (isStep && mark && isStep(user, hunter, cue))
            mark(user, hunter);
        if (playSoundCue(world, audio, assets, hunter, cue))
            return;
        if (std::strcmp(cue, "pounce") == 0)
            playSoundCue(world, audio, assets, hunter, "impact");
        else if (std::strcmp(cue, "pound") == 0)
        {
            if (!playSoundCue(world, audio, assets, hunter, "land"))
                playSoundCue(world, audio, assets, hunter, "impact");
        }
    }

} // namespace Dark
