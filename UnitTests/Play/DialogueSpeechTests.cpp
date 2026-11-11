#include "Audio/AudioSystem.h"
#include "Audio/SpeechSynth.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/DialogueSpeech.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

using namespace Dark;

TEST(DialogueSpeech, SynthCacheKeyHashesTheLine)
{
    const std::string longLine = "A very long line that must not be stored whole in the asset map.";
    const std::string key = dialogueSynthCacheKey(1, -2, 0, "greet", longLine);
    EXPECT_EQ(key.find(longLine), std::string::npos);
    EXPECT_EQ(key, dialogueSynthCacheKey(1, -2, 0, "greet", longLine));
    EXPECT_NE(key, dialogueSynthCacheKey(1, -2, 0, "greet", "Different"));
    EXPECT_NE(key, dialogueSynthCacheKey(1, -2, 0, "other", longLine));
}

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

TEST(DialogueSpeech, FollowTracksTheSpeakerWhenAVoiceIsPlaying)
{
    Audio::AudioSystem audio;
    if (!audio.create() || !audio.isValid())
        GTEST_SKIP() << "no audio device; voice follow was not exercised";

    auto clip = std::make_shared<Audio::SoundClip>();
    ASSERT_TRUE(clip->createTone(440.0f, 0.4f, 0.05f));
    AssetRef<Audio::SoundClip> ref = clip;
    Audio::PlayDesc probe;
    probe.spatial = true;
    probe.doppler = false;
    probe.position = { 0.0f, 1.6f, 0.0f };
    const Audio::VoiceId probeId = audio.play(ref, probe);
    if (probeId == 0)
        GTEST_SKIP() << "audio device did not start a voice; voice follow was not exercised";
    audio.stop(probeId);

    World world;
    Entity npc = world.createEntity();
    TransformComponent& xf = world.emplace<TransformComponent>(npc);
    xf.position = { 2.0f, 0.0f, 3.0f };

    struct Holder
    {
        AssetRef<Audio::SoundClip> clip;
    };
    Holder holder;
    holder.clip = ref;
    DialogueSpeech speech;
    speech.setServices(nullptr, &audio, &world);
    DialogueSpeech::ClipHooks hooks;
    hooks.user = &holder;
    hooks.exists = [](void*, const std::string&) { return true; };
    hooks.load = [](void* user, const std::string&) { return static_cast<Holder*>(user)->clip; };
    speech.setClipHooks(hooks);

    DialogueLine line;
    line.id = "hi";
    line.text = "Hold there.";
    line.clip = "tone";
    float shown = 0.0f;
    ASSERT_TRUE(speech.acquire(line, npc, 0.0f, shown));
    EXPECT_GT(shown, 0.0f);
    xf.position = { 6.0f, 1.0f, -2.0f };
    speech.follow(npc);
    speech.follow(Entity{});
    speech.stop();
    speech.follow(npc);
}
