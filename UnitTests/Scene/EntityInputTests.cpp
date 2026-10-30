#include <gtest/gtest.h>

#include "Input/ActionMap.h"
#include "Input/Input.h"
#include "Scene/EntityInput.h"

#include <vector>

using namespace Dark;

namespace
{
    bool hasKey(const ActionMap& map, const char* action, Key key, float scale, uint8_t mods = 0)
    {
        const std::vector<ActionMap::Binding>* list = map.bindings(action);
        if (!list)
            return false;
        for (const ActionMap::Binding& b : *list)
        {
            if (b.type == ActionMap::BindingType::Key && b.code == static_cast<uint16_t>(key) && b.scale == scale && b.mods == mods)
                return true;
        }
        return false;
    }

    bool hasMouse(const ActionMap& map, const char* action, MouseButton button)
    {
        const std::vector<ActionMap::Binding>* list = map.bindings(action);
        if (!list)
            return false;
        for (const ActionMap::Binding& b : *list)
        {
            if (b.type == ActionMap::BindingType::MouseButton && b.code == static_cast<uint16_t>(button))
                return true;
        }
        return false;
    }

    bool hasAxisHeld(const ActionMap& map, const char* action, GamepadAxis axis, float threshold)
    {
        const std::vector<ActionMap::Binding>* list = map.bindings(action);
        if (!list)
            return false;
        for (const ActionMap::Binding& b : *list)
        {
            if (b.type == ActionMap::BindingType::GamepadAxis && b.code == static_cast<uint16_t>(axis) && b.threshold == threshold)
                return true;
        }
        return false;
    }

    bool hasButton(const ActionMap& map, const char* action, GamepadButton button, float scale)
    {
        const std::vector<ActionMap::Binding>* list = map.bindings(action);
        if (!list)
            return false;
        for (const ActionMap::Binding& b : *list)
        {
            if (b.type == ActionMap::BindingType::GamepadButton && b.code == static_cast<uint16_t>(button) && b.scale == scale)
                return true;
        }
        return false;
    }

    bool hasAxis(const ActionMap& map, const char* action, GamepadAxis axis, float scale)
    {
        const std::vector<ActionMap::Binding>* list = map.bindings(action);
        if (!list)
            return false;
        for (const ActionMap::Binding& b : *list)
        {
            if (b.type == ActionMap::BindingType::GamepadAxis && b.code == static_cast<uint16_t>(axis) && b.scale == scale && b.threshold == 0.0f)
                return true;
        }
        return false;
    }
}

TEST(EntityInput, BadJsonLeavesBindings)
{
    std::vector<EntityInputBinding> rows(1);
    rows[0].action = "keep";
    EXPECT_FALSE(parseEntityInput("{", rows));
    EXPECT_EQ(rows[0].action, "keep");
    EXPECT_FALSE(parseEntityInput("{\"version\":1,\"type\":\"wolf\",\"bindings\":[]}", rows));
    EXPECT_EQ(rows[0].action, "keep");
    EXPECT_FALSE(parseEntityInput("{\"version\":2,\"type\":\"player\",\"bindings\":[]}", rows));
    EXPECT_EQ(rows[0].action, "keep");
    EXPECT_FALSE(parseEntityInput("{\"version\":1,\"type\":\"sandbox\",\"bindings\":[]}", rows));
    EXPECT_EQ(rows[0].action, "keep");
    EXPECT_EQ(entityInputVirtualPath("player"), "entities/player.entity.input.json");
}

TEST(EntityInput, SandboxTypeIsSeparateFromPlayer)
{
    std::vector<EntityInputBinding> rows(1);
    rows[0].action = "keep";
    const char* sandboxQuit = R"({"version":1,"type":"sandbox","bindings":[{"action":"quit","key":"Escape"}]})";
    EXPECT_FALSE(parseEntityInput(sandboxQuit, rows));
    EXPECT_EQ(rows[0].action, "keep");
    ASSERT_TRUE(parseEntityInput(sandboxQuit, rows, "sandbox"));
    EXPECT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].action, "quit");
    EXPECT_EQ(rows[0].key, "Escape");
    EXPECT_FALSE(parseEntityInput("{\"version\":1,\"type\":\"player\",\"bindings\":[]}", rows, "sandbox"));
    EXPECT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].action, "quit");
}

