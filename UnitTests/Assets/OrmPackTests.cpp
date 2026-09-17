#include <gtest/gtest.h>

#include "Assets/Image.h"
#include "Assets/Material.h"
#include "Math/Color.h"

#include <cstring>

using Dark::Image;
using Dark::ImageFormat;
using Dark::packOrmImage;
using Dark::Color::ColorSpace;

TEST(OrmPack, PacksOneByOneOccAndMr)
{
    const uint8_t occPx[] = { 32, 0, 0, 255 };
    const uint8_t mrPx[]  = { 0, 64, 192, 255 };
    Image         occ;
    Image         mr;
    Image         out;
    ASSERT_TRUE(occ.createFromRGBA(occPx, 1, 1, 4));
    ASSERT_TRUE(mr.createFromRGBA(mrPx, 1, 1, 4));
    ASSERT_TRUE(packOrmImage(&occ, &mr, out));
    ASSERT_TRUE(out.valid());
    EXPECT_EQ(out.width(), 1u);
    EXPECT_EQ(out.height(), 1u);
    EXPECT_EQ(out.format(), ImageFormat::RGBA8);
    EXPECT_EQ(out.colorSpace(), ColorSpace::Linear);
    EXPECT_FALSE(out.colorSpaceWasDefaulted());
    const uint8_t* px = out.pixels();
    ASSERT_NE(px, nullptr);
    EXPECT_EQ(px[0], 32);
    EXPECT_EQ(px[1], 64);
    EXPECT_EQ(px[2], 192);
    EXPECT_EQ(px[3], 255);
}

TEST(OrmPack, SamePointerMemcpy)
{
    const uint8_t packed[] = { 11, 22, 33, 44 };
    Image         src;
    Image         out;
    ASSERT_TRUE(src.createFromRGBA(packed, 1, 1, 4));
    ASSERT_TRUE(packOrmImage(&src, &src, out));
    ASSERT_TRUE(out.valid());
    EXPECT_EQ(out.width(), 1u);
    EXPECT_EQ(out.height(), 1u);
    const uint8_t* px = out.pixels();
    ASSERT_NE(px, nullptr);
    EXPECT_EQ(px[0], 11);
    EXPECT_EQ(px[1], 22);
    EXPECT_EQ(px[2], 33);
    EXPECT_EQ(px[3], 44);
    EXPECT_EQ(out.colorSpace(), ColorSpace::Linear);
    EXPECT_FALSE(out.colorSpaceWasDefaulted());
}

TEST(OrmPack, RejectsR32F)
{
    const float samples[] = { 0.5f };
    Image       r32;
    Image       rgba;
    Image       out;
    ASSERT_TRUE(r32.createFromR32Float(samples, 1, 1, 4));
    ASSERT_TRUE(rgba.createSolidColor(1, 2, 3, 4));
    EXPECT_FALSE(packOrmImage(&r32, &rgba, out));
    EXPECT_FALSE(packOrmImage(&rgba, &r32, out));
    EXPECT_FALSE(packOrmImage(&r32, &r32, out));
    EXPECT_FALSE(packOrmImage(&r32, nullptr, out));
    EXPECT_FALSE(packOrmImage(nullptr, &r32, out));
}

TEST(OrmPack, PaddedRowPitch)
{
    // 2x2 RGBA8 with 4 bytes of row padding (pitch 12, tight would be 8).
    uint8_t occPx[24];
    uint8_t mrPx[24];
    std::memset(occPx, 0, sizeof(occPx));
    std::memset(mrPx, 0, sizeof(mrPx));
    occPx[0]  = 10; // (0,0).r
    occPx[4]  = 20; // (1,0).r
    occPx[12] = 30; // (0,1).r
    occPx[16] = 40; // (1,1).r
    mrPx[1]   = 50; // (0,0).g
    mrPx[2]   = 60; // (0,0).b
    mrPx[5]   = 70;
    mrPx[6]   = 80;
    mrPx[13]  = 90;
    mrPx[14]  = 100;
    mrPx[17]  = 110;
    mrPx[18]  = 120;

    Image occ;
    Image mr;
    Image out;
    ASSERT_TRUE(occ.createFromRGBA(occPx, 2, 2, 12));
    ASSERT_TRUE(mr.createFromRGBA(mrPx, 2, 2, 12));
    ASSERT_TRUE(packOrmImage(&occ, &mr, out));
    ASSERT_TRUE(out.valid());
    EXPECT_EQ(out.width(), 2u);
    EXPECT_EQ(out.height(), 2u);
    const uint8_t* px = out.pixels();
    ASSERT_NE(px, nullptr);
    EXPECT_EQ(px[0], 10);
    EXPECT_EQ(px[1], 50);
    EXPECT_EQ(px[2], 60);
    EXPECT_EQ(px[3], 255);
    EXPECT_EQ(px[4], 20);
    EXPECT_EQ(px[5], 70);
    EXPECT_EQ(px[6], 80);
    EXPECT_EQ(px[7], 255);
    const uint32_t pitch = out.rowPitchBytes();
    EXPECT_EQ(px[pitch + 0], 30);
    EXPECT_EQ(px[pitch + 1], 90);
    EXPECT_EQ(px[pitch + 2], 100);
    EXPECT_EQ(px[pitch + 3], 255);
    EXPECT_EQ(px[pitch + 4], 40);
    EXPECT_EQ(px[pitch + 5], 110);
    EXPECT_EQ(px[pitch + 6], 120);
    EXPECT_EQ(px[pitch + 7], 255);
}

