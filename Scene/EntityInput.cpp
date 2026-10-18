#include "Scene/EntityInput.h"

#include "Core/Log.h"
#include "Input/ActionMap.h"
#include "Input/InputCodes.h"
#include "Scene/EntityMaster.h"

#include "third_party/nlohmann/json.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

namespace Dark
{
    namespace
    {
        using json = nlohmann::json;

        constexpr std::uintmax_t kMaxJsonBytes = 64u * 1024u;

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

        bool readString(const json& obj, const char* key, std::string& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end())
                return false;
            if (const auto* s = it->get_ptr<const json::string_t*>())
            {
                out = *s;
                return true;
            }
            return false;
        }

        bool jsonToFloat(const json& v, float& out)
        {
            if (const auto* f = v.get_ptr<const json::number_float_t*>())
            {
                out = static_cast<float>(*f);
                return true;
            }
            if (const auto* i = v.get_ptr<const json::number_integer_t*>())
            {
                out = static_cast<float>(*i);
                return true;
            }
            if (const auto* u = v.get_ptr<const json::number_unsigned_t*>())
            {
                out = static_cast<float>(*u);
                return true;
            }
            return false;
        }

        bool jsonToInt(const json& v, int& out)
        {
            float n = 0.0f;
            if (!jsonToFloat(v, n) || !std::isfinite(n))
                return false;
            out = static_cast<int>(n);
            return true;
        }

        template <typename T>
        bool namedValue(std::string_view name, const std::pair<const char*, T>* table, int count, T& out)
        {
            for (int i = 0; i < count; ++i)
            {
                if (sameText(name, table[i].first))
                {
                    out = table[i].second;
                    return true;
                }
            }
            return false;
        }

        bool keyFromName(std::string_view name, Key& out)
        {
            static const std::pair<const char*, Key> kKeys[] = {
                { "A", Key::A }, { "B", Key::B }, { "C", Key::C }, { "D", Key::D }, { "E", Key::E },
                { "F", Key::F }, { "G", Key::G }, { "H", Key::H }, { "I", Key::I }, { "J", Key::J },
                { "K", Key::K }, { "L", Key::L }, { "M", Key::M }, { "N", Key::N }, { "O", Key::O },
                { "P", Key::P }, { "Q", Key::Q }, { "R", Key::R }, { "S", Key::S }, { "T", Key::T },
                { "U", Key::U }, { "V", Key::V }, { "W", Key::W }, { "X", Key::X }, { "Y", Key::Y }, { "Z", Key::Z },
                { "Digit0", Key::Digit0 }, { "Digit1", Key::Digit1 }, { "Digit2", Key::Digit2 }, { "Digit3", Key::Digit3 },
                { "Digit4", Key::Digit4 }, { "Digit5", Key::Digit5 }, { "Digit6", Key::Digit6 }, { "Digit7", Key::Digit7 },
                { "Digit8", Key::Digit8 }, { "Digit9", Key::Digit9 },
                { "F1", Key::F1 }, { "F2", Key::F2 }, { "F3", Key::F3 }, { "F4", Key::F4 }, { "F5", Key::F5 },
                { "F6", Key::F6 }, { "F7", Key::F7 }, { "F8", Key::F8 }, { "F9", Key::F9 }, { "F10", Key::F10 },
                { "F11", Key::F11 }, { "F12", Key::F12 },
                { "Escape", Key::Escape }, { "Tab", Key::Tab }, { "CapsLock", Key::CapsLock }, { "Space", Key::Space },
                { "Enter", Key::Enter }, { "Backspace", Key::Backspace }, { "Delete", Key::Delete }, { "Insert", Key::Insert },
                { "Home", Key::Home }, { "End", Key::End }, { "PageUp", Key::PageUp }, { "PageDown", Key::PageDown },
                { "Left", Key::Left }, { "Up", Key::Up }, { "Right", Key::Right }, { "Down", Key::Down },
                { "LeftShift", Key::LeftShift }, { "RightShift", Key::RightShift },
                { "LeftControl", Key::LeftControl }, { "RightControl", Key::RightControl },
                { "LeftAlt", Key::LeftAlt }, { "RightAlt", Key::RightAlt },
                { "Minus", Key::Minus }, { "Equal", Key::Equal }, { "LeftBracket", Key::LeftBracket }, { "RightBracket", Key::RightBracket },
                { "Backslash", Key::Backslash }, { "Semicolon", Key::Semicolon }, { "Apostrophe", Key::Apostrophe },
                { "Comma", Key::Comma }, { "Period", Key::Period }, { "Slash", Key::Slash }, { "Grave", Key::Grave },
            };
            return namedValue(name, kKeys, static_cast<int>(sizeof(kKeys) / sizeof(kKeys[0])), out);
        }