TEST(EntityInput, ApplySkipsOtherAppsAndUnknownDevices)
{
    const char* text = R"({
        "version": 1,
        "type": "player",
        "bindings": [
            { "action": "jump", "key": "Space" },
            { "action": "jump", "key": "NotAKey" },
            { "action": "move_x", "key": "A", "scale": -1 },
            { "action": "crouch", "key": "LeftControl" },
            { "action": "crouch", "key": "C", "app": "editor" },
            { "action": "shield", "axis": "LeftTrigger", "threshold": 0.45 },
            { "action": "attack", "mouse": "Left" }
        ]
    })";
    std::vector<EntityInputBinding> rows;
    ASSERT_TRUE(parseEntityInput(text, rows));
    EXPECT_EQ(rows.size(), 7u);

    ActionMap sandbox;
    sandbox.bindKey("quit", Key::Escape);
    sandbox.bindKey("jump", Key::F);
    ASSERT_TRUE(applyEntityInput(sandbox, rows, "sandbox"));
    EXPECT_TRUE(hasKey(sandbox, "quit", Key::Escape, 1.0f));
    EXPECT_TRUE(hasKey(sandbox, "jump", Key::Space, 1.0f));
    EXPECT_FALSE(hasKey(sandbox, "jump", Key::F, 1.0f));
    EXPECT_TRUE(hasKey(sandbox, "move_x", Key::A, -1.0f));
    EXPECT_TRUE(hasKey(sandbox, "crouch", Key::LeftControl, 1.0f));
    EXPECT_FALSE(hasKey(sandbox, "crouch", Key::C, 1.0f));
    EXPECT_TRUE(hasAxisHeld(sandbox, "shield", GamepadAxis::LeftTrigger, 0.45f));
    EXPECT_TRUE(hasMouse(sandbox, "attack", MouseButton::Left));

    ActionMap editor;
    ASSERT_TRUE(applyEntityInput(editor, rows, "editor"));
    EXPECT_TRUE(hasKey(editor, "crouch", Key::C, 1.0f));
    EXPECT_TRUE(hasKey(editor, "crouch", Key::LeftControl, 1.0f));
}

