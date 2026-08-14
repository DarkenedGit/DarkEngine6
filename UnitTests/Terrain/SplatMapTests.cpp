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
