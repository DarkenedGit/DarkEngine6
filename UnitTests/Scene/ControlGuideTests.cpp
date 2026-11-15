#include <gtest/gtest.h>

#include "Input/ActionMap.h"
#include "Scene/ControlGuide.h"
#include "Scene/EntityInput.h"
#include "Scene/EntityMaster.h"

#include <fstream>
#include <sstream>
#include <string>

using namespace Dark;

namespace
{

    bool hasToken(const std::vector<ControlRow>& rows, const std::string& token)
    {
        for (const ControlRow& row : rows)
        {
            std::size_t start = 0;
            while (start <= row.keys.size())
            {
                const std::size_t found = row.keys.find(" / ", start);
                const std::size_t stop  = found == std::string::npos ? row.keys.size() : found;
                if (row.keys.compare(start, stop - start, token) == 0)
                    return true;
                if (found == std::string::npos)
                    break;
                start = found + 3;
            }
        }
        return false;
    }

    bool tokenDoes(const std::vector<ControlRow>& rows, const char* token, const char* what)
    {
        for (const ControlRow& row : rows)
        {
            if (row.what != what)
                continue;
            std::size_t start = 0;
            while (start <= row.keys.size())
            {
                const std::size_t found = row.keys.find(" / ", start);
                const std::size_t stop  = found == std::string::npos ? row.keys.size() : found;
                if (row.keys.compare(start, stop - start, token) == 0)
                    return true;
                if (found == std::string::npos)
                    break;
                start = found + 3;
            }
        }
        return false;
    }

    bool listed(const ControlGuide& guide, const std::string& token)
    {
        return hasToken(guide.game, token) || hasToken(guide.engine, token);
    }

    bool forSandbox(const EntityInputBinding& row)
    {
        if (row.app.empty())
            return true;
        std::string app = row.app;
        for (char& c : app)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        return app == "sandbox";
    }

