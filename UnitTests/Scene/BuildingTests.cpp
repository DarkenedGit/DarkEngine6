#include "AI/Walkability.h"
#include "Core/ContentRoots.h"
#include "Scene/Building.h"
#include "Terrain/HeightMap.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace Dark;

namespace
{
    constexpr const char* kKit = R"({
        "cell": 4.0, "height": 3.0, "thickness": 0.2,
        "symbols": { "-": "wall", "|": "wall", "D": "door" },
        "pieces": {
            "wall": { "spans": [ [0, 4, 0, 3] ] },
            "door": { "spans": [ [0, 1, 0, 3], [3, 4, 0, 3], [1, 3, 2.4, 3] ] }
        },
        "props": { "locker": { "size": [1, 2, 0.8], "hide": true, "label": "Locker" }, "desk": { "size": [2, 0.8, 1] } }
    })";

    constexpr const char* kHut = R"({
        "id": "hut",
        "layout": [ "+-+", "D.|", "+-+" ],
        "props": [ { "prop": "locker", "pos": [3, 3], "yaw": 0 }, { "prop": "desk", "pos": [2, 1], "loot": [ { "item": "medkit" } ] } ],
        "pickups": [ { "item": "bio_sample", "pos": [2, 2] } ],
        "guards": [ { "pos": [2, 2], "patrol": [ [1, 1], [3, 3] ] } ]
    })";

    int countBlocking(const BuildingExpansion& out)
    {
        int n = 0;
        for (const SceneObjectData& o : out.objects)
            n += (o.type == SceneObjectType::Cube && o.block && o.pickup.empty() && o.scale.y >= 0.3f) ? 1 : 0;
        return n;
    }
} // namespace

TEST(Building, ExpandsLayoutPropsPickupsAndGuards)
{
    BuildingKit kit;
    BuildingDef hut;
    ASSERT_TRUE(parseBuildingKit(kKit, kit));
    ASSERT_TRUE(parseBuilding(kHut, hut));
    BuildingExpansion out;
    ASSERT_TRUE(expandBuilding(hut, kit, SceneBuildingDesc{ "hut", { 10.0f, 0.0f, 20.0f }, 0.0f }, out));
    EXPECT_EQ(countBlocking(out), 4 + 3 + 2 + 2);
    ASSERT_EQ(out.hides.size(), 1u);
    ASSERT_EQ(out.containers.size(), 1u);
    int guards = 0, pickups = 0;
    for (const SceneObjectData& o : out.objects)
    {
        if (o.type == SceneObjectType::Hunter)
        {
            ++guards;
            EXPECT_EQ(o.squad, "building:hut");
            EXPECT_NEAR(o.position.x, 12.0f, 1.0e-4f);
            ASSERT_EQ(o.patrol.size(), 2u);
            EXPECT_NEAR(o.patrol[1].y, 23.0f, 1.0e-4f);
        }
        pickups += o.pickup == "bio_sample" ? 1 : 0;
    }
    EXPECT_EQ(guards, 1);
    EXPECT_EQ(pickups, 1);
}

TEST(Building, DoorLeavesAGapAndLintelDoesNotBlock)
{
    BuildingKit kit;
    BuildingDef hut;
    ASSERT_TRUE(parseBuildingKit(kKit, kit));
    ASSERT_TRUE(parseBuilding(kHut, hut));
    BuildingExpansion out;
    ASSERT_TRUE(expandBuilding(hut, kit, SceneBuildingDesc{ "hut", {}, 0.0f }, out));
    for (const SceneObjectData& o : out.objects)
    {
        const bool atDoor = std::fabs(o.position.x) < 0.2f && std::fabs(o.position.z - 2.0f) < 0.5f;
        if (atDoor && o.block)
            ADD_FAILURE() << "a blocking box sits in the doorway";
    }
}

TEST(Building, QuarterTurnSwapsTheFootprint)
{
    BuildingKit kit;
    BuildingDef hut;
    ASSERT_TRUE(parseBuildingKit(kKit, kit));
    ASSERT_TRUE(parseBuilding(kHut, hut));
    BuildingExpansion flat, turned;
    ASSERT_TRUE(expandBuilding(hut, kit, SceneBuildingDesc{ "hut", {}, 0.0f }, flat));
    ASSERT_TRUE(expandBuilding(hut, kit, SceneBuildingDesc{ "hut", {}, 90.0f }, turned));
    ASSERT_EQ(flat.objects.size(), turned.objects.size());
    EXPECT_NEAR(flat.objects[1].scale.x, turned.objects[1].scale.z, 1.0e-4f);
    EXPECT_NEAR(flat.objects[1].scale.z, turned.objects[1].scale.x, 1.0e-4f);
}

TEST(Building, RejectsBadLayout)
{
    BuildingDef def;
    std::string error;
    EXPECT_FALSE(parseBuilding(R"({ "id": "x", "layout": [ "+-+", "|.|" ] })", def, &error));
    EXPECT_FALSE(parseBuilding(R"({ "id": "x", "layout": [ "+-+", "|.", "+-+" ] })", def, &error));
    EXPECT_FALSE(parseBuilding(R"({ "id": 3, "layout": [ "+-+", "|.|", "+-+" ] })", def, &error));
    EXPECT_FALSE(error.empty());
}

