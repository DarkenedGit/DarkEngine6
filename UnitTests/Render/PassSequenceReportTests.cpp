#include <gtest/gtest.h>

#include "Render/PassTable.h"

#include <cstring>

using namespace Dark;

TEST(PassSequenceReport, NamesEveryPassId)
{
    for (uint32_t i = 0; i <= static_cast<uint32_t>(PassId::EditorGizmos); ++i)
        EXPECT_STRNE(passName(static_cast<PassId>(i)), "Unknown") << i;
    EXPECT_STREQ(passName(PassId::Gtao), "GTAO");
    EXPECT_STREQ(passName(static_cast<PassId>(200)), "Unknown");
}

TEST(PassSequenceReport, HostCheckCountsEveryFailedFrame)
{
    uint32_t        count = 0;
    const PassNode* table = sandboxDeferredPasses(count);
    ASSERT_NE(table, nullptr);
    const uint32_t before = hostPassSequenceFailures();

    const PassId good[] = { PassId::GBuffer, PassId::Decals, PassId::Gtao, PassId::Ssr, PassId::Lighting, PassId::LocalVolumes, PassId::Translucent, PassId::Post };
    EXPECT_TRUE(validateHostPassSequence("test", table, count, good, 8));
    const uint32_t afterGood = hostPassSequenceFailures();

    const PassId bad[] = { PassId::Lighting, PassId::GBuffer };
    EXPECT_FALSE(validateHostPassSequence("test", table, count, bad, 2));
    EXPECT_FALSE(validateHostPassSequence("test", table, count, bad, 2)); // logged once, counted twice
    EXPECT_EQ(hostPassSequenceFailures(), afterGood + 2);
    EXPECT_EQ(afterGood, before);
}
