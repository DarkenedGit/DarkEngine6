#include "Content/ContentDb.h"
#include "Content/ContentId.h"
#include "Gameplay/Item.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

using namespace Dark;

namespace
{

    std::filesystem::path tempRoot(const char* name)
    {
        const auto path = std::filesystem::temp_directory_path() / name;
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
        return path;
    }

    void writeFile(const std::filesystem::path& path, const std::string& text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << text;
    }

} // namespace

TEST(ContentId, HashCollisionRefusesTheSecondName)
{
    std::unordered_map<uint64_t, std::string> taken;
    ContentError err;
    EXPECT_TRUE(claimContentHash(taken, 11, "core:alpha", err));
    EXPECT_TRUE(claimContentHash(taken, 11, "core:alpha", err));
    EXPECT_FALSE(claimContentHash(taken, 11, "core:beta", err));
    EXPECT_NE(err.message.find("collision"), std::string::npos);
}

TEST(ContentDb, OverrideOrderNamespacingBaseAndBadComponent)
{
    const auto first  = tempRoot("darkengine6_content_a");
    const auto second = tempRoot("darkengine6_content_b");
    writeFile(first / "package.json", R"({ "id": "core", "version": "1.0.0", "engine": ">=6.0" })");
    writeFile(second / "package.json", R"({ "id": "better", "version": "1.0.0", "engine": ">=6.0", "loadAfter": ["core"] })");
    writeFile(first / "archetypes" / "wolf.archetype.json", R"({
        "id": "core:wolf",
        "components": { "Health": { "max": 10 } },
        "behaviors": [ { "name": "ai.wolf" } ]
    })");
    writeFile(second / "archetypes" / "wolf.archetype.json", R"({
        "id": "core:wolf",
        "components": { "Health": { "max": 60 } },
        "behaviors": [ { "name": "ai.wolf" } ]
    })");
    writeFile(second / "archetypes" / "named.archetype.json", R"({
        "id": "better:howler",
        "base": "core:wolf",
        "components": { "Health": { "max": 80 } },
        "behaviors": []
    })");
    writeFile(second / "archetypes" / "orphan.archetype.json", R"({
        "id": "better:orphan",
        "base": "core:missing",
        "components": {},
        "behaviors": []
    })");
    writeFile(second / "archetypes" / "loop_a.archetype.json", R"({
        "id": "better:loop_a",
        "base": "better:loop_b",
        "components": {},
        "behaviors": []
    })");
    writeFile(second / "archetypes" / "loop_b.archetype.json", R"({
        "id": "better:loop_b",
        "base": "better:loop_a",
        "components": {},
        "behaviors": []
    })");
    writeFile(second / "archetypes" / "bad.archetype.json", R"({
        "id": "better:bad",
        "components": { "NotAComponent": {} },
        "behaviors": []
    })");

    ContentError err;
    ASSERT_TRUE(loadContentPackagesForTest({ first, second }, &err));
    const Archetype* wolf = findArchetype("wolf");
    ASSERT_NE(wolf, nullptr);
    ASSERT_EQ(wolf->components.size(), 1u);
    EXPECT_NE(wolf->components[0].paramsJson.find("60"), std::string::npos);
    EXPECT_EQ(wolf->packageId, "better");

    const Archetype* named = findArchetype("better:howler");
    ASSERT_NE(named, nullptr);
    EXPECT_TRUE(named->baseName.empty());
    ASSERT_FALSE(named->components.empty());
    EXPECT_NE(named->components[0].paramsJson.find("80"), std::string::npos);

    EXPECT_EQ(findArchetype("better:orphan"), nullptr);
    EXPECT_EQ(findArchetype("better:loop_a"), nullptr);
    EXPECT_EQ(findArchetype("better:loop_b"), nullptr);
    EXPECT_EQ(findArchetype("better:bad"), nullptr);
    EXPECT_FALSE(contentIssues().empty());

    resetContentDatabase();
    std::error_code ec;
    std::filesystem::remove_all(first, ec);
    std::filesystem::remove_all(second, ec);
}

