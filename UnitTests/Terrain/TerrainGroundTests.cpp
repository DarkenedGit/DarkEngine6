#include <gtest/gtest.h>

#include "Terrain/HeightMap.h"
#include "Terrain/SplatMap.h"
#include "Terrain/TerrainGround.h"

using namespace Dark;
using namespace Dark::Terrain;

TEST(TerrainGround, DefaultsSlowRockAndSnow)
{
    TerrainGround ground;
    EXPECT_FLOAT_EQ(ground.layer(0).moveSpeed, 1.0f);
    EXPECT_FLOAT_EQ(ground.layer(1).moveSpeed, 1.0f);
    EXPECT_FLOAT_EQ(ground.layer(2).moveSpeed, 0.75f);
    EXPECT_FLOAT_EQ(ground.layer(3).moveSpeed, 0.75f);
    EXPECT_STREQ(ground.layer(2).cue, "step_rock");
    EXPECT_STREQ(ground.layer(3).cue, "step_snow");
}

TEST(TerrainGround, JsonOverridesSpeedAndFootstep)
{
    const char* text = R"({
        "version": 1,
        "layers": [
            { "id": "snow", "moveSpeed": 0.5, "footstep": "audio/custom_snow.wav", "blipHz": 40 },
            { "id": "rock", "moveSpeed": 0.75 }
        ]
    })";
    TerrainGround ground;
    ASSERT_TRUE(ground.parse(text));
    EXPECT_FLOAT_EQ(ground.layer(3).moveSpeed, 0.5f);
    EXPECT_STREQ(ground.layer(3).footstep, "audio/custom_snow.wav");
    EXPECT_FLOAT_EQ(ground.layer(3).blipHz, 40.0f);
    EXPECT_FLOAT_EQ(ground.layer(2).moveSpeed, 0.75f);
    EXPECT_FLOAT_EQ(ground.layer(1).moveSpeed, 1.0f);
}

TEST(TerrainGround, DominantSplatPicksRock)
{
    HeightMap height;
    ASSERT_TRUE(height.create(2, 2, 1.0f, 1.0f));
    SplatMap splat;
    ASSERT_TRUE(splat.create(2, 2));
    splat.setTexel(0, 0, 0, 0, 255, 0);
    splat.setTexel(1, 0, 0, 0, 255, 0);
    splat.setTexel(0, 1, 0, 0, 255, 0);
    splat.setTexel(1, 1, 0, 0, 255, 0);

    TerrainGround ground;
    const GroundContact contact = ground.at(&height, &splat, 0.0f, 0.0f);
    EXPECT_EQ(contact.layer, 2);
    EXPECT_FLOAT_EQ(contact.moveSpeed, 0.75f);
    EXPECT_STREQ(contact.cue, "step_rock");
}

TEST(TerrainGround, MissingMapIsGrass)
{
    TerrainGround ground;
    const GroundContact contact = ground.at(nullptr, nullptr, 0.0f, 0.0f);
    EXPECT_EQ(contact.layer, 1);
    EXPECT_FLOAT_EQ(contact.moveSpeed, 1.0f);
}

TEST(TerrainGround, LoadsContentFile)
{
    TerrainGround ground;
    ASSERT_TRUE(ground.loadFromContent());
    EXPECT_FLOAT_EQ(ground.layer(2).moveSpeed, 0.75f);
    EXPECT_FLOAT_EQ(ground.layer(3).moveSpeed, 0.75f);
    EXPECT_STREQ(ground.layer(0).footstep, "audio/foot_dirt.wav");
}
