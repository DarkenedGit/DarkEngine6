#include <gtest/gtest.h>

#include "Audio/StreamQueue.h"

using namespace Dark::Audio;

TEST(StreamQueue, PacketsFree)
{
    EXPECT_EQ(streamPacketsFree(8, 0), 8u);
    EXPECT_EQ(streamPacketsFree(8, 8), 0u);
    EXPECT_EQ(streamPacketsFree(8, 9), 0u);
}

TEST(StreamQueue, WriteIndexAdvancesOnlyWhenFree)
{
    uint32_t writeIndex = 4;
    EXPECT_FALSE(advanceStreamWriteIndex(8, 8, writeIndex));
    EXPECT_EQ(writeIndex, 4u);

    EXPECT_FALSE(advanceStreamWriteIndex(8, 9, writeIndex));
    EXPECT_EQ(writeIndex, 4u);

    EXPECT_TRUE(advanceStreamWriteIndex(8, 0, writeIndex));
    EXPECT_EQ(writeIndex, 5u);

    writeIndex = 7;
    EXPECT_TRUE(advanceStreamWriteIndex(8, 7, writeIndex));
    EXPECT_EQ(writeIndex, 0u);
}
