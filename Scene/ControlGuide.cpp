#include "Scene/ControlGuide.h"

#include "Input/ActionMap.h"
#include "Scene/EntityMaster.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>

namespace Dark
{
    namespace
    {

        constexpr std::uintmax_t kMaxJsonBytes = 64u * 1024u;

        enum class ControlList : uint8_t
        {
            Game,
            Engine,
        };

        struct ControlCopy
        {
            const char* action;
            ControlList list;
            const char* positive;
            const char* negative;
            const char* stick;
        };

        std::string lowerCopy(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
            {
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            }
            return out;
        }

        bool sameText(std::string_view a, std::string_view b)
        {
            return lowerCopy(a) == lowerCopy(b);
        }

        bool forSandbox(const EntityInputBinding& row)
        {
            return row.app.empty() || sameText(row.app, "sandbox");
        }

        bool hasLabel(const std::string& keys, const std::string& label)
        {
            std::size_t start = 0;
            while (start <= keys.size())
            {
                const std::size_t found = keys.find(" / ", start);
                const std::size_t stop  = found == std::string::npos ? keys.size() : found;
                if (keys.compare(start, stop - start, label) == 0)
                    return true;
                if (found == std::string::npos)
                    break;
                start = found + 3;
            }
            return false;
        }

        void addEntry(std::vector<ControlRow>& list, std::string label, std::string what)
        {
            if (label.empty() || what.empty())
                return;
            for (ControlRow& row : list)
            {
                if (row.what != what)
                    continue;
                if (!hasLabel(row.keys, label))
                {
                    if (!row.keys.empty())
                        row.keys += " / ";
                    row.keys += label;
                }
                return;
            }
            list.push_back(ControlRow{ std::move(label), std::move(what) });
        }

        const char* namedLabel(std::string_view name, const std::pair<const char*, const char*>* table, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                if (sameText(name, table[i].first))
                    return table[i].second;
            }
            return nullptr;
        }

        std::string keyLabel(std::string_view name)
        {
            if (name.empty())
                return {};
            if (name.size() == 1)
            {
                unsigned char c = static_cast<unsigned char>(name[0]);
                if (c >= 'a' && c <= 'z')
                    c = static_cast<unsigned char>(c - 'a' + 'A');
                if (c >= 'A' && c <= 'Z')
                    return std::string(1, static_cast<char>(c));
            }
            if (name.size() == 6 && sameText(name.substr(0, 5), "digit") && name[5] >= '0' && name[5] <= '9')
                return std::string(1, name[5]);
            if ((name[0] == 'F' || name[0] == 'f') && name.size() >= 2 && name.size() <= 3)
            {
                int  n  = 0;
                bool ok = true;
                for (std::size_t i = 1; i < name.size(); ++i)
                {
                    if (name[i] < '0' || name[i] > '9')
                        ok = false;
                    else
                        n = n * 10 + (name[i] - '0');
                }
                if (ok && n >= 1 && n <= 12)
                    return "F" + std::to_string(n);
            }
            static const std::pair<const char*, const char*> kKeys[] = {
                { "Escape", "Esc" },       { "Space", "Space" },       { "Enter", "Enter" }, { "Tab", "Tab" },      { "LeftShift", "Shift" }, { "RightShift", "Shift" },
                { "LeftControl", "Ctrl" }, { "RightControl", "Ctrl" }, { "LeftAlt", "Alt" }, { "RightAlt", "Alt" }, { "Minus", "-" },         { "Equal", "=" },
                { "Left", "Left" },        { "Right", "Right" },       { "Up", "Up" },       { "Down", "Down" },    { "Delete", "Delete" },   { "Backspace", "Backspace" },
            };
            if (const char* label = namedLabel(name, kKeys, static_cast<int>(sizeof(kKeys) / sizeof(kKeys[0]))))
                return label;
            return std::string(name);
        }

        std::string buttonLabel(std::string_view name)
        {
            if (name.empty())
                return {};
            static const std::pair<const char*, const char*> kButtons[] = {
                { "A", "Pad A" },
                { "B", "Pad B" },
                { "X", "Pad X" },
                { "Y", "Pad Y" },
                { "LeftShoulder", "LB" },
                { "RightShoulder", "RB" },
                { "Back", "Pad Back" },
                { "Start", "Pad Start" },
                { "LeftThumb", "LS" },
                { "RightThumb", "RS" },
                { "DPadUp", "D-pad up" },
                { "DPadDown", "D-pad down" },
                { "DPadLeft", "D-pad left" },
                { "DPadRight", "D-pad right" },
            };
            if (const char* label = namedLabel(name, kButtons, static_cast<int>(sizeof(kButtons) / sizeof(kButtons[0]))))
                return label;
            return std::string(name);
        }

