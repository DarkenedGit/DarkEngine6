#include <gtest/gtest.h>

#include "Input/ActionMap.h"
#include "Scene/EntityInput.h"

#include <vector>

using namespace Dark;

namespace
{
    bool hasKey(const ActionMap& map, const char* action, Key key, float scale)
    {
        const std::vector<ActionMap::Binding>* list = map.bindings(action);
        if (!list)
            return false;
        for (const ActionMap::Binding& b : *list)
        {
            if (b.type == ActionMap::BindingType::Key && b.code == static_cast<uint16_t>(key) && b.scale == scale)
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
    EXPECT_EQ(entityInputVirtualPath("player"), "entities/player.entity.input.json");
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
    EXPECT_TRUE(hasMouse(sandbox, "attack", MouseButton::Left));
    EXPECT_TRUE(hasMouse(sandbox, "shield", MouseButton::Right));
    EXPECT_TRUE(hasAxisHeld(sandbox, "shield", GamepadAxis::LeftTrigger, 0.45f));
    const std::vector<ActionMap::Binding>* look = sandbox.bindings("look_yaw");
    ASSERT_NE(look, nullptr);
    EXPECT_EQ(look->size(), 1u);
    EXPECT_EQ((*look)[0].code, static_cast<uint16_t>(GamepadAxis::RightX));

    ActionMap editor;
    ASSERT_TRUE(applyPlayerInputFile(editor, "editor"));
    EXPECT_TRUE(hasKey(editor, "crouch", Key::C, 1.0f));
    EXPECT_TRUE(hasKey(editor, "move_x", Key::D, 1.0f));
    const std::vector<ActionMap::Binding>* lookY = editor.bindings("look_y");
    ASSERT_NE(lookY, nullptr);
    EXPECT_EQ((*lookY)[0].code, static_cast<uint16_t>(GamepadAxis::RightY));
    EXPECT_EQ((*lookY)[0].threshold, 0.0f);
}
