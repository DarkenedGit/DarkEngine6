#include "Audio/SpeechSynth.h"
#include "Play/DialogueSpeech.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace Dark;

TEST(DialogueSpeech, MissingWavDoesNotLoad)
{
    DialogueSpeech speech;
    int loads = 0;
    DialogueSpeech::ClipHooks hooks;
    hooks.user = &loads;
    hooks.exists = [](void*, const std::string&) { return false; };
    hooks.load = [](void* user, const std::string&) {
        *static_cast<int*>(user) += 1;
        return AssetRef<Audio::SoundClip>{};
    };
    speech.setClipHooks(hooks);

    DialogueLine line;
    line.id = "hi";
    line.text = "Hold there.";
    line.clip = "dialogue/missing.wav";
    float shown = 0.0f;
    EXPECT_TRUE(speech.acquire(line, Entity{ 1 }, 0.0f, shown));
    EXPECT_EQ(loads, 0);
    EXPECT_FLOAT_EQ(shown, 2.5f);
}

TEST(DialogueSpeech, SynthSpeaksWhenVoicesExist)
{
    DialogueSpeech speech;
    if (!speech.startSynth())
        GTEST_SKIP() << "no speech on this platform";
    DialogueLine line;
    line.id = "hi";
    line.text = "Who goes there?";
    float shown = 0.0f;
    bool ready = false;
    for (int i = 0; i < 40 && !ready; ++i)
    {
        ready = speech.acquire(line, Entity{ 1 }, static_cast<float>(i) * 0.1f, shown);
        if (!ready)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!ready)
        GTEST_SKIP() << "no SAPI voice installed";
    EXPECT_GT(shown, 0.0f);
    speech.stop();
}