TEST(EntityInput, ContentFileFeedsSandboxAndEditor)
{
    ActionMap sandbox;
    sandbox.bindKey("quit", Key::Escape);
    ASSERT_TRUE(applyPlayerInputFile(sandbox, "sandbox"));
    EXPECT_TRUE(hasKey(sandbox, "quit", Key::Escape, 1.0f));
    EXPECT_TRUE(hasKey(sandbox, "jump", Key::Space, 1.0f));
    EXPECT_TRUE(hasKey(sandbox, "move_z", Key::W, 1.0f));
    EXPECT_TRUE(hasKey(sandbox, "move_z", Key::S, -1.0f));
    EXPECT_TRUE(hasKey(sandbox, "dodge_forward", Key::Up, 1.0f));
    EXPECT_TRUE(hasKey(sandbox, "dodge_left", Key::Left, 1.0f));
    EXPECT_TRUE(hasKey(sandbox, "sprint", Key::LeftShift, 1.0f));
    EXPECT_TRUE(hasKey(sandbox, "sprint", Key::RightShift, 1.0f));
    EXPECT_FALSE(hasKey(sandbox, "crouch", Key::C, 1.0f));
    EXPECT_TRUE(hasKey(sandbox, "camouflage", Key::C, 1.0f));
    EXPECT_TRUE(hasKey(sandbox, "pickup", Key::G, 1.0f));
    EXPECT_TRUE(hasKey(sandbox, "night_sight", Key::N, 1.0f));
    EXPECT_TRUE(hasKey(sandbox, "infrared", Key::H, 1.0f));
    EXPECT_TRUE(hasKey(sandbox, "ultraviolet", Key::U, 1.0f));
    EXPECT_TRUE(hasKey(sandbox, "echolocation", Key::B, 1.0f));
    EXPECT_TRUE(hasMouse(sandbox, "attack", MouseButton::Left));
    EXPECT_TRUE(hasMouse(sandbox, "shield", MouseButton::Right));
    EXPECT_TRUE(hasAxisHeld(sandbox, "shield", GamepadAxis::LeftTrigger, 0.45f));
    const std::vector<ActionMap::Binding>* look = sandbox.bindings("look_yaw");
    ASSERT_NE(look, nullptr);
    EXPECT_EQ(look->size(), 1u);
    EXPECT_EQ((*look)[0].code, static_cast<uint16_t>(GamepadAxis::RightX));

    ActionMap editor;
    ASSERT_TRUE(applyPlayerInputFile(editor, "editor"));
    EXPECT_TRUE(hasKey(editor, "crouch", Key::X, 1.0f));
    EXPECT_FALSE(hasKey(editor, "camouflage", Key::C, 1.0f));
    EXPECT_FALSE(hasKey(editor, "pickup", Key::G, 1.0f));
    EXPECT_FALSE(hasKey(editor, "night_sight", Key::N, 1.0f));
    EXPECT_FALSE(hasKey(editor, "infrared", Key::H, 1.0f));
    EXPECT_FALSE(hasKey(editor, "ultraviolet", Key::U, 1.0f));
    EXPECT_FALSE(hasKey(editor, "echolocation", Key::B, 1.0f));
    EXPECT_TRUE(hasKey(editor, "move_x", Key::D, 1.0f));
    const std::vector<ActionMap::Binding>* lookY = editor.bindings("look_y");
    ASSERT_NE(lookY, nullptr);
    EXPECT_EQ((*lookY)[0].code, static_cast<uint16_t>(GamepadAxis::RightY));
    EXPECT_EQ((*lookY)[0].threshold, 0.0f);
}