        std::string axisLabel(std::string_view name)
        {
            if (name.empty())
                return {};
            static const std::pair<const char*, const char*> kAxes[] = {
                { "LeftX", "Left stick" }, { "LeftY", "Left stick" }, { "RightX", "Right stick" }, { "RightY", "Right stick" }, { "LeftTrigger", "LT" }, { "RightTrigger", "RT" },
            };
            if (const char* label = namedLabel(name, kAxes, static_cast<int>(sizeof(kAxes) / sizeof(kAxes[0]))))
                return label;
            return std::string(name);
        }

        std::string mouseLabel(std::string_view name)
        {
            if (name.empty())
                return {};
            static const std::pair<const char*, const char*> kMouse[] = {
                { "Left", "Mouse left" },
                { "Right", "Mouse right" },
                { "Middle", "Mouse middle" },
            };
            if (const char* label = namedLabel(name, kMouse, static_cast<int>(sizeof(kMouse) / sizeof(kMouse[0]))))
                return label;
            return std::string(name);
        }

        bool isStick(std::string_view axis)
        {
            return sameText(axis, "LeftX") || sameText(axis, "LeftY") || sameText(axis, "RightX") || sameText(axis, "RightY");
        }

        const ControlCopy* findCopy(std::string_view action)
        {
            // positive is scale >= 0 and mouse. negative is scale < 0. stick replaces both for Left/Right sticks.
            static const ControlCopy kCopy[] = {
                { "move_x", ControlList::Game, "Move right", "Move left", "Move" },
                { "move_z", ControlList::Game, "Move forward", "Move back", "Move" },
                { "jump", ControlList::Game, "Jump", nullptr, nullptr },
                { "sprint", ControlList::Game, "Sprint", nullptr, nullptr },
                { "crouch", ControlList::Game, "Crouch", nullptr, nullptr },
                { "camouflage", ControlList::Game, "Camouflage", nullptr, nullptr },
                { "pickup", ControlList::Game, "Pick up, or take all and close", nullptr, nullptr },
                { "takedown", ControlList::Game, "Takedown", nullptr, nullptr },
                { "lean_left", ControlList::Game, "Lean left", nullptr, nullptr },
                { "lean_right", ControlList::Game, "Lean right", nullptr, nullptr },
                { "attack", ControlList::Game, "Attack", nullptr, nullptr },
                { "shield", ControlList::Game, "Shield", nullptr, nullptr },
                { "flashlight", ControlList::Game, "Flashlight", nullptr, nullptr },
                { "night_sight", ControlList::Game, "Night sight", nullptr, nullptr },
                { "infrared", ControlList::Game, "Infrared", nullptr, nullptr },
                { "ultraviolet", ControlList::Game, "Ultraviolet", nullptr, nullptr },
                { "echolocation", ControlList::Game, "Echolocation", nullptr, nullptr },
                { "weapon_1", ControlList::Game, "Melee", nullptr, nullptr },
                { "weapon_2", ControlList::Game, "Firearm", nullptr, nullptr },
                { "weapon_3", ControlList::Game, "Throwable", nullptr, nullptr },
                { "look_yaw", ControlList::Game, "Look", nullptr, "Look" },
                { "look_pitch", ControlList::Game, "Look", nullptr, "Look" },
                { "look_x", ControlList::Game, "Look", nullptr, "Look" },
                { "look_y", ControlList::Game, "Look", nullptr, "Look" },
                { "dodge_forward", ControlList::Game, "Dodge forward (double-tap)", nullptr, nullptr },
                { "dodge_back", ControlList::Game, "Dodge back (double-tap)", nullptr, nullptr },
                { "dodge_left", ControlList::Game, "Dodge left (double-tap)", nullptr, nullptr },
                { "dodge_right", ControlList::Game, "Dodge right (double-tap)", nullptr, nullptr },
                { "quit", ControlList::Game, "Menu", nullptr, nullptr },
                { "inventory", ControlList::Game, "Inventory", nullptr, nullptr },
                { "journal", ControlList::Game, "Journal", nullptr, nullptr },
                { "upgrades", ControlList::Game, "Upgrades", nullptr, nullptr },
                { "talk", ControlList::Game, "Talk", nullptr, nullptr },

                { "pause", ControlList::Engine, "Pause the simulation", nullptr, nullptr },
                { "step", ControlList::Engine, "Step one frame while paused", nullptr, nullptr },
                { "quick_save", ControlList::Engine, "Quick save", nullptr, nullptr },
                { "quick_load", ControlList::Engine, "Quick load", nullptr, nullptr },
                { "toggle_lighting", ControlList::Engine, "Toggle lighting", nullptr, nullptr },
                { "reset", ControlList::Engine, "Reset the demo cube", nullptr, nullptr },
                { "speed_up", ControlList::Engine, "Spin the cube faster", nullptr, nullptr },
                { "speed_down", ControlList::Engine, "Spin the cube slower", nullptr, nullptr },
                { "yaw", ControlList::Engine, "Turn the cube", "Turn the cube", nullptr },
                { "pawn_x", ControlList::Engine, "Nudge the walker right", "Nudge the walker left", nullptr },
                { "pawn_z", ControlList::Engine, "Nudge the walker forward", "Nudge the walker back", nullptr },
                { "fly_forward", ControlList::Engine, "Fly forward while paused", "Fly back while paused", "Fly while paused" },
                { "fly_strafe", ControlList::Engine, "Fly right while paused", "Fly left while paused", "Fly while paused" },
                { "fly_climb", ControlList::Engine, "Fly up while paused", "Fly down while paused", nullptr },
                { "dev_tools", ControlList::Engine, "Dev tools", nullptr, nullptr },
                { "profiler", ControlList::Engine, "Profiler", nullptr, nullptr },
                { "anim_walk", ControlList::Engine, "Demo walk animation", nullptr, nullptr },
            };
            for (const ControlCopy& copy : kCopy)
            {
                if (sameText(action, copy.action))
                    return &copy;
            }
            return nullptr;
        }