TEST(ContentDb, ShippedLegacyTypesAreNotSynthesizedAsAnimals)
{
    ContentError err;
    ASSERT_TRUE(loadContentDatabase(&err)) << err.message;
    const Archetype* wolf = findArchetype("core:wolf");
    ASSERT_NE(wolf, nullptr);
    bool wolfBehavior = false;
    bool animal = false;
    for (const ArchetypeBehavior& behavior : wolf->behaviors)
    {
        if (behavior.name == "ai.wolf")
            wolfBehavior = true;
        if (behavior.name == "ai.animal")
            animal = true;
    }
    EXPECT_TRUE(wolfBehavior);
    EXPECT_FALSE(animal);

    const Archetype* player = findArchetype("player");
    ASSERT_NE(player, nullptr);
    bool controller = false;
    for (const ArchetypeBehavior& behavior : player->behaviors)
    {
        if (behavior.name == "player.controller")
            controller = true;
        if (behavior.name == "ai.animal")
            animal = true;
    }
    EXPECT_TRUE(controller);
    EXPECT_FALSE(animal);

    const Archetype* deer = findArchetype("deer");
    ASSERT_NE(deer, nullptr);
    bool deerAnimal = false;
    for (const ArchetypeBehavior& behavior : deer->behaviors)
    {
        if (behavior.name == "ai.animal")
            deerAnimal = true;
    }
    EXPECT_TRUE(deerAnimal);
    resetContentDatabase();
}

TEST(ItemCatalog, JsonMatchesTheCompiledRows)
{
    reloadItemCatalog();
    const ItemDef* sword = findItemDef("sword");
    const ItemDef* med   = findItemDef("medkit");
    const ItemDef* chest = findItemDef("chest");
    const ItemDef* file  = findItemDef("research");
    ASSERT_NE(sword, nullptr);
    ASSERT_NE(med, nullptr);
    ASSERT_NE(chest, nullptr);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(sword->type, ItemType::Weapon);
    EXPECT_EQ(sword->maxStack, 1);
    EXPECT_FLOAT_EQ(med->heal, 50.0f);
    EXPECT_EQ(med->type, ItemType::Consumable);
    EXPECT_EQ(med->maxStack, 5);
    EXPECT_FLOAT_EQ(chest->armor, 40.0f);
    EXPECT_EQ(chest->slot, EquipSlot::Chest);
    EXPECT_EQ(file->maxStack, 99);
    EXPECT_EQ(findItemDef("does_not_exist"), nullptr);

    std::vector<LoadoutGrant> grants;
    ASSERT_TRUE(loadStartingLoadout(grants));
    ASSERT_GE(grants.size(), 3u);
    EXPECT_EQ(grants[0].id, "sword");
    EXPECT_EQ(grants[0].count, 1);
    EXPECT_EQ(grants[2].id, "medkit");
    EXPECT_EQ(grants[2].count, 2);
}

TEST(ContentDb, LaterPackageReplacesAndOmitsAliases)
{
    const auto core = tempRoot("darkengine6_alias_core");
    const auto mod  = tempRoot("darkengine6_alias_mod");
    writeFile(core / "package.json", R"({ "id": "core", "version": "1.0.0", "engine": ">=6.0" })");
    writeFile(mod / "package.json", R"({ "id": "mod", "version": "1.0.0", "engine": ">=6.0" })");
    writeFile(core / "archetypes" / "hunter.archetype.json", R"({
        "id": "core:hunter",
        "components": { "Tag": {} },
        "behaviors": []
    })");
    writeFile(core / "archetypes" / "wolf.archetype.json", R"({
        "id": "core:wolf",
        "components": { "Tag": {} },
        "behaviors": []
    })");
    writeFile(core / "archetypes" / "legacy_aliases.json", R"({
        "aliases": {
            "enemy": "core:hunter",
            "ghost": "core:wolf"
        }
    })");
    writeFile(mod / "archetypes" / "legacy_aliases.json", R"({
        "aliases": {
            "enemy": "core:wolf",
            "ghost": null
        }
    })");

    ContentError err;
    ASSERT_TRUE(loadContentPackagesForTest({ core, mod }, &err)) << err.message;
    const Archetype* enemy = findArchetype("enemy");
    ASSERT_NE(enemy, nullptr);
    EXPECT_EQ(enemy->name, "core:wolf");
    EXPECT_EQ(findArchetype("ghost"), nullptr);
    EXPECT_NE(findArchetype("core:wolf"), nullptr);

    resetContentDatabase();
    std::error_code ec;
    std::filesystem::remove_all(core, ec);
    std::filesystem::remove_all(mod, ec);
}