TEST(EntityInput, SandboxFileThenPlayerFile)
{
    ActionMap map;
    map.bindKey("quit", Key::F1);
    ASSERT_TRUE(applySandboxInputFile(map));
    EXPECT_FALSE(hasKey(map, "quit", Key::F1, 1.0f));
    EXPECT_TRUE(hasKey(map, "quit", Key::Escape, 1.0f));
    EXPECT_TRUE(hasButton(map, "quit", GamepadButton::Back, 1.0f));
    EXPECT_TRUE(hasKey(map, "pause", Key::F3, 1.0f));
    EXPECT_TRUE(hasButton(map, "pause", GamepadButton::Start, 1.0f));
    EXPECT_TRUE(hasKey(map, "step", Key::O, 1.0f));
    EXPECT_TRUE(hasKey(map, "quick_save", Key::F5, 1.0f));
    EXPECT_TRUE(hasKey(map, "quick_load", Key::F9, 1.0f));
    EXPECT_TRUE(hasKey(map, "toggle_lighting", Key::F2, 1.0f));
    EXPECT_TRUE(hasKey(map, "reset", Key::R, 1.0f));
    EXPECT_TRUE(hasButton(map, "reset", GamepadButton::Y, 1.0f));
    EXPECT_TRUE(hasKey(map, "speed_up", Key::Equal, 1.0f));
    EXPECT_TRUE(hasButton(map, "speed_up", GamepadButton::RightShoulder, 1.0f));
    EXPECT_TRUE(hasKey(map, "speed_down", Key::Minus, 1.0f));
    EXPECT_TRUE(hasButton(map, "speed_down", GamepadButton::X, 1.0f));
    EXPECT_TRUE(hasKey(map, "yaw", Key::Q, -1.0f));
    EXPECT_TRUE(hasKey(map, "yaw", Key::E, 1.0f));
    EXPECT_TRUE(hasKey(map, "pitch", Key::Z, 1.0f));
    EXPECT_TRUE(hasKey(map, "pitch", Key::X, -1.0f));
    EXPECT_TRUE(hasKey(map, "pawn_x", Key::Left, -1.0f));
    EXPECT_TRUE(hasKey(map, "pawn_x", Key::Right, 1.0f));
    EXPECT_TRUE(hasButton(map, "pawn_x", GamepadButton::DPadLeft, -1.0f));
    EXPECT_TRUE(hasButton(map, "pawn_x", GamepadButton::DPadRight, 1.0f));
    EXPECT_TRUE(hasKey(map, "pawn_z", Key::Up, 1.0f));
    EXPECT_TRUE(hasKey(map, "pawn_z", Key::Down, -1.0f));
    EXPECT_TRUE(hasButton(map, "pawn_z", GamepadButton::DPadUp, 1.0f));
    EXPECT_TRUE(hasButton(map, "pawn_z", GamepadButton::DPadDown, -1.0f));
    EXPECT_TRUE(hasKey(map, "fly_forward", Key::W, 1.0f));
    EXPECT_TRUE(hasKey(map, "fly_forward", Key::S, -1.0f));
    EXPECT_TRUE(hasAxis(map, "fly_forward", GamepadAxis::LeftY, 1.0f));
    EXPECT_TRUE(hasKey(map, "fly_strafe", Key::A, -1.0f));
    EXPECT_TRUE(hasKey(map, "fly_strafe", Key::D, 1.0f));
    EXPECT_TRUE(hasAxis(map, "fly_strafe", GamepadAxis::LeftX, 1.0f));
    EXPECT_TRUE(hasKey(map, "fly_climb", Key::Q, 1.0f));
    EXPECT_TRUE(hasKey(map, "fly_climb", Key::Space, 1.0f));
    EXPECT_TRUE(hasKey(map, "fly_climb", Key::Z, -1.0f));
    EXPECT_TRUE(hasKey(map, "fly_climb", Key::LeftControl, -1.0f));
    EXPECT_TRUE(hasAxis(map, "fly_climb", GamepadAxis::RightTrigger, 1.0f));
    EXPECT_TRUE(hasAxis(map, "fly_climb", GamepadAxis::LeftTrigger, -1.0f));
    EXPECT_TRUE(hasKey(map, "dev_tools", Key::M, 1.0f));
    EXPECT_TRUE(hasKey(map, "profiler", Key::P, 1.0f));
    EXPECT_TRUE(hasKey(map, "inventory", Key::I, 1.0f));
    EXPECT_TRUE(hasKey(map, "anim_walk", Key::Y, 1.0f));
    EXPECT_TRUE(hasKey(map, "talk", Key::T, 1.0f));
    EXPECT_TRUE(hasButton(map, "anim_walk", GamepadButton::RightThumb, 1.0f));
    const std::vector<ActionMap::Binding>* climb = map.bindings("fly_climb");
    ASSERT_NE(climb, nullptr);
    EXPECT_EQ(climb->size(), 6u);

    ASSERT_TRUE(applyPlayerInputFile(map, "sandbox"));
    EXPECT_TRUE(hasKey(map, "jump", Key::Space, 1.0f));
    EXPECT_TRUE(hasKey(map, "camouflage", Key::C, 1.0f));
    EXPECT_TRUE(hasKey(map, "pickup", Key::G, 1.0f));
    EXPECT_TRUE(hasKey(map, "move_x", Key::A, -1.0f));
    EXPECT_TRUE(hasKey(map, "quit", Key::Escape, 1.0f));
    EXPECT_TRUE(hasKey(map, "quick_save", Key::F5, 1.0f));
    EXPECT_TRUE(hasKey(map, "quick_load", Key::F9, 1.0f));
    EXPECT_TRUE(hasKey(map, "fly_strafe", Key::A, -1.0f));
    EXPECT_TRUE(hasKey(map, "fly_climb", Key::Space, 1.0f));
    climb = map.bindings("fly_climb");
    ASSERT_NE(climb, nullptr);
    EXPECT_EQ(climb->size(), 6u);
}

