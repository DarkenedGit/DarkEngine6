#include <gtest/gtest.h>

#include "Scene/EntityMaster.h"

using namespace Dark;

TEST(EntityMaster, ParseRoundTripFields)
{
    const char* text = R"({
        "version": 1,
        "type": "player",
        "gltf": "models/human.gltf",
        "anim": "models/human.anim.json",
        "physics": "models/human.physics.json",
        "hsm": "ai/player.hsm.json"
    })";
    EntityMaster master;
    master.type = "keep";
    ASSERT_TRUE(parseEntityMaster(text, master));
    EXPECT_EQ(master.type, "player");
    EXPECT_EQ(master.gltf, "models/human.gltf");
    EXPECT_EQ(master.anim, "models/human.anim.json");
    EXPECT_EQ(master.physics, "models/human.physics.json");
    EXPECT_EQ(master.hsm, "ai/player.hsm.json");
    EXPECT_EQ(entityMasterVirtualPath("player"), "entities/player.entity.json");
}

TEST(EntityMaster, BadJsonLeavesMaster)
{
    EntityMaster master;
    master.type = "keep";
    EXPECT_FALSE(parseEntityMaster("{", master));
    EXPECT_EQ(master.type, "keep");
    EXPECT_FALSE(parseEntityMaster("{\"version\":1}", master));
    EXPECT_EQ(master.type, "keep");
}

TEST(EntityMaster, ContentMastersMatchAssets)
{
    const EntityMaster player = loadEntityMasterType("player");
    EXPECT_EQ(player.type, "player");
    EXPECT_EQ(player.gltf, "models/human.gltf");
    EXPECT_EQ(player.anim, "models/human.anim.json");
    EXPECT_EQ(player.hsm, "ai/player.hsm.json");
    EXPECT_FALSE(resolveContentFile(player.gltf).empty());
    EXPECT_FALSE(resolveContentFile(player.anim).empty());
    EXPECT_FALSE(resolveContentFile(player.hsm).empty());

    const EntityMaster cube = loadEntityMasterType("cube");
    EXPECT_EQ(cube.type, "cube");
    EXPECT_TRUE(cube.gltf.empty());
    EXPECT_EQ(cube.physics, "entities/cube.physics.json");

    const char* types[] = { "cube", "sphere", "human", "wolf", "player", "unit_cube", "unit_glass", "wiggle" };
    for (const char* type : types)
    {
        const EntityMaster master = loadEntityMasterType(type);
        EXPECT_EQ(master.type, type) << type;
        if (!master.gltf.empty())
            EXPECT_FALSE(resolveContentFile(master.gltf).empty()) << master.gltf;
        if (!master.anim.empty())
            EXPECT_FALSE(resolveContentFile(master.anim).empty()) << master.anim;
        if (!master.hsm.empty())
            EXPECT_FALSE(resolveContentFile(master.hsm).empty()) << master.hsm;
    }
}