        const char* textFor(const ControlCopy& copy, const EntityInputBinding& row)
        {
            if (!row.axis.empty() && isStick(row.axis) && copy.stick)
                return copy.stick;
            if (row.scale < 0.0f && copy.negative)
                return copy.negative;
            return copy.positive;
        }

        void appendBinding(ControlGuide& guide, const EntityInputBinding& row, bool engineByDefault)
        {
            if (!forSandbox(row))
                return;
            std::string label = controlDeviceLabel(row);
            if (label.empty())
                return;
            const ControlCopy* copy   = findCopy(row.action);
            const bool         engine = copy ? copy->list == ControlList::Engine : engineByDefault;
            const char*        line   = copy ? textFor(*copy, row) : nullptr;
            std::string        what   = line && line[0] != '\0' ? line : row.action;
            addEntry(engine ? guide.engine : guide.game, std::move(label), std::move(what));
        }

        void appendFixed(ControlGuide& guide)
        {
            // These are not in the input json. Play reads the mouse. Dialogue reads the arrows and Enter.
            // The movie keys and the paused fly boost are read in SandboxApp.
            addEntry(guide.game, "Mouse", "Look");
            addEntry(guide.game, "Up", "Dialogue choice");
            addEntry(guide.game, "Down", "Dialogue choice");
            addEntry(guide.game, "Enter", "Confirm the dialogue choice");
            addEntry(guide.engine, "Shift", "Fly faster while paused");
            addEntry(guide.engine, "F8", "Play the sample movie");
            addEntry(guide.engine, "F6", "Pause or resume the movie");
            addEntry(guide.engine, "F7", "Rewind the movie");
            addEntry(guide.engine, "F10", "Advance the movie");
        }

        bool readInputFile(std::string_view relative, std::string_view expectedType, std::vector<EntityInputBinding>& out)
        {
            const std::filesystem::path path = resolveContentFile(relative);
            if (path.empty())
                return false;
            std::error_code ec;
            const auto      sz = std::filesystem::file_size(path, ec);
            if (ec || sz == 0 || sz > kMaxJsonBytes)
                return false;
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            std::ostringstream ss;
            ss << in.rdbuf();
            return parseEntityInput(ss.str(), out, expectedType);
        }

    } // namespace

    std::string controlDeviceLabel(const EntityInputBinding& row)
    {
        std::string label;
        if (!row.key.empty())
            label = keyLabel(row.key);
        else if (!row.button.empty())
            label = buttonLabel(row.button);
        else if (!row.axis.empty())
            label = axisLabel(row.axis);
        else if (!row.mouse.empty())
            label = mouseLabel(row.mouse);
        if (label.empty())
            return {};
        if ((row.mods & ActionMap::ModAlt) != 0)
            label = "Alt+" + label;
        if ((row.mods & ActionMap::ModShift) != 0)
            label = "Shift+" + label;
        if ((row.mods & ActionMap::ModCtrl) != 0)
            label = "Ctrl+" + label;
        return label;
    }

    void buildSandboxControlGuide(const std::vector<EntityInputBinding>& player, const std::vector<EntityInputBinding>& sandbox, ControlGuide& out)
    {
        out.game.clear();
        out.engine.clear();
        for (const EntityInputBinding& row : player)
            appendBinding(out, row, false);
        for (const EntityInputBinding& row : sandbox)
            appendBinding(out, row, true);
        appendFixed(out);
    }

    bool loadSandboxControlGuide(ControlGuide& out)
    {
        std::vector<EntityInputBinding> player;
        std::vector<EntityInputBinding> sandbox;
        if (!readInputFile(entityInputVirtualPath("player"), "player", player))
            return false;
        if (!readInputFile("sandbox.input.json", "sandbox", sandbox))
            return false;
        buildSandboxControlGuide(player, sandbox, out);
        return true;
    }

} // namespace Dark