TEST(EntityInput, ModifierGatesTheBinding)
{
    ActionMap map;
    map.bindKeyAsAxis("save_scene", Key::F5, 1.0f);
    map.bindKeyAsAxis("save_scene", Key::S, 1.0f, ActionMap::ModCtrl);
    map.bindKeyAsAxis("quick_save", Key::F5, 1.0f, ActionMap::ModShift);

    Input bare;
    bare.onKeyDown(static_cast<uint16_t>(Key::S), false);
    EXPECT_FALSE(map.pressed(bare, "save_scene"));
    bare.onKeyDown(static_cast<uint16_t>(Key::LeftControl), false);
    EXPECT_TRUE(map.pressed(bare, "save_scene"));

    Input f5;
    f5.onKeyDown(static_cast<uint16_t>(Key::F5), false);
    EXPECT_TRUE(map.pressed(f5, "save_scene"));
    EXPECT_FALSE(map.pressed(f5, "quick_save"));
    f5.onKeyDown(static_cast<uint16_t>(Key::RightShift), false);
    EXPECT_TRUE(map.pressed(f5, "save_scene"));
    EXPECT_TRUE(map.pressed(f5, "quick_save"));
}

TEST(EntityInput, EditorFileThenPlayerFile)
{
    std::vector<EntityInputBinding> rows(1);
    rows[0].action = "keep";
    EXPECT_FALSE(parseEntityInput("{\"version\":1,\"type\":\"editor\",\"bindings\":[]}", rows));
    EXPECT_EQ(rows[0].action, "keep");
    const char* chord = R"({"version":1,"type":"editor","bindings":[{"action":"save_scene","key":"S","mod":"ctrl"},{"action":"nope","key":"K","mod":"hyper"}]})";
    ASSERT_TRUE(parseEntityInput(chord, rows, "editor"));
    EXPECT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].action, "save_scene");
    EXPECT_EQ(rows[0].mods, ActionMap::ModCtrl);

    ActionMap map;
    map.bindKey("cycle_color", Key::F1);
    ASSERT_TRUE(applyEditorInputFile(map));
    EXPECT_FALSE(hasKey(map, "cycle_color", Key::F1, 1.0f));
    EXPECT_TRUE(hasKey(map, "quit", Key::Escape, 1.0f));
    EXPECT_TRUE(hasButton(map, "quit", GamepadButton::Back, 1.0f));
    EXPECT_TRUE(hasKey(map, "place", Key::P, 1.0f));
    EXPECT_TRUE(hasButton(map, "place", GamepadButton::X, 1.0f));
    EXPECT_TRUE(hasKey(map, "delete", Key::Delete, 1.0f));
    EXPECT_TRUE(hasKey(map, "delete", Key::Backspace, 1.0f));
    EXPECT_TRUE(hasButton(map, "delete", GamepadButton::B, 1.0f));
    EXPECT_TRUE(hasKey(map, "toggle_grid", Key::G, 1.0f));
    EXPECT_TRUE(hasKey(map, "toggle_solid", Key::F, 1.0f));
    EXPECT_TRUE(hasKey(map, "toggle_snap", Key::Tab, 1.0f));
    EXPECT_TRUE(hasKey(map, "select_next", Key::RightBracket, 1.0f));
    EXPECT_TRUE(hasKey(map, "select_prev", Key::LeftBracket, 1.0f));
    EXPECT_TRUE(hasKey(map, "type_cube", Key::Digit1, 1.0f));
    EXPECT_TRUE(hasKey(map, "type_sphere", Key::Digit2, 1.0f));
    EXPECT_TRUE(hasKey(map, "type_particle", Key::Digit3, 1.0f));
    EXPECT_TRUE(hasKey(map, "type_point_light", Key::Digit4, 1.0f));
    EXPECT_TRUE(hasKey(map, "type_spot_light", Key::Digit5, 1.0f));
    EXPECT_TRUE(hasKey(map, "type_player", Key::Digit6, 1.0f));
    EXPECT_TRUE(hasKey(map, "type_hunter", Key::Digit7, 1.0f));
    EXPECT_TRUE(hasKey(map, "type_wolf", Key::Digit8, 1.0f));
    EXPECT_TRUE(hasKey(map, "play", Key::F12, 1.0f));
    EXPECT_TRUE(hasKey(map, "cycle_type", Key::T, 1.0f));
    EXPECT_TRUE(hasKey(map, "cycle_color", Key::C, 1.0f));
    EXPECT_TRUE(hasKey(map, "toggle_particle_ui", Key::F2, 1.0f));
    EXPECT_TRUE(hasKey(map, "toggle_anim_ui", Key::F4, 1.0f));
    EXPECT_TRUE(hasKey(map, "toggle_hsm_ui", Key::F8, 1.0f));
    EXPECT_TRUE(hasKey(map, "toggle_assets_ui", Key::F10, 1.0f));
    EXPECT_TRUE(hasKey(map, "debug_fill", Key::F1, 1.0f));
    EXPECT_TRUE(hasKey(map, "debug_lighting", Key::F6, 1.0f));
    EXPECT_TRUE(hasKey(map, "debug_shadow_enable", Key::F7, 1.0f));
    EXPECT_TRUE(hasKey(map, "toggle_scene_mode", Key::F3, 1.0f));
    EXPECT_TRUE(hasKey(map, "debug_gbuffer", Key::F11, 1.0f));
    EXPECT_TRUE(hasKey(map, "move_y", Key::E, 1.0f));
    EXPECT_TRUE(hasKey(map, "move_y", Key::Q, -1.0f));
    EXPECT_TRUE(hasAxis(map, "move_y", GamepadAxis::RightTrigger, 1.0f));
    EXPECT_TRUE(hasAxis(map, "move_y", GamepadAxis::LeftTrigger, -1.0f));
    EXPECT_TRUE(hasKey(map, "save_scene", Key::F5, 1.0f));
    EXPECT_TRUE(hasKey(map, "save_scene", Key::S, 1.0f, ActionMap::ModCtrl));
    EXPECT_FALSE(hasKey(map, "save_scene", Key::S, 1.0f));
    EXPECT_TRUE(hasKey(map, "load_scene", Key::F9, 1.0f));
    EXPECT_TRUE(hasKey(map, "load_scene", Key::O, 1.0f, ActionMap::ModCtrl));
    EXPECT_TRUE(hasKey(map, "quick_save", Key::F5, 1.0f, ActionMap::ModShift));
    EXPECT_FALSE(hasKey(map, "quick_save", Key::F5, 1.0f));
    EXPECT_TRUE(hasKey(map, "quick_load", Key::F9, 1.0f, ActionMap::ModShift));

    ASSERT_TRUE(applyPlayerInputFile(map, "editor"));
    EXPECT_TRUE(hasKey(map, "jump", Key::Space, 1.0f));
    EXPECT_TRUE(hasKey(map, "crouch", Key::X, 1.0f));
    EXPECT_FALSE(hasKey(map, "camouflage", Key::C, 1.0f));
    EXPECT_FALSE(hasKey(map, "pickup", Key::G, 1.0f));
    EXPECT_TRUE(hasKey(map, "cycle_color", Key::C, 1.0f));
    EXPECT_TRUE(hasKey(map, "toggle_grid", Key::G, 1.0f));
    EXPECT_TRUE(hasKey(map, "save_scene", Key::F5, 1.0f));
    EXPECT_TRUE(hasKey(map, "quick_save", Key::F5, 1.0f, ActionMap::ModShift));
    EXPECT_TRUE(hasKey(map, "weapon_1", Key::Digit1, 1.0f));
    EXPECT_TRUE(hasKey(map, "type_cube", Key::Digit1, 1.0f));
    EXPECT_TRUE(hasKey(map, "weapon_3", Key::Digit3, 1.0f));
    EXPECT_TRUE(hasKey(map, "type_particle", Key::Digit3, 1.0f));
}