    std::string readFile(const std::filesystem::path& path)
    {
        std::ifstream      in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

} // namespace

TEST(ControlGuide, LabelsCollapseModifiersAndPads)
{
    EntityInputBinding shift;
    shift.key = "LeftShift";
    EXPECT_EQ(controlDeviceLabel(shift), "Shift");

    EntityInputBinding digit;
    digit.key = "Digit1";
    EXPECT_EQ(controlDeviceLabel(digit), "1");

    EntityInputBinding pad;
    pad.button = "B";
    EXPECT_EQ(controlDeviceLabel(pad), "Pad B");

    EntityInputBinding stick;
    stick.axis = "RightX";
    EXPECT_EQ(controlDeviceLabel(stick), "Right stick");

    EntityInputBinding mouse;
    mouse.mouse = "Left";
    EXPECT_EQ(controlDeviceLabel(mouse), "Mouse left");

    EntityInputBinding mod;
    mod.key  = "F5";
    mod.mods = ActionMap::ModShift;
    EXPECT_EQ(controlDeviceLabel(mod), "Shift+F5");

    EntityInputBinding empty;
    EXPECT_TRUE(controlDeviceLabel(empty).empty());
}

TEST(ControlGuide, SplitsGameFromEngineAndKeepsUnknowns)
{
    EntityInputBinding jump;
    jump.action = "jump";
    jump.key    = "Space";

    EntityInputBinding editorCrouch;
    editorCrouch.action = "crouch";
    editorCrouch.app    = "editor";
    editorCrouch.key    = "X";

    EntityInputBinding inventory;
    inventory.action = "inventory";
    inventory.key    = "I";

    EntityInputBinding profiler;
    profiler.action = "profiler";
    profiler.key    = "P";

    EntityInputBinding frob;
    frob.action = "frob";
    frob.key    = "H";

    ControlGuide guide;
    buildSandboxControlGuide({ jump, editorCrouch }, { inventory, profiler, frob }, guide);

    EXPECT_TRUE(tokenDoes(guide.game, "Space", "Jump"));
    EXPECT_TRUE(tokenDoes(guide.game, "I", "Inventory"));
    EXPECT_FALSE(hasToken(guide.game, "X"));
    EXPECT_FALSE(hasToken(guide.engine, "X"));
    EXPECT_TRUE(tokenDoes(guide.engine, "P", "Profiler"));
    EXPECT_FALSE(hasToken(guide.game, "P"));
    EXPECT_TRUE(tokenDoes(guide.engine, "H", "frob"));
    EXPECT_TRUE(tokenDoes(guide.game, "Mouse", "Look"));
    EXPECT_TRUE(tokenDoes(guide.game, "Enter", "Confirm the dialogue choice"));
    EXPECT_TRUE(tokenDoes(guide.engine, "F8", "Play the sample movie"));
    EXPECT_TRUE(tokenDoes(guide.engine, "F6", "Pause or resume the movie"));
    EXPECT_TRUE(tokenDoes(guide.engine, "F7", "Rewind the movie"));
    EXPECT_TRUE(tokenDoes(guide.engine, "F10", "Advance the movie"));
    EXPECT_TRUE(tokenDoes(guide.engine, "Shift", "Fly faster while paused"));
}

TEST(ControlGuide, SandboxFilesListEveryBinding)
{
    const std::filesystem::path playerPath  = resolveContentFile(entityInputVirtualPath("player"));
    const std::filesystem::path sandboxPath = resolveContentFile("sandbox.input.json");
    ASSERT_FALSE(playerPath.empty());
    ASSERT_FALSE(sandboxPath.empty());

    std::vector<EntityInputBinding> player;
    std::vector<EntityInputBinding> sandbox;
    ASSERT_TRUE(parseEntityInput(readFile(playerPath), player, "player"));
    ASSERT_TRUE(parseEntityInput(readFile(sandboxPath), sandbox, "sandbox"));

    ControlGuide guide;
    ASSERT_TRUE(loadSandboxControlGuide(guide));
    EXPECT_FALSE(guide.game.empty());
    EXPECT_FALSE(guide.engine.empty());

    for (const EntityInputBinding& row : player)
    {
        if (!forSandbox(row))
            continue;
        const std::string label = controlDeviceLabel(row);
        EXPECT_FALSE(label.empty()) << row.action;
        EXPECT_TRUE(listed(guide, label)) << row.action << " " << label;
    }
    for (const EntityInputBinding& row : sandbox)
    {
        if (!forSandbox(row))
            continue;
        const std::string label = controlDeviceLabel(row);
        EXPECT_FALSE(label.empty()) << row.action;
        EXPECT_TRUE(listed(guide, label)) << row.action << " " << label;
    }

    EXPECT_TRUE(tokenDoes(guide.game, "W", "Move forward"));
    EXPECT_TRUE(tokenDoes(guide.game, "S", "Move back"));
    EXPECT_TRUE(tokenDoes(guide.game, "A", "Move left"));
    EXPECT_TRUE(tokenDoes(guide.game, "D", "Move right"));
    EXPECT_TRUE(tokenDoes(guide.game, "Left stick", "Move"));
    EXPECT_TRUE(tokenDoes(guide.game, "Right stick", "Look"));
    EXPECT_TRUE(tokenDoes(guide.game, "Mouse", "Look"));
    EXPECT_TRUE(tokenDoes(guide.game, "Space", "Jump"));
    EXPECT_TRUE(tokenDoes(guide.game, "Pad A", "Jump"));
    EXPECT_TRUE(tokenDoes(guide.game, "1", "Melee"));
    EXPECT_TRUE(tokenDoes(guide.game, "Esc", "Menu"));
    EXPECT_TRUE(tokenDoes(guide.game, "I", "Inventory"));
    EXPECT_TRUE(tokenDoes(guide.game, "T", "Talk"));
    EXPECT_TRUE(tokenDoes(guide.game, "E", "Takedown"));
    EXPECT_TRUE(tokenDoes(guide.game, "E", "Lean right"));
    EXPECT_FALSE(hasToken(guide.game, "X"));
    EXPECT_FALSE(hasToken(guide.game, "P"));
    EXPECT_FALSE(hasToken(guide.game, "F2"));

    EXPECT_TRUE(tokenDoes(guide.engine, "F2", "Toggle lighting"));
    EXPECT_TRUE(tokenDoes(guide.engine, "F3", "Pause the simulation"));
    EXPECT_TRUE(tokenDoes(guide.engine, "F5", "Quick save"));
    EXPECT_TRUE(tokenDoes(guide.engine, "F9", "Quick load"));
    EXPECT_TRUE(tokenDoes(guide.engine, "M", "Dev tools"));
    EXPECT_TRUE(tokenDoes(guide.engine, "P", "Profiler"));
    EXPECT_TRUE(tokenDoes(guide.engine, "W", "Fly forward while paused"));
    EXPECT_TRUE(tokenDoes(guide.engine, "Left stick", "Fly while paused"));
    EXPECT_TRUE(tokenDoes(guide.engine, "=", "Spin the cube faster"));
    EXPECT_TRUE(tokenDoes(guide.engine, "-", "Spin the cube slower"));
    EXPECT_TRUE(tokenDoes(guide.engine, "Q", "Turn the cube"));
    EXPECT_TRUE(tokenDoes(guide.engine, "E", "Turn the cube"));
    EXPECT_TRUE(tokenDoes(guide.engine, "Z", "Pitch the cube"));
    EXPECT_TRUE(tokenDoes(guide.engine, "X", "Pitch the cube"));
    EXPECT_TRUE(tokenDoes(guide.engine, "F8", "Play the sample movie"));
    EXPECT_FALSE(hasToken(guide.engine, "Esc"));
    EXPECT_FALSE(hasToken(guide.engine, "I"));
}
