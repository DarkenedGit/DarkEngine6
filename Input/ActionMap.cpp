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

    void ActionMap::bindKeyAsAxis(std::string_view action, Key key, float scale)
    {
        Binding b{};
        b.type  = BindingType::Key;
        b.code  = static_cast<uint16_t>(key);
        b.scale = scale;
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

    void ActionMap::bindButtonAsAxis(std::string_view action, GamepadButton button, float scale, int padIndex)
    {
        Binding b{};
        b.type  = BindingType::GamepadButton;
        b.code  = static_cast<uint16_t>(button);
        b.pad   = static_cast<int8_t>(padIndex);
        b.scale = scale;
        ensure(action).push_back(b);
    }

    void ActionMap::bindAxis(std::string_view action, GamepadAxis axis, float scale, int padIndex)
    {
        Binding b{};
        b.type  = BindingType::GamepadAxis;
        b.code  = static_cast<uint16_t>(axis);
        b.pad   = static_cast<int8_t>(padIndex);
        b.scale = scale;
        ensure(action).push_back(b);
    }

    const std::vector<ActionMap::Binding>* ActionMap::bindings(std::string_view action) const
    {
        const auto it = m_map.find(normalize(action));
        if (it == m_map.end())
            return nullptr;
        return &it->second;
    }

    bool ActionMap::down(const Input& input, std::string_view action) const
    {
        const auto* list = bindings(action);
        if (!list)
            return false;

        for (const Binding& b : *list)
        {
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
            if (b.type == BindingType::GamepadAxis)
            {
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

} // namespace Dark