TEST(EntityInput, Player2dFileLeavesOtherActions)
{
    std::vector<EntityInputBinding> rows(1);
    rows[0].action = "keep";
    EXPECT_FALSE(parseEntityInput("{\"version\":1,\"type\":\"player2d\",\"bindings\":[]}", rows));
    EXPECT_EQ(rows[0].action, "keep");
    EXPECT_EQ(entityInputVirtualPath("player2d"), "entities/player2d.entity.input.json");

    ActionMap map;
    map.bindKey("quit", Key::Escape);
    map.bindKey("jump", Key::F1);
    ASSERT_TRUE(applyPlayer2dInputFile(map));
    EXPECT_TRUE(hasKey(map, "quit", Key::Escape, 1.0f));
    EXPECT_FALSE(hasKey(map, "jump", Key::F1, 1.0f));
    EXPECT_TRUE(hasKey(map, "jump", Key::Space, 1.0f));
    EXPECT_TRUE(hasKey(map, "jump", Key::W, 1.0f));
    EXPECT_TRUE(hasKey(map, "jump", Key::Up, 1.0f));
    EXPECT_TRUE(hasButton(map, "jump", GamepadButton::A, 1.0f));
    EXPECT_TRUE(hasButton(map, "jump", GamepadButton::B, 1.0f));
    EXPECT_TRUE(hasKey(map, "move", Key::A, -1.0f));
    EXPECT_TRUE(hasKey(map, "move", Key::D, 1.0f));
    EXPECT_TRUE(hasKey(map, "move", Key::Left, -1.0f));
    EXPECT_TRUE(hasKey(map, "move", Key::Right, 1.0f));
    EXPECT_TRUE(hasAxis(map, "move", GamepadAxis::LeftX, 1.0f));
    EXPECT_TRUE(hasButton(map, "move", GamepadButton::DPadLeft, -1.0f));
    EXPECT_TRUE(hasButton(map, "move", GamepadButton::DPadRight, 1.0f));
    EXPECT_FALSE(hasKey(map, "move_x", Key::A, -1.0f));
    EXPECT_FALSE(hasKey(map, "camouflage", Key::C, 1.0f));
    const std::vector<ActionMap::Binding>* jump = map.bindings("jump");
    ASSERT_NE(jump, nullptr);
    EXPECT_EQ(jump->size(), 5u);
    const std::vector<ActionMap::Binding>* move = map.bindings("move");
    ASSERT_NE(move, nullptr);
    EXPECT_EQ(move->size(), 7u);
}

