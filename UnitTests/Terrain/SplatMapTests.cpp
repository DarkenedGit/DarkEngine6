#include <gtest/gtest.h>

#include "Terrain/HeightMap.h"
#include "Terrain/SplatMap.h"

using namespace Dark::Terrain;

TEST(SplatMap, SetAndSample)
{
    SplatMap splat;
    ASSERT_TRUE(splat.create(2, 2));
    splat.setTexel(0, 0, 255, 0, 0, 0);
    splat.setTexel(1, 0, 0, 255, 0, 0);
    splat.setTexel(0, 1, 0, 0, 255, 0);
    splat.setTexel(1, 1, 0, 0, 0, 255);

    float w[kMaxTerrainLayers]{};
    splat.sampleWeights(0.0f, 0.0f, w);
    EXPECT_NEAR(w[0], 1.0f, 1.0e-4f);
    EXPECT_NEAR(w[1], 0.0f, 1.0e-4f);

    splat.sampleWeights(0.5f, 0.0f, w);
    EXPECT_NEAR(w[0], 0.5f, 1.0e-4f);
    EXPECT_NEAR(w[1], 0.5f, 1.0e-4f);
}

TEST(SplatMap, GenerateFromHeightHasFourLayers)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(17, 17, 1.0f, 1.0f));
    for (int z = 0; z < 17; ++z)
    {
        for (int x = 0; x < 17; ++x)
            hm.setHeight(x, z, static_cast<float>(z) + (x > 12 ? static_cast<float>(x) : 0.0f));
    }

    SplatMap splat;
    ASSERT_TRUE(splat.generateFromHeight(hm));
    EXPECT_EQ(splat.width(), 17u);
    EXPECT_EQ(splat.height(), 17u);

    bool used[kMaxTerrainLayers]{ false, false, false, false };
    for (int z = 0; z < 17; ++z)
    {
        for (int x = 0; x < 17; ++x)
        {
            uint8_t c[4]{};
            splat.getTexel(x, z, c);
            const int sum = c[0] + c[1] + c[2] + c[3];
            EXPECT_GT(sum, 0);
            for (int i = 0; i < kMaxTerrainLayers; ++i)
            {
                if (c[i] > 8)
                    used[i] = true;
            }
        }
    }
    // Height + slope paint should touch more than one layer.
    const int layersUsed = (used[0] ? 1 : 0) + (used[1] ? 1 : 0) + (used[2] ? 1 : 0) + (used[3] ? 1 : 0);
    EXPECT_GE(layersUsed, 2);
}

TEST(SplatMap, GenerateFromHeight_MatchesHeightMap)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(17, 17, 1.0f, 1.0f));
    for (int z = 0; z < 17; ++z)
    {
        for (int x = 0; x < 17; ++x)
            hm.setHeight(x, z, static_cast<float>(z));
    }

    SplatMap splat;
    ASSERT_TRUE(splat.generateFromHeight(hm));
    EXPECT_EQ(splat.width(), hm.width());
    EXPECT_EQ(splat.height(), hm.height());

    splat.paintTexel(-4, -4, 2, 1.0f);
    uint8_t rim[4]{};
    splat.getTexel(0, 0, rim);
    EXPECT_EQ(static_cast<int>(rim[0]) + rim[1] + rim[2] + rim[3], 255);
}

TEST(SplatMap, Create_RejectsOversize)
{
    SplatMap splat;
    EXPECT_FALSE(splat.create(2048, 2048));
    EXPECT_FALSE(splat.valid());
    EXPECT_EQ(splat.width(), 0u);
    EXPECT_FALSE(splat.create(1026, 2));
    EXPECT_TRUE(splat.create(kMaxSplatMapSize, 2));
    EXPECT_EQ(splat.width(), kMaxSplatMapSize);
}

TEST(SplatMap, Paint_Renormalize)
{
    SplatMap splat;
    ASSERT_TRUE(splat.create(2, 2));
    splat.setTexel(0, 0, 255, 0, 0, 0);
    splat.paintTexel(0, 0, 2, 1.0f);

    uint8_t c[4]{};
    splat.getTexel(0, 0, c);
    EXPECT_EQ(static_cast<int>(c[0]) + c[1] + c[2] + c[3], 255);
    EXPECT_GT(c[2], 0);
    EXPECT_LT(c[0], 255);
    EXPECT_EQ(c[1], 0);
    EXPECT_EQ(c[3], 0);
}

TEST(SplatMap, Paint_EmptyTexel_Grass)
{
    SplatMap splat;
    ASSERT_TRUE(splat.create(2, 2));
    splat.setTexel(1, 1, 0, 0, 0, 0);
    splat.paintTexel(1, 1, 0, 0.0f);

    uint8_t c[4]{};
    splat.getTexel(1, 1, c);
    EXPECT_EQ(c[0], 0);
    EXPECT_EQ(c[1], 255);
    EXPECT_EQ(c[2], 0);
    EXPECT_EQ(c[3], 0);
    EXPECT_EQ(static_cast<int>(c[0]) + c[1] + c[2] + c[3], 255);
}

TEST(SplatMap, Paint_InvalidLayerAndMap)
{
    SplatMap splat;
    EXPECT_FALSE(splat.renormalizeTexel(0, 0));
    splat.paintTexel(0, 0, 1, 1.0f);
    splat.paintDisk(0.0f, 0.0f, 2.0f, 1, 1.0f);

    ASSERT_TRUE(splat.create(4, 4));
    splat.setTexel(0, 0, 255, 0, 0, 0);
    splat.paintTexel(0, 0, -1, 1.0f);
    splat.paintTexel(0, 0, kMaxTerrainLayers, 1.0f);
    uint8_t c[4]{};
    splat.getTexel(0, 0, c);
    EXPECT_EQ(c[0], 255);
    EXPECT_EQ(c[1], 0);
}

TEST(SplatMap, PaintDisk_OffMapClamps)
{
    SplatMap splat;
    ASSERT_TRUE(splat.create(4, 4));
    splat.setTexel(0, 0, 255, 0, 0, 0);
    splat.paintDisk(-8.0f, -8.0f, 1.0f, 2, 1.0f);

    uint8_t c[4]{};
    splat.getTexel(0, 0, c);
    EXPECT_EQ(static_cast<int>(c[0]) + c[1] + c[2] + c[3], 255);

    splat.paintDisk(1.5f, 1.5f, 0.0f, 1, 1.0f);
    splat.getTexel(2, 2, c);
    EXPECT_EQ(static_cast<int>(c[0]) + c[1] + c[2] + c[3], 255);
    EXPECT_EQ(c[1], 255);
}
