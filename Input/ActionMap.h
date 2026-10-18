#pragma once

#include "Input/InputCodes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Dark
{

    class Input;

    // Maps named runtime commands ("quit", "pause", "camera_yaw") to
    // one or more keyboard keys and/or gamepad buttons/axes.
    class ActionMap
    {
    public:
        enum class BindingType : uint8_t
        {
            Key,
            GamepadButton,
            GamepadAxis,
            MouseButton,
        };

        struct Binding
        {
            BindingType type      = BindingType::Key;
            uint16_t    code      = 0;    // Key vk, GamepadButton, GamepadAxis, or MouseButton
            int8_t      pad       = 0;    // gamepad index
            float       scale     = 1.0f; // for axes and digital-as-axis (use -1 to invert)
            float       threshold = 0.0f; // axis is a hold when this is > 0; not part of actionAxis
        };

        void clear();
        void clearAction(std::string_view name);

        void bindKey(std::string_view action, Key key);
        void bindButton(std::string_view action, GamepadButton button, int padIndex = 0);
        // Axis binding: contribution = deviceAxis * scale (use -1 to invert).
        void bindAxis(std::string_view action, GamepadAxis axis, float scale = 1.0f, int padIndex = 0);
        // Held while the axis, multiplied by scale, is greater than threshold. Omitted from actionAxis.
        void bindAxisHeld(std::string_view action, GamepadAxis axis, float threshold, float scale = 1.0f, int padIndex = 0);
        void bindMouse(std::string_view action, Dark::MouseButton button);
        // Digital → axis while held (keyboard or gamepad button).
        void bindKeyAsAxis(std::string_view action, Key key, float scale);
        void bindButtonAsAxis(std::string_view action, GamepadButton button, float scale, int padIndex = 0);

        bool down(const Input& input, std::string_view action) const;
        bool pressed(const Input& input, std::string_view action) const;
        bool released(const Input& input, std::string_view action) const;

        // Mouse and axis-holds are separate so a caller can ignore keys while a panel has the keyboard.
        bool mouseDown(const Input& input, std::string_view action) const;
        bool mousePressed(const Input& input, std::string_view action) const;
        bool axisHeld(const Input& input, std::string_view action) const;

        // Sum of bound axes / digital-as-axis contributors, clamped to [-1, 1].
        float axis(const Input& input, std::string_view action) const;

        const std::vector<Binding>* bindings(std::string_view action) const;

    private:
        std::unordered_map<std::string, std::vector<Binding>> m_map;

        static std::string    normalize(std::string_view name);
        std::vector<Binding>& ensure(std::string_view action);
    };

} // namespace Dark