TEST(EntityInput, Sandbox2dFileThenPlayer2dFile)
{
    std::vector<EntityInputBinding> rows(1);
    rows[0].action = "keep";
    EXPECT_FALSE(parseEntityInput("{\"version\":1,\"type\":\"sandbox2d\",\"bindings\":[]}", rows));
    EXPECT_EQ(rows[0].action, "keep");

    ActionMap map;
    map.bindKey("quit", Key::F1);
    ASSERT_TRUE(applySandbox2dInputFile(map));
    EXPECT_FALSE(hasKey(map, "quit", Key::F1, 1.0f));
    EXPECT_TRUE(hasKey(map, "quit", Key::Escape, 1.0f));
    EXPECT_TRUE(hasButton(map, "quit", GamepadButton::Back, 1.0f));
    EXPECT_TRUE(hasKey(map, "reset", Key::R, 1.0f));
    EXPECT_TRUE(hasButton(map, "reset", GamepadButton::Y, 1.0f));
    EXPECT_TRUE(hasKey(map, "debug", Key::F1, 1.0f));
    EXPECT_TRUE(hasButton(map, "debug", GamepadButton::Start, 1.0f));
    EXPECT_TRUE(hasKey(map, "net_disconnect", Key::F4, 1.0f));
    EXPECT_TRUE(hasKey(map, "net_host", Key::F5, 1.0f));
    EXPECT_TRUE(hasKey(map, "net_join", Key::F6, 1.0f));
    EXPECT_TRUE(hasKey(map, "debug_listen", Key::F10, 1.0f));

    ASSERT_TRUE(applyPlayer2dInputFile(map));
    EXPECT_TRUE(hasKey(map, "quit", Key::Escape, 1.0f));
    EXPECT_TRUE(hasKey(map, "net_host", Key::F5, 1.0f));
    EXPECT_TRUE(hasKey(map, "jump", Key::Space, 1.0f));
    EXPECT_TRUE(hasKey(map, "move", Key::A, -1.0f));
}