        bool buttonFromName(std::string_view name, GamepadButton& out)
        {
            static const std::pair<const char*, GamepadButton> kButtons[] = {
                { "A", GamepadButton::A }, { "B", GamepadButton::B }, { "X", GamepadButton::X }, { "Y", GamepadButton::Y },
                { "LeftShoulder", GamepadButton::LeftShoulder }, { "RightShoulder", GamepadButton::RightShoulder },
                { "Back", GamepadButton::Back }, { "Start", GamepadButton::Start },
                { "LeftThumb", GamepadButton::LeftThumb }, { "RightThumb", GamepadButton::RightThumb },
                { "DPadUp", GamepadButton::DPadUp }, { "DPadDown", GamepadButton::DPadDown },
                { "DPadLeft", GamepadButton::DPadLeft }, { "DPadRight", GamepadButton::DPadRight },
            };
            return namedValue(name, kButtons, static_cast<int>(sizeof(kButtons) / sizeof(kButtons[0])), out);
        }

        bool axisFromName(std::string_view name, GamepadAxis& out)
        {
            static const std::pair<const char*, GamepadAxis> kAxes[] = {
                { "LeftX", GamepadAxis::LeftX }, { "LeftY", GamepadAxis::LeftY },
                { "RightX", GamepadAxis::RightX }, { "RightY", GamepadAxis::RightY },
                { "LeftTrigger", GamepadAxis::LeftTrigger }, { "RightTrigger", GamepadAxis::RightTrigger },
            };
            return namedValue(name, kAxes, static_cast<int>(sizeof(kAxes) / sizeof(kAxes[0])), out);
        }

        bool mouseFromName(std::string_view name, MouseButton& out)
        {
            static const std::pair<const char*, MouseButton> kMouse[] = {
                { "Left", MouseButton::Left }, { "Right", MouseButton::Right }, { "Middle", MouseButton::Middle },
            };
            return namedValue(name, kMouse, static_cast<int>(sizeof(kMouse) / sizeof(kMouse[0])), out);
        }

        bool appMatches(const EntityInputBinding& row, std::string_view app)
        {
            return row.app.empty() || sameText(row.app, app);
        }

        int deviceCount(const EntityInputBinding& row)
        {
            return (row.key.empty() ? 0 : 1) + (row.button.empty() ? 0 : 1) + (row.axis.empty() ? 0 : 1) + (row.mouse.empty() ? 0 : 1);
        }

        bool alreadyCleared(const std::vector<std::string>& names, const std::string& action)
        {
            for (const std::string& name : names)
            {
                if (name == action)
                    return true;
            }
            return false;
        }

        bool readBinding(const json& row, EntityInputBinding& out)
        {
            EntityInputBinding parsed;
            if (!readString(row, "action", parsed.action) || parsed.action.empty())
            {
                DE_LOG_WARN(LogCategory::Input, "EntityInput: binding missing action");
                return false;
            }
            readString(row, "app", parsed.app);
            readString(row, "key", parsed.key);
            readString(row, "button", parsed.button);
            readString(row, "axis", parsed.axis);
            readString(row, "mouse", parsed.mouse);
            if (deviceCount(parsed) != 1)
            {
                DE_LOG_WARN(LogCategory::Input, "EntityInput: '{}' needs one of key, button, axis, or mouse", parsed.action);
                return false;
            }
            if (const auto it = row.find("scale"); it != row.end())
            {
                float scale = 1.0f;
                if (!jsonToFloat(*it, scale) || !std::isfinite(scale))
                {
                    DE_LOG_WARN(LogCategory::Input, "EntityInput: ignoring scale on '{}'", parsed.action);
                    return false;
                }
                parsed.scale = scale;
            }
            if (const auto it = row.find("threshold"); it != row.end())
            {
                float threshold = 0.0f;
                if (!jsonToFloat(*it, threshold) || !std::isfinite(threshold) || threshold < 0.0f)
                {
                    DE_LOG_WARN(LogCategory::Input, "EntityInput: ignoring threshold on '{}'", parsed.action);
                    return false;
                }
                parsed.threshold = threshold;
            }
            if (const auto it = row.find("pad"); it != row.end())
            {
                int pad = 0;
                if (!jsonToInt(*it, pad) || pad < 0 || pad >= kMaxGamepads)
                {
                    DE_LOG_WARN(LogCategory::Input, "EntityInput: ignoring pad on '{}'", parsed.action);
                    return false;
                }
                parsed.pad = pad;
            }
            if (!parsed.app.empty() && !sameText(parsed.app, "sandbox") && !sameText(parsed.app, "editor"))
            {
                DE_LOG_WARN(LogCategory::Input, "EntityInput: unknown app '{}' on '{}'", parsed.app, parsed.action);
                return false;
            }
            out = std::move(parsed);
            return true;
        }

        bool deviceKnown(const EntityInputBinding& row)
        {
            Key           key    = Key::Unknown;
            GamepadButton button = GamepadButton::A;
            GamepadAxis   axis   = GamepadAxis::LeftX;
            MouseButton   mouse  = MouseButton::Left;
            if (!row.key.empty())
                return keyFromName(row.key, key);
            if (!row.button.empty())
                return buttonFromName(row.button, button);
            if (!row.axis.empty())
                return axisFromName(row.axis, axis);
            return mouseFromName(row.mouse, mouse);
        }

