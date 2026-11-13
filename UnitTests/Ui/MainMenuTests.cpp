#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
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

TEST(MainMenu, DpiScaleOneKeepsMouseHit)
{
    MainMenu menu;
    menu.addScene("a.json", "A", "a.json");
    menu.addScene("b.json", "B", "b.json");
    menu.addQuit();
    menu.show();
    menu.setDpiScale(1.0f);

    Input in;
    in.beginFrame();
    in.onMouseMove(640, 370);
    in.onMouseButton(MouseButton::Left, true);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.pollResult(), MainMenuResult::Confirm);
    ASSERT_NE(menu.selected(), nullptr);
    EXPECT_EQ(menu.selected()->id, "b.json");
}

TEST(MainMenu, DpiScaleEnlargesRows)
{
    MainMenu menu;
    menu.addScene("a.json", "A", "a.json");
    menu.addScene("b.json", "B", "b.json");
    menu.addQuit();
    menu.show();

    Input miss;
    miss.beginFrame();
    miss.onMouseMove(1000, 380);
    miss.onMouseButton(MouseButton::Left, true);
    menu.update(miss, 1280, 720);
    EXPECT_EQ(menu.pollResult(), MainMenuResult::None);
    ASSERT_NE(menu.selected(), nullptr);
    EXPECT_EQ(menu.selected()->id, "a.json");

    menu.setDpiScale(2.0f);
    Input hit;
    hit.beginFrame();
    hit.onMouseMove(1000, 380);
    hit.onMouseButton(MouseButton::Left, true);
    menu.update(hit, 1280, 720);
    EXPECT_EQ(menu.pollResult(), MainMenuResult::Confirm);
    ASSERT_NE(menu.selected(), nullptr);
    EXPECT_EQ(menu.selected()->id, "b.json");
}

void addThreeRows(MainMenu& menu)
{
    menu.addScene("a.json", "A", "a.json");
    menu.addScene("b.json", "B", "b.json");
    menu.addQuit();
    menu.show();
}

bool bindMenuSounds(MainMenu& menu, Audio::AudioSystem& audio, AssetManager& assets)
{
    if (assets.mountContentRoots() <= 0)
        return false;
    return menu.loadSounds(audio, assets) && menu.hasSounds();
}

} // namespace

TEST(MainMenu, LoadsUiSounds)
{
    AssetManager assets;
    ASSERT_GT(assets.mountContentRoots(), 0);
    const auto move   = assets.loadAudio("audio/UI/ui_move.wav");
    const auto select = assets.loadAudio("audio/UI/ui_select.wav");
    ASSERT_TRUE(move);
    ASSERT_TRUE(select);
    EXPECT_EQ(move->channels(), 1);
    EXPECT_EQ(select->channels(), 1);
    EXPECT_EQ(move->sampleRate(), 44100u);
    EXPECT_EQ(select->sampleRate(), 44100u);
    EXPECT_EQ(move->frameCount(), 1764u);
    EXPECT_EQ(select->frameCount(), 3087u);

    Audio::AudioSystem audio;
    MainMenu           menu;
    ASSERT_TRUE(menu.loadSounds(audio, assets));
    EXPECT_TRUE(menu.hasSounds());
}

