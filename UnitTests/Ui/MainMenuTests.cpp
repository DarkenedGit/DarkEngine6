#include <gtest/gtest.h>

#include "Input/Input.h"
#include "Input/InputCodes.h"
#include "Ui/MainMenu.h"

using namespace Dark;

namespace
{

void pressKey(Input& in, Key key)
{
    in.beginFrame();
    in.onKeyUp(static_cast<uint16_t>(key));
    in.onKeyDown(static_cast<uint16_t>(key), false);
}

} // namespace

TEST(MainMenu, ConfirmSceneAndQuit)
{
    MainMenu menu;
    menu.addScene("level.json", "Level", "level.json");
    menu.addBuiltIn("sandbox", "Play", "Default sandbox");
    menu.addQuit();
    menu.show();

    EXPECT_TRUE(menu.visible());
    ASSERT_EQ(menu.entries().size(), 3u);
    EXPECT_EQ(menu.selectedIndex(), 0u);
    ASSERT_NE(menu.selected(), nullptr);
    EXPECT_EQ(menu.selected()->kind, MainMenuKind::Scene);

    Input in;
    pressKey(in, Key::Down);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.selectedIndex(), 1u);
    EXPECT_EQ(menu.pollResult(), MainMenuResult::None);

    pressKey(in, Key::Enter);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.pollResult(), MainMenuResult::Confirm);
    ASSERT_NE(menu.selected(), nullptr);
    EXPECT_EQ(menu.selected()->id, "sandbox");
    EXPECT_EQ(menu.pollResult(), MainMenuResult::None);

    pressKey(in, Key::Down);
    menu.update(in, 1280, 720);
    pressKey(in, Key::Enter);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.pollResult(), MainMenuResult::Quit);
}

TEST(MainMenu, EscapeQuitsAndSelectById)
{
    MainMenu menu;
    menu.addBuiltIn("play", "Play");
    menu.addScene("level2d.json", "Level 2D", "content/scenes/level2d.json", "level2d.json");
    menu.addQuit();
    ASSERT_TRUE(menu.selectById("level2d.json"));
    EXPECT_EQ(menu.selected()->id, "level2d.json");
    EXPECT_FALSE(menu.selectById("missing"));
    menu.show();

    Input in;
    pressKey(in, Key::Escape);
    menu.update(in, 1920, 1080);
    EXPECT_EQ(menu.pollResult(), MainMenuResult::Quit);
}

TEST(MainMenu, HiddenIgnoresInput)
{
    MainMenu menu;
    menu.addQuit();
    Input in;
    pressKey(in, Key::Enter);
    menu.update(in, 800, 600);
    EXPECT_EQ(menu.pollResult(), MainMenuResult::None);
    EXPECT_FALSE(menu.visible());
}

TEST(MainMenu, MouseClickSelectsRow)
{
    MainMenu menu;
    menu.addScene("a.json", "A", "a.json");
    menu.addScene("b.json", "B", "b.json");
    menu.addQuit();
    menu.show();

    Input in;
    in.beginFrame();
    // Layout: 1280x720 panel centered, two 56px rows after 40px title + 18px gap.
    // Second row is below the first; click near the panel center, lower button.
    in.onMouseMove(640, 370);
    in.onMouseButton(MouseButton::Left, true);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.pollResult(), MainMenuResult::Confirm);
    ASSERT_NE(menu.selected(), nullptr);
    EXPECT_EQ(menu.selected()->id, "b.json");
}
