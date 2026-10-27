#include "Audio/SpeechSynth.h"
#include "Core/ContentRoots.h"
#include "Play/GuardVoice.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace Dark;

TEST(GuardBarks, ParsesLinesAndChats)
{
    GuardBarks barks;
    ASSERT_TRUE(parseGuardBarks(R"({ "volume": 0.8, "lines": { "alert": ["There!", 3, ""], "idle": ["Long night."] },
                                     "chats": [ ["Quiet.", "Too quiet."], ["broken"] ] })",
                                barks));
    EXPECT_FLOAT_EQ(barks.volume, 0.8f);
    ASSERT_EQ(barks.alert.size(), 1u);
    EXPECT_EQ(barks.alert[0], "There!");
    ASSERT_EQ(barks.chats.size(), 1u);
    EXPECT_EQ(barks.chats[0].second, "Too quiet.");
}

TEST(GuardBarks, WrongTypeFailsWithoutCrashing)
{
    GuardBarks  barks;
    std::string error;
    EXPECT_FALSE(parseGuardBarks(R"({ "volume": "loud" })", barks, &error));
    EXPECT_NE(error.find("volume"), std::string::npos);
}

TEST(GuardBarks, ShippedFileLoads)
{
    bool loaded = false;
    for (const std::filesystem::path& root : contentRootCandidates())
    {
        GuardBarks barks;
        if (!loadGuardBarks(root / "ai" / "barks.json", barks))
            continue;
        loaded = true;
        EXPECT_FALSE(barks.alert.empty());
        EXPECT_FALSE(barks.chats.empty());
        break;
    }
    EXPECT_TRUE(loaded);
}

TEST(SpeechSynth, SpeaksALineToPcmWhenVoicesExist)
{
    Audio::SpeechSynth synth;
    if (!synth.start())
        GTEST_SKIP() << "no speech on this platform";
    const uint64_t id = synth.request("Who's there?", 0, 0, 0);
    ASSERT_NE(id, 0u);
    Audio::SpeechResult result;
    bool                got = false;
    for (int i = 0; i < 200 && !got; ++i)
    {
        got = synth.poll(result);
        if (!got)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!synth.running())
        GTEST_SKIP() << "no SAPI voice installed";
    ASSERT_TRUE(got);
    EXPECT_EQ(result.id, id);
    EXPECT_TRUE(result.ok);
    EXPECT_GT(result.pcm.samples.size(), 4000u);
    EXPECT_EQ(result.pcm.sampleRate, 22050u);
}