TEST(OrmPack, SizeMismatchNearestUsesMrResolution)
{
    const uint8_t occPx[] = { 10, 0, 0, 255, 20, 0, 0, 255, 30, 0, 0, 255, 40, 0, 0, 255 };
    const uint8_t mrPx[]  = { 0, 64, 192, 255 };
    Image         occ;
    Image         mr;
    Image         out;
    ASSERT_TRUE(occ.createFromRGBA(occPx, 2, 2, 8));
    ASSERT_TRUE(mr.createFromRGBA(mrPx, 1, 1, 4));
    ASSERT_TRUE(packOrmImage(&occ, &mr, out));
    ASSERT_TRUE(out.valid());
    EXPECT_EQ(out.width(), 1u);
    EXPECT_EQ(out.height(), 1u);
    const uint8_t* px = out.pixels();
    ASSERT_NE(px, nullptr);
    EXPECT_EQ(px[0], 10);
    EXPECT_EQ(px[1], 64);
    EXPECT_EQ(px[2], 192);
    EXPECT_EQ(px[3], 255);

    Image occ1;
    Image mr2;
    Image up;
    const uint8_t occ1Px[] = { 32, 0, 0, 255 };
    const uint8_t mr2Px[]  = { 0, 1, 2, 255, 0, 3, 4, 255, 0, 5, 6, 255, 0, 7, 8, 255 };
    ASSERT_TRUE(occ1.createFromRGBA(occ1Px, 1, 1, 4));
    ASSERT_TRUE(mr2.createFromRGBA(mr2Px, 2, 2, 8));
    ASSERT_TRUE(packOrmImage(&occ1, &mr2, up));
    ASSERT_TRUE(up.valid());
    EXPECT_EQ(up.width(), 2u);
    EXPECT_EQ(up.height(), 2u);
    const uint8_t* upx = up.pixels();
    ASSERT_NE(upx, nullptr);
    EXPECT_EQ(upx[0], 32);
    EXPECT_EQ(upx[1], 1);
    EXPECT_EQ(upx[2], 2);
    EXPECT_EQ(upx[3], 255);
    EXPECT_EQ(upx[4], 32);
    EXPECT_EQ(upx[5], 3);
    EXPECT_EQ(upx[6], 4);
    EXPECT_EQ(upx[7], 255);
    EXPECT_EQ(upx[8], 32);
    EXPECT_EQ(upx[9], 5);
    EXPECT_EQ(upx[10], 6);
    EXPECT_EQ(upx[11], 255);
    EXPECT_EQ(upx[12], 32);
    EXPECT_EQ(upx[13], 7);
    EXPECT_EQ(upx[14], 8);
    EXPECT_EQ(upx[15], 255);
}

TEST(OrmPack, BothNullReturnsFalse)
{
    Image out;
    EXPECT_FALSE(packOrmImage(nullptr, nullptr, out));
    EXPECT_FALSE(out.valid());
}

TEST(OrmPack, OnlyOcclusion)
{
    const uint8_t occPx[] = { 32, 9, 8, 7 };
    Image         occ;
    Image         out;
    ASSERT_TRUE(occ.createFromRGBA(occPx, 1, 1, 4));
    ASSERT_TRUE(packOrmImage(&occ, nullptr, out));
    const uint8_t* px = out.pixels();
    ASSERT_NE(px, nullptr);
    EXPECT_EQ(px[0], 32);
    EXPECT_EQ(px[1], 255);
    EXPECT_EQ(px[2], 255);
    EXPECT_EQ(px[3], 255);
    EXPECT_EQ(out.colorSpace(), ColorSpace::Linear);
    EXPECT_FALSE(out.colorSpaceWasDefaulted());
}

TEST(OrmPack, OnlyMetallicRoughness)
{
    const uint8_t mrPx[] = { 9, 64, 192, 7 };
    Image         mr;
    Image         out;
    ASSERT_TRUE(mr.createFromRGBA(mrPx, 1, 1, 4));
    ASSERT_TRUE(packOrmImage(nullptr, &mr, out));
    const uint8_t* px = out.pixels();
    ASSERT_NE(px, nullptr);
    EXPECT_EQ(px[0], 255);
    EXPECT_EQ(px[1], 64);
    EXPECT_EQ(px[2], 192);
    EXPECT_EQ(px[3], 255);
}

TEST(OrmPack, RejectsEmptyAndNullPixels)
{
    Image empty;
    Image rgba;
    Image out;
    ASSERT_TRUE(rgba.createSolidColor(1, 2, 3, 4));
    EXPECT_FALSE(empty.valid());
    EXPECT_EQ(empty.pixels(), nullptr);
    EXPECT_FALSE(packOrmImage(&empty, &rgba, out));
    EXPECT_FALSE(packOrmImage(&rgba, &empty, out));
    EXPECT_FALSE(packOrmImage(&empty, nullptr, out));
    EXPECT_FALSE(packOrmImage(nullptr, &empty, out));
}
