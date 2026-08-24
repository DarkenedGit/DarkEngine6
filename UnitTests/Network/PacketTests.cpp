#include <gtest/gtest.h>

#include "Network/Packet.h"

#include <bit>
#include <cstdint>

using namespace Dark;

TEST(Packet, RoundTripScalarsAndBytes)
{
    uint8_t buf[32]{};
    PacketWriter w;
    ASSERT_TRUE(w.begin(buf, sizeof(buf)));
    EXPECT_TRUE(w.writeU8(0xAB));
    EXPECT_TRUE(w.writeU16(0x0201));
    EXPECT_TRUE(w.writeU32(0x08070605));
    EXPECT_TRUE(w.writeF32(1.0f));
    const uint8_t blob[] = {9, 8, 7};
    EXPECT_TRUE(w.writeBytes(blob, sizeof(blob)));
    EXPECT_EQ(w.size(), 1u + 2u + 4u + 4u + 3u);

    PacketReader r;
    ASSERT_TRUE(r.begin(buf, w.size()));
    EXPECT_EQ(r.remaining(), w.size());

    uint8_t  u8  = 0;
    uint16_t u16 = 0;
    uint32_t u32 = 0;
    float    f   = 0.0f;
    uint8_t  out[3]{};
    EXPECT_TRUE(r.readU8(u8));
    EXPECT_EQ(u8, 0xAB);
    EXPECT_TRUE(r.readU16(u16));
    EXPECT_EQ(u16, 0x0201);
    EXPECT_TRUE(r.readU32(u32));
    EXPECT_EQ(u32, 0x08070605u);
    EXPECT_TRUE(r.readF32(f));
    EXPECT_EQ(f, 1.0f);
    EXPECT_TRUE(r.readBytes(out, 3));
    EXPECT_EQ(out[0], 9);
    EXPECT_EQ(out[1], 8);
    EXPECT_EQ(out[2], 7);
    EXPECT_EQ(r.remaining(), 0u);
}

TEST(Packet, LittleEndianLayout)
{
    uint8_t buf[8]{};
    PacketWriter w;
    ASSERT_TRUE(w.begin(buf, sizeof(buf)));
    ASSERT_TRUE(w.writeU16(0x0201));
    ASSERT_TRUE(w.writeU32(0x08070605));
    EXPECT_EQ(buf[0], 0x01);
    EXPECT_EQ(buf[1], 0x02);
    EXPECT_EQ(buf[2], 0x05);
    EXPECT_EQ(buf[3], 0x06);
    EXPECT_EQ(buf[4], 0x07);
    EXPECT_EQ(buf[5], 0x08);

    PacketWriter f;
    uint8_t fbuf[4]{};
    ASSERT_TRUE(f.begin(fbuf, sizeof(fbuf)));
    ASSERT_TRUE(f.writeF32(1.0f));
    const uint32_t bits = std::bit_cast<uint32_t>(1.0f);
    EXPECT_EQ(fbuf[0], static_cast<uint8_t>(bits));
    EXPECT_EQ(fbuf[1], static_cast<uint8_t>(bits >> 8));
    EXPECT_EQ(fbuf[2], static_cast<uint8_t>(bits >> 16));
    EXPECT_EQ(fbuf[3], static_cast<uint8_t>(bits >> 24));
}

TEST(Packet, WriteOverflowDoesNotAdvance)
{
    uint8_t buf[3]{};
    PacketWriter w;
    ASSERT_TRUE(w.begin(buf, sizeof(buf)));
    EXPECT_TRUE(w.writeU8(1));
    EXPECT_TRUE(w.writeU8(2));
    EXPECT_TRUE(w.writeU8(3));
    EXPECT_EQ(w.size(), 3u);
    EXPECT_FALSE(w.writeU8(4));
    EXPECT_FALSE(w.writeU16(1));
    EXPECT_FALSE(w.writeU32(1));
    EXPECT_FALSE(w.writeF32(1.0f));
    const uint8_t blob[] = {1, 2};
    EXPECT_FALSE(w.writeBytes(blob, 2));
    EXPECT_EQ(w.size(), 3u);
}

TEST(Packet, ReadUnderrunLeavesRemaining)
{
    uint8_t buf[2]{0x11, 0x22};
    PacketReader r;
    ASSERT_TRUE(r.begin(buf, sizeof(buf)));
    uint8_t u8 = 0;
    EXPECT_TRUE(r.readU8(u8));
    EXPECT_EQ(r.remaining(), 1u);

    uint16_t u16 = 0;
    uint32_t u32 = 0;
    float    f   = 0.0f;
    uint8_t  two[2]{};
    EXPECT_FALSE(r.readU16(u16));
    EXPECT_FALSE(r.readU32(u32));
    EXPECT_FALSE(r.readF32(f));
    EXPECT_FALSE(r.readBytes(two, 2));
    EXPECT_EQ(r.remaining(), 1u);
}

TEST(Packet, BeginRejectsNull)
{
    PacketWriter w;
    EXPECT_FALSE(w.begin(nullptr, 8));
    EXPECT_EQ(w.size(), 0u);
    EXPECT_FALSE(w.writeU8(1));

    PacketReader r;
    EXPECT_FALSE(r.begin(nullptr, 8));
    EXPECT_EQ(r.remaining(), 0u);
    uint8_t v = 0;
    EXPECT_FALSE(r.readU8(v));
}

TEST(Packet, EmptyWriteBytes)
{
    uint8_t buf[4]{};
    PacketWriter w;
    ASSERT_TRUE(w.begin(buf, sizeof(buf)));
    EXPECT_TRUE(w.writeBytes(nullptr, 0));
    EXPECT_EQ(w.size(), 0u);
}