        bool bindRow(ActionMap& actions, const EntityInputBinding& row)
        {
            if (!row.key.empty())
            {
                Key key = Key::Unknown;
                if (!keyFromName(row.key, key))
                {
                    DE_LOG_WARN(LogCategory::Input, "EntityInput: unknown key '{}' on '{}'", row.key, row.action);
                    return false;
                }
                actions.bindKeyAsAxis(row.action, key, row.scale);
                return true;
            }
            if (!row.button.empty())
            {
                GamepadButton button = GamepadButton::A;
                if (!buttonFromName(row.button, button))
                {
                    DE_LOG_WARN(LogCategory::Input, "EntityInput: unknown button '{}' on '{}'", row.button, row.action);
                    return false;
                }
                actions.bindButtonAsAxis(row.action, button, row.scale, row.pad);
                return true;
            }
            if (!row.axis.empty())
            {
                GamepadAxis axis = GamepadAxis::LeftX;
                if (!axisFromName(row.axis, axis))
                {
                    DE_LOG_WARN(LogCategory::Input, "EntityInput: unknown axis '{}' on '{}'", row.axis, row.action);
                    return false;
                }
                if (row.threshold > 0.0f)
                    actions.bindAxisHeld(row.action, axis, row.threshold, row.scale, row.pad);
                else
                    actions.bindAxis(row.action, axis, row.scale, row.pad);
                return true;
            }
            MouseButton mouse = MouseButton::Left;
            if (!mouseFromName(row.mouse, mouse))
            {
                DE_LOG_WARN(LogCategory::Input, "EntityInput: unknown mouse '{}' on '{}'", row.mouse, row.action);
                return false;
            }
            actions.bindMouse(row.action, mouse);
            return true;
        }
    } // namespace

    std::string entityInputVirtualPath(std::string_view typeName)
    {
        return std::string("entities/") + std::string(typeName) + ".entity.input.json";
    }

    bool parseEntityInput(std::string_view jsonText, std::vector<EntityInputBinding>& out)
    {
        if (jsonText.empty() || jsonText.size() > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::Input, "EntityInput: empty or oversized json");
            return false;
        }
        const json root = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            DE_LOG_ERROR(LogCategory::Input, "EntityInput: discarded or non-object json");
            return false;
        }

        const auto verIt = root.find("version");
        if (verIt != root.end())
        {
            float ver = 0.0f;
            if (!jsonToFloat(*verIt, ver) || static_cast<int>(ver) != 1)
            {
                DE_LOG_ERROR(LogCategory::Input, "EntityInput: unsupported version");
                return false;
            }
        }

        std::string type;
        if (!readString(root, "type", type) || type != "player")
        {
            DE_LOG_ERROR(LogCategory::Input, "EntityInput: type must be player");
            return false;
        }

        const auto list = root.find("bindings");
        if (list == root.end() || !list->is_array())
        {
            DE_LOG_ERROR(LogCategory::Input, "EntityInput: missing bindings");
            return false;
        }

        std::vector<EntityInputBinding> parsed;
        parsed.reserve(list->size());
        for (const json& row : *list)
        {
            if (!row.is_object())
            {
                DE_LOG_WARN(LogCategory::Input, "EntityInput: skipped a non-object binding");
                continue;
            }
            EntityInputBinding binding;
            if (readBinding(row, binding))
                parsed.push_back(std::move(binding));
        }
        out = std::move(parsed);
        return true;
    }

    bool applyEntityInput(ActionMap& actions, const std::vector<EntityInputBinding>& bindings, std::string_view app)
    {
        std::vector<std::string> cleared;
        int                      applied = 0;
        for (const EntityInputBinding& row : bindings)
        {
            if (!appMatches(row, app))
                continue;
            // Reject an unknown device before clearAction, so a bad row cannot wipe a good binding.
            if (!deviceKnown(row))
            {
                DE_LOG_WARN(LogCategory::Input, "EntityInput: unknown device on '{}'", row.action);
                continue;
            }
            const std::string action = lowerCopy(row.action);
            if (!alreadyCleared(cleared, action))
            {
                actions.clearAction(row.action);
                cleared.push_back(action);
            }
            if (bindRow(actions, row))
                ++applied;
        }
        return applied > 0;
    }

    bool applyPlayerInputFile(ActionMap& actions, std::string_view app)
    {
        const std::filesystem::path path = resolveContentFile(entityInputVirtualPath("player"));
        if (path.empty())
        {
            DE_LOG_ERROR(LogCategory::Input, "EntityInput: missing entities/player.entity.input.json");
            return false;
        }
        std::error_code ec;
        const auto      sz = std::filesystem::file_size(path, ec);
        if (ec || sz > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::Input, "EntityInput: unreadable or too large '{}'", path.string());
            return false;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        std::vector<EntityInputBinding> bindings;
        if (!parseEntityInput(ss.str(), bindings))
            return false;
        if (!applyEntityInput(actions, bindings, app))
        {
            DE_LOG_ERROR(LogCategory::Input, "EntityInput: no bindings for '{}'", app);
            return false;
        }
        DE_LOG_INFO(LogCategory::Input, "EntityInput: loaded {} player bindings for {}", static_cast<int>(bindings.size()), app);
        return true;
    }

} // namespace Dark