TEST(MainMenu, MoveAndSelectSounds)
{
    AssetManager       assets;
    Audio::AudioSystem audio;
    MainMenu           menu;
    addThreeRows(menu);
    ASSERT_TRUE(bindMenuSounds(menu, audio, assets));

    Input in;
    pressKey(in, Key::Down);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.selectedIndex(), 1u);
    EXPECT_EQ(menu.moveSoundCount(), 1u);
    EXPECT_EQ(menu.selectSoundCount(), 0u);

    in.beginFrame();
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.selectedIndex(), 1u);
    EXPECT_EQ(menu.moveSoundCount(), 1u);

    pressKey(in, Key::S);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.selectedIndex(), 2u);
    EXPECT_EQ(menu.moveSoundCount(), 2u);

    pressKey(in, Key::Up);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.selectedIndex(), 1u);
    EXPECT_EQ(menu.moveSoundCount(), 3u);

    pressKey(in, Key::W);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.selectedIndex(), 0u);
    EXPECT_EQ(menu.moveSoundCount(), 4u);

    pressKey(in, Key::Up);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.selectedIndex(), 2u);
    EXPECT_EQ(menu.moveSoundCount(), 5u);

    // Row centers at 1280x720 with three rows: row 0 is y=304, row 1 is y=370.
    in.beginFrame();
    in.onMouseMove(640, 304);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.selectedIndex(), 0u);
    EXPECT_EQ(menu.moveSoundCount(), 6u);

    in.beginFrame();
    in.onMouseMove(640, 370);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.selectedIndex(), 1u);
    EXPECT_EQ(menu.moveSoundCount(), 7u);

    in.beginFrame();
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.selectedIndex(), 1u);
    EXPECT_EQ(menu.moveSoundCount(), 7u);
    EXPECT_EQ(menu.selectSoundCount(), 0u);

    in.beginFrame();
    in.onMouseMove(640, 304);
    in.onMouseButton(MouseButton::Left, true);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.pollResult(), MainMenuResult::Confirm);
    EXPECT_EQ(menu.selectedIndex(), 0u);
    EXPECT_EQ(menu.moveSoundCount(), 8u);
    EXPECT_EQ(menu.selectSoundCount(), 1u);

    pressKey(in, Key::Enter);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.pollResult(), MainMenuResult::Confirm);
    EXPECT_EQ(menu.moveSoundCount(), 8u);
    EXPECT_EQ(menu.selectSoundCount(), 2u);

    pressKey(in, Key::Space);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.pollResult(), MainMenuResult::Confirm);
    EXPECT_EQ(menu.selectSoundCount(), 3u);

    pressKey(in, Key::Escape);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.pollResult(), MainMenuResult::Quit);
    EXPECT_EQ(menu.moveSoundCount(), 8u);
    EXPECT_EQ(menu.selectSoundCount(), 3u);
}

TEST(MainMenu, SingleRowWrapIsSilent)
{
    AssetManager       assets;
    Audio::AudioSystem audio;
    MainMenu           menu;
    menu.addQuit();
    menu.show();
    ASSERT_TRUE(bindMenuSounds(menu, audio, assets));

    Input in;
    pressKey(in, Key::Down);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.selectedIndex(), 0u);
    EXPECT_EQ(menu.moveSoundCount(), 0u);

    pressKey(in, Key::Up);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.moveSoundCount(), 0u);
}

TEST(MainMenu, HiddenMenuPlaysNothing)
{
    AssetManager       assets;
    Audio::AudioSystem audio;
    MainMenu           menu;
    addThreeRows(menu);
    ASSERT_TRUE(bindMenuSounds(menu, audio, assets));
    menu.hide();

    Input in;
    pressKey(in, Key::Down);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.selectedIndex(), 0u);
    EXPECT_EQ(menu.moveSoundCount(), 0u);

    pressKey(in, Key::Enter);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.pollResult(), MainMenuResult::None);
    EXPECT_EQ(menu.selectSoundCount(), 0u);
}

TEST(MainMenu, NavigationWithoutSounds)
{
    MainMenu menu;
    addThreeRows(menu);
    EXPECT_FALSE(menu.hasSounds());

    Input in;
    pressKey(in, Key::Down);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.selectedIndex(), 1u);
    EXPECT_EQ(menu.moveSoundCount(), 0u);

    pressKey(in, Key::Enter);
    menu.update(in, 1280, 720);
    EXPECT_EQ(menu.pollResult(), MainMenuResult::Confirm);
    EXPECT_EQ(menu.selectSoundCount(), 0u);
    ASSERT_NE(menu.selected(), nullptr);
    EXPECT_EQ(menu.selected()->id, "b.json");
}
