#include "Input/ActionMap.h"
#include "Input/Input.h"

#include <algorithm>
#include <cctype>

namespace Dark
{

    std::string ActionMap::normalize(std::string_view name)
    {
        std::string s(name);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::vector<ActionMap::Binding>& ActionMap::ensure(std::string_view action)
    {
        return m_map[normalize(action)];
    }

    void ActionMap::clear()
    {
        m_map.clear();
    }

    void ActionMap::clearAction(std::string_view name)
    {
        m_map.erase(normalize(name));
    }

    void ActionMap::bindKey(std::string_view action, Key key)
    {
        Binding b{};
        b.type  = BindingType::Key;
        b.code  = static_cast<uint16_t>(key);
        b.scale = 1.0f;
        ensure(action).push_back(b);
    }

    void ActionMap::bindKeyAsAxis(std::string_view action, Key key, float scale, uint8_t mods)
    {
        Binding b{};
        b.type  = BindingType::Key;
        b.code  = static_cast<uint16_t>(key);
        b.scale = scale;
        b.mods  = mods;
        ensure(action).push_back(b);
    }

    void ActionMap::bindButton(std::string_view action, GamepadButton button, int padIndex)
    {
        Binding b{};
        b.type  = BindingType::GamepadButton;
        b.code  = static_cast<uint16_t>(button);
        b.pad   = static_cast<int8_t>(padIndex);
        b.scale = 1.0f;
        ensure(action).push_back(b);
    }

    void ActionMap::bindButtonAsAxis(std::string_view action, GamepadButton button, float scale, int padIndex, uint8_t mods)
    {
        Binding b{};
        b.type  = BindingType::GamepadButton;
        b.code  = static_cast<uint16_t>(button);
        b.pad   = static_cast<int8_t>(padIndex);
        b.scale = scale;
        b.mods  = mods;
        ensure(action).push_back(b);
    }

    void ActionMap::bindAxis(std::string_view action, GamepadAxis axis, float scale, int padIndex, uint8_t mods)
    {
        Binding b{};
        b.type  = BindingType::GamepadAxis;
        b.code  = static_cast<uint16_t>(axis);
        b.pad   = static_cast<int8_t>(padIndex);
        b.scale = scale;
        b.mods  = mods;
        ensure(action).push_back(b);
    }

    void ActionMap::bindAxisHeld(std::string_view action, GamepadAxis axis, float threshold, float scale, int padIndex, uint8_t mods)
    {
        Binding b{};
        b.type      = BindingType::GamepadAxis;
        b.code      = static_cast<uint16_t>(axis);
        b.pad       = static_cast<int8_t>(padIndex);
        b.scale     = scale;
        b.threshold = threshold;
        b.mods      = mods;
        ensure(action).push_back(b);
    }

    void ActionMap::bindMouse(std::string_view action, MouseButton button, uint8_t mods)
    {
        Binding b{};
        b.type  = BindingType::MouseButton;
        b.code  = static_cast<uint16_t>(button);
        b.scale = 1.0f;
        b.mods  = mods;
        ensure(action).push_back(b);
    }

    namespace
    {
        bool bindingModsHeld(const Input& input, uint8_t mods)
        {
            if (mods == 0)
                return true;
            if ((mods & ActionMap::ModCtrl) != 0 && !input.keyDown(Key::LeftControl) && !input.keyDown(Key::RightControl))
                return false;
            if ((mods & ActionMap::ModShift) != 0 && !input.keyDown(Key::LeftShift) && !input.keyDown(Key::RightShift))
                return false;
            if ((mods & ActionMap::ModAlt) != 0 && !input.keyDown(Key::LeftAlt) && !input.keyDown(Key::RightAlt))
                return false;
            return true;
        }
    }

    const std::vector<ActionMap::Binding>* ActionMap::bindings(std::string_view action) const
    {
        const auto it = m_map.find(normalize(action));
        if (it == m_map.end())
            return nullptr;
        return &it->second;
    }

    namespace
    {

        std::string keyGlyph(Key key)
        {
            const uint16_t code = static_cast<uint16_t>(key);
            if (code >= static_cast<uint16_t>('A') && code <= static_cast<uint16_t>('Z'))
                return std::string(1, static_cast<char>(code));
            if (code >= static_cast<uint16_t>('0') && code <= static_cast<uint16_t>('9'))
                return std::string(1, static_cast<char>(code));
            if (code >= static_cast<uint16_t>(Key::F1) && code <= static_cast<uint16_t>(Key::F12))
                return "F" + std::to_string(static_cast<int>(code - static_cast<uint16_t>(Key::F1) + 1));
            switch (key)
            {
            case Key::Space:         return "Space";
            case Key::Escape:        return "Esc";
            case Key::Enter:         return "Enter";
            case Key::Tab:           return "Tab";
            case Key::LeftShift:
            case Key::RightShift:    return "Shift";
            case Key::LeftControl:
            case Key::RightControl:  return "Ctrl";
            case Key::LeftAlt:
            case Key::RightAlt:      return "Alt";
            default:                 return {};
            }
        }

    } // namespace

    std::string ActionMap::keyBracket(std::string_view action, std::string_view fallback) const
    {
        const std::vector<Binding>* list = bindings(action);
        if (!list)
            return std::string(fallback);
        for (const Binding& binding : *list)
        {
            if (binding.type != BindingType::Key)
                continue;
            const std::string glyph = keyGlyph(static_cast<Key>(binding.code));
            if (glyph.empty())
                break;
            return "[" + glyph + "]";
        }
        return std::string(fallback);
    }

    bool ActionMap::down(const Input& input, std::string_view action) const
    {
        const auto* list = bindings(action);
        if (!list)
            return false;

        for (const Binding& b : *list)
        {
            if (!bindingModsHeld(input, b.mods))
                continue;
            if (b.type == BindingType::Key)
            {
                if (input.keyDown(b.code))
                    return true;
            }
            else if (b.type == BindingType::GamepadButton)
            {
                if (input.buttonDown(static_cast<GamepadButton>(b.code), b.pad))
                    return true;
            }
        }
        return false;
    }

    bool ActionMap::pressed(const Input& input, std::string_view action) const
    {
        const auto* list = bindings(action);
        if (!list)
            return false;

        for (const Binding& b : *list)
        {
            if (!bindingModsHeld(input, b.mods))
                continue;
            if (b.type == BindingType::Key)
            {
                if (input.keyPressed(b.code))
                    return true;
            }
            else if (b.type == BindingType::GamepadButton)
            {
                if (input.buttonPressed(static_cast<GamepadButton>(b.code), b.pad))
                    return true;
            }
        }
        return false;
    }

    bool ActionMap::released(const Input& input, std::string_view action) const
    {
        const auto* list = bindings(action);
        if (!list)
            return false;

        for (const Binding& b : *list)
        {
            if (!bindingModsHeld(input, b.mods))
                continue;
            if (b.type == BindingType::Key)
            {
                if (input.keyReleased(b.code))
                    return true;
            }
            else if (b.type == BindingType::GamepadButton)
            {
                if (input.buttonReleased(static_cast<GamepadButton>(b.code), b.pad))
                    return true;
            }
        }
        return false;
    }

    float ActionMap::axis(const Input& input, std::string_view action) const
    {
        const auto* list = bindings(action);
        if (!list)
            return 0.0f;

        float sum = 0.0f;
        for (const Binding& b : *list)
        {
            if (!bindingModsHeld(input, b.mods))
                continue;
            if (b.type == BindingType::GamepadAxis)
            {
                if (b.threshold > 0.0f)
                    continue;
                sum += input.axis(static_cast<GamepadAxis>(b.code), b.pad) * b.scale;
            }
            else if (b.type == BindingType::Key)
            {
                if (input.keyDown(b.code))
                    sum += b.scale;
            }
            else if (b.type == BindingType::GamepadButton)
            {
                if (input.buttonDown(static_cast<GamepadButton>(b.code), b.pad))
                    sum += b.scale;
            }
        }

        // Clamp typical move axes to [-1, 1]
        if (sum > 1.0f)
            sum = 1.0f;
        if (sum < -1.0f)
            sum = -1.0f;
        return sum;
    }

    bool ActionMap::mouseDown(const Input& input, std::string_view action) const
    {
        const auto* list = bindings(action);
        if (!list)
            return false;
        for (const Binding& b : *list)
        {
            if (!bindingModsHeld(input, b.mods))
                continue;
            if (b.type == BindingType::MouseButton && input.mouseDown(static_cast<MouseButton>(b.code)))
                return true;
        }
        return false;
    }

    bool ActionMap::mousePressed(const Input& input, std::string_view action) const
    {
        const auto* list = bindings(action);
        if (!list)
            return false;
        for (const Binding& b : *list)
        {
            if (!bindingModsHeld(input, b.mods))
                continue;
            if (b.type == BindingType::MouseButton && input.mousePressed(static_cast<MouseButton>(b.code)))
                return true;
        }
        return false;
    }

    bool ActionMap::axisHeld(const Input& input, std::string_view action) const
    {
        const auto* list = bindings(action);
        if (!list)
            return false;
        for (const Binding& b : *list)
        {
            if (!bindingModsHeld(input, b.mods))
                continue;
            if (b.type != BindingType::GamepadAxis || !(b.threshold > 0.0f))
                continue;
            const float v = input.axis(static_cast<GamepadAxis>(b.code), b.pad) * b.scale;
            if (v > b.threshold)
                return true;
        }
        return false;
    }

} // namespace Dark
