#include <gtest/gtest.h>

#include "Character/ThrowableView.h"
#include "Core/ContentRoots.h"
#include "Gameplay/ThrowableCatalog.h"

#include <filesystem>
#include <string>
#include <string_view>

using namespace Dark;

namespace
{

    bool contentFile(std::string_view virtualPath)
    {
        namespace fs = std::filesystem;
        const fs::path virtualFile{ std::string(virtualPath) };
        for (const fs::path& root : contentRootCandidates())
        {
            std::error_code ec;
            if (fs::is_regular_file(root / virtualFile, ec) && !ec)
                return true;
        }
        return false;
    }

} // namespace

TEST(ThrowableView, SelectedHandUsesCatalogModelOnRightHand)
{
    const ThrowableDef* rock = throwableCatalog().find("rock");
    ASSERT_NE(rock, nullptr);
    const ThrowableHandChoice hand = throwableHandChoice(WeaponKind::Throwable, "rock");
    EXPECT_TRUE(hand.shown);
    EXPECT_STREQ(hand.jointName, "R_Hand");
    EXPECT_EQ(hand.modelPath, rock->model);
    EXPECT_EQ(hand.modelPath, "models/NaturePack/glTF/Rock_Medium_1.gltf");
    EXPECT_NE(hand.modelPath, "models/unit_cube.gltf");
}

TEST(ThrowableView, PickupAndFlightUseTheHandModel)
{
    const ThrowableDef* rock = throwableCatalog().find("rock");
    ASSERT_NE(rock, nullptr);
    const ThrowableHandChoice hand = throwableHandChoice(WeaponKind::Throwable, "rock");
    const std::string         pickup = throwableVisualModelPath("rock");
    const std::string         flight = throwableVisualModelPath("rock");
    EXPECT_EQ(pickup, hand.modelPath);
    EXPECT_EQ(flight, hand.modelPath);
    EXPECT_EQ(pickup, rock->model);
    EXPECT_NE(pickup, "models/unit_cube.gltf");
    EXPECT_NE(flight, "models/unit_cube.gltf");

    const ThrowableDef* bottle = throwableCatalog().find("bottle");
    ASSERT_NE(bottle, nullptr);
    const std::string bottlePath = throwableVisualModelPath("bottle");
    EXPECT_EQ(bottlePath, bottle->model);
    EXPECT_EQ(bottlePath, "models/unit_glass.gltf");
    EXPECT_NE(bottlePath, "models/unit_cube.gltf");
    EXPECT_EQ(throwableHandChoice(WeaponKind::Throwable, "bottle").modelPath, bottlePath);
}

TEST(ThrowableView, SwordAndRifleHideTheHandModel)
{
    const ThrowableHandChoice sword = throwableHandChoice(WeaponKind::Melee, "rock");
    const ThrowableHandChoice rifle = throwableHandChoice(WeaponKind::Projectile, "bottle");
    EXPECT_FALSE(sword.shown);
    EXPECT_FALSE(rifle.shown);
    EXPECT_TRUE(throwableHandChoice(WeaponKind::Throwable, "rock").shown);
}

TEST(ThrowableView, MissingModelSkipsMeshAndAllowsThrow)
{
    const ThrowableMeshChoice empty = throwableMeshChoice("");
    EXPECT_FALSE(empty.attachMesh);
    EXPECT_TRUE(empty.allowThrow);

    const ThrowableMeshChoice unknown = throwableMeshChoice("models/not_a_throwable.gltf");
    EXPECT_FALSE(unknown.attachMesh);
    EXPECT_TRUE(unknown.allowThrow);

    const ThrowableMeshChoice cube = throwableMeshChoice("models/unit_cube.gltf");
    EXPECT_FALSE(cube.attachMesh);
    EXPECT_TRUE(cube.allowThrow);

    const std::string rock = throwableVisualModelPath("rock");
    ASSERT_FALSE(rock.empty());
    if (!contentFile(rock))
        GTEST_SKIP() << "catalog rock model is not in content";
    const ThrowableMeshChoice present = throwableMeshChoice(rock);
    EXPECT_TRUE(present.attachMesh);
    EXPECT_TRUE(present.allowThrow);
}

TEST(ThrowableView, EditorPlayUsesTheSameHandChoice)
{
    const ThrowableHandChoice editor  = throwableHandChoice(WeaponKind::Throwable, "bottle");
    const ThrowableHandChoice sandbox = throwableHandChoice(WeaponKind::Throwable, "bottle");
    EXPECT_TRUE(editor.shown);
    EXPECT_EQ(editor.shown, sandbox.shown);
    EXPECT_EQ(editor.modelPath, sandbox.modelPath);
    EXPECT_STREQ(editor.jointName, sandbox.jointName);
    EXPECT_STREQ(editor.jointName, "R_Hand");
    EXPECT_EQ(editor.modelPath, throwableVisualModelPath("bottle"));
    EXPECT_NE(editor.modelPath, "models/unit_cube.gltf");
    EXPECT_FALSE(throwableHandChoice(WeaponKind::Melee, "bottle").shown);
    EXPECT_FALSE(throwableHandChoice(WeaponKind::Projectile, "bottle").shown);
}