TEST(Building, ShippedLabGuardsCanWalkIntoEveryRoom)
{
    BuildingKit kit;
    BuildingDef lab;
    bool        loaded = false;
    for (const std::filesystem::path& root : contentRootCandidates())
    {
        if (loadBuildingKit(root / buildingKitVirtualPath("facility"), kit) && loadBuilding(root / buildingVirtualPath("lab_small"), lab))
        {
            loaded = true;
            break;
        }
    }
    ASSERT_TRUE(loaded);
    BuildingExpansion out;
    ASSERT_TRUE(expandBuilding(lab, kit, SceneBuildingDesc{ "lab_small", { 4.0f, 0.0f, 4.0f }, 0.0f }, out));
    EXPECT_EQ(out.hides.size(), 2u);
    EXPECT_EQ(out.containers.size(), 2u);
    ASSERT_EQ(out.doors.size(), 2u);
    int locked = 0;
    for (const BuildingDoor& d : out.doors)
        locked += d.lock == "keycard_red" ? 1 : 0;
    EXPECT_EQ(locked, 1);
    ASSERT_EQ(out.cameras.size(), 1u);
    EXPECT_EQ(out.cameras[0].circuit, "lab_cams");
    EXPECT_NEAR(out.cameras[0].yaw, -90.0f, 0.5f);
    int powerBoxes = 0;
    for (const SceneObjectData& o : out.objects)
        powerBoxes += o.circuit == "lab_cams" ? 1 : 0;
    EXPECT_EQ(powerBoxes, 1);

    std::vector<Math::AABox3f> cubes;
    for (const SceneObjectData& o : out.objects)
    {
        if (o.type == SceneObjectType::Cube && o.block && o.pickup.empty() && o.scale.y >= 0.3f)
            cubes.push_back(Math::AABox3f::FromCenterExtents(o.position, o.scale * 0.5f));
    }
    Terrain::HeightMap hm;
    ASSERT_TRUE(hm.create(25u, 25u, 1.0f, 1.0f));
    hm.setOrigin(Math::Vector3f{ 0.0f, 0.0f, 0.0f });
    for (int z = 0; z < 25; ++z)
        for (int x = 0; x < 25; ++x)
            hm.setHeight(x, z, 0.0f);
    AI::Walkability     walk;
    AI::WalkabilityDesc desc;
    desc.heightMap   = &hm;
    desc.waterLevel  = -10.0f;
    desc.agentRadius = 0.8f;
    desc.cubes       = cubes.data();
    desc.cubeCount   = static_cast<int>(cubes.size());
    ASSERT_TRUE(walk.bake(desc));
    const int outside = walk.islandWorld(1.5f, 6.5f);
    ASSERT_GT(outside, 0);
    EXPECT_EQ(walk.islandWorld(4.0f + 3.5f, 4.0f + 6.5f), outside) << "main lab";
    EXPECT_EQ(walk.islandWorld(4.0f + 8.5f, 4.0f + 3.5f), outside) << "storage room";
    EXPECT_EQ(walk.islandWorld(4.0f + 8.5f, 4.0f + 8.5f), outside) << "sample room";
}

TEST(Building, ShippedWarehouseGuardsReachEveryArea)
{
    BuildingKit kit;
    BuildingDef wing;
    bool        loaded = false;
    for (const std::filesystem::path& root : contentRootCandidates())
    {
        if (loadBuildingKit(root / buildingKitVirtualPath("facility"), kit) && loadBuilding(root / buildingVirtualPath("warehouse"), wing))
        {
            loaded = true;
            break;
        }
    }
    ASSERT_TRUE(loaded);
    BuildingExpansion out;
    ASSERT_TRUE(expandBuilding(wing, kit, SceneBuildingDesc{ "warehouse", { 4.0f, 0.0f, 4.0f }, 0.0f }, out));
    std::vector<Math::AABox3f> cubes;
    for (const SceneObjectData& o : out.objects)
    {
        if (o.type == SceneObjectType::Cube && o.block && o.pickup.empty() && o.scale.y >= 0.3f)
            cubes.push_back(Math::AABox3f::FromCenterExtents(o.position, o.scale * 0.5f));
    }
    Terrain::HeightMap hm;
    ASSERT_TRUE(hm.create(31u, 25u, 1.0f, 1.0f));
    hm.setOrigin(Math::Vector3f{ 0.0f, 0.0f, 0.0f });
    for (int z = 0; z < 25; ++z)
        for (int x = 0; x < 31; ++x)
            hm.setHeight(x, z, 0.0f);
    AI::Walkability     walk;
    AI::WalkabilityDesc desc;
    desc.heightMap   = &hm;
    desc.waterLevel  = -10.0f;
    desc.agentRadius = 0.8f;
    desc.cubes       = cubes.data();
    desc.cubeCount   = static_cast<int>(cubes.size());
    ASSERT_TRUE(walk.bake(desc));
    const int outside = walk.islandWorld(1.5f, 7.5f);
    ASSERT_GT(outside, 0);
    EXPECT_EQ(walk.islandWorld(4.0f + 6.5f, 4.0f + 6.5f), outside) << "hall";
    EXPECT_EQ(walk.islandWorld(4.0f + 14.5f, 4.0f + 3.5f), outside) << "behind the low wall";
    EXPECT_EQ(walk.islandWorld(4.0f + 14.5f, 4.0f + 8.5f), outside) << "server room";
    EXPECT_EQ(walk.islandWorld(4.0f + 9.5f, 4.0f + 18.0f), outside) << "outside the loading bay";
    for (const BuildingGuardDef& g : wing.guards)
        for (const Math::Vector2f& p : g.patrol)
            EXPECT_EQ(walk.islandWorld(4.0f + p.x, 4.0f + p.y), outside) << "patrol point " << p.x << "," << p.y;
}
