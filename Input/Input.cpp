#include "Input/Input.h"
#include "Core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Xinput.h>

#include <cmath>

namespace Dark
{
        constexpr float kStickDeadzone    = 0.2f;
        constexpr float kTriggerThreshold = 0.1f; // unused for digital; triggers are analog

        WORD xinputMask(GamepadButton b)
        {
            switch (b)
            {
            case GamepadButton::A:
                return XINPUT_GAMEPAD_A;
            case GamepadButton::B:
                return XINPUT_GAMEPAD_B;
            case GamepadButton::X:
                return XINPUT_GAMEPAD_X;
            case GamepadButton::Y:
                return XINPUT_GAMEPAD_Y;
            case GamepadButton::LeftShoulder:
                return XINPUT_GAMEPAD_LEFT_SHOULDER;
            case GamepadButton::RightShoulder:
                return XINPUT_GAMEPAD_RIGHT_SHOULDER;
            case GamepadButton::Back:
                return XINPUT_GAMEPAD_BACK;
            case GamepadButton::Start:
                return XINPUT_GAMEPAD_START;
            case GamepadButton::LeftThumb:
                return XINPUT_GAMEPAD_LEFT_THUMB;
            case GamepadButton::RightThumb:
                return XINPUT_GAMEPAD_RIGHT_THUMB;
            case GamepadButton::DPadUp:
                return XINPUT_GAMEPAD_DPAD_UP;
            case GamepadButton::DPadDown:
                return XINPUT_GAMEPAD_DPAD_DOWN;
            case GamepadButton::DPadLeft:
                return XINPUT_GAMEPAD_DPAD_LEFT;
            case GamepadButton::DPadRight:
                return XINPUT_GAMEPAD_DPAD_RIGHT;
            default:
                return 0;
            }
        }

    float Input::applyStickDeadzone(float v, float deadzone)
    {
        const float a = std::fabs(v);
        if (a < deadzone)
            return 0.0f;
        const float sign = (v < 0.0f) ? -1.0f : 1.0f;
        return sign * ((a - deadzone) / (1.0f - deadzone));
    }

    void Input::applyRadialDeadzone(float& x, float& y, float deadzone)
    {
        const float mag = std::sqrt(x * x + y * y);
        if (mag < deadzone)
        {
            x = 0.0f;
            y = 0.0f;
            return;
        }
        float scaled = (mag - deadzone) / (1.0f - deadzone);
        if (scaled > 1.0f)
            scaled = 1.0f;
        const float inv = scaled / mag;
        x *= inv;
        y *= inv;
    }

    int Input::resolvePad(int padIndex) const
    {
        if (padIndex < 0 || padIndex >= kMaxGamepads)
            return -1;
        if (m_pads[padIndex].connected)
            return padIndex;
        if (padIndex != 0)
            return padIndex;
        for (int i = 1; i < kMaxGamepads; ++i)
        {
            if (m_pads[i].connected)
                return i;
        }
        return 0;
    }

    void Input::beginFrame()
    {
        for (DigitalState& k : m_keys)
        {
            k.pressed  = false;
            k.released = false;
        }

        for (PadState& pad : m_pads)
        {
            for (DigitalState& b : pad.buttons)
            {
                b.pressed  = false;
                b.released = false;
            }
        }

        for (DigitalState& b : m_mouseButtons)
        {
            b.pressed  = false;
            b.released = false;
        }
        m_mouseDeltaX = 0;
        m_mouseDeltaY = 0;
        m_mouseWheel  = 0.0f;
    }

    void Input::onKeyDown(uint16_t vkCode, bool repeat)
    {
        if (vkCode >= kMaxKeyCode)
            return;

        DigitalState& s = m_keys[vkCode];
        if (!s.down && !repeat)
            s.pressed = true;
        s.down = true;
    }

    void Input::onKeyUp(uint16_t vkCode)
    {
        if (vkCode >= kMaxKeyCode)
            return;

        DigitalState& s = m_keys[vkCode];
        if (s.down)
            s.released = true;
        s.down = false;
    }

    void Input::onFocusLost()
    {
        for (uint16_t i = 0; i < kMaxKeyCode; ++i)
        {
            DigitalState& s = m_keys[i];
            if (s.down)
            {
                s.released = true;
                s.down     = false;
            }
        }

        for (DigitalState& s : m_mouseButtons)
        {
            if (s.down)
            {
                s.released = true;
                s.down     = false;
            }
        }
        m_mousePosValid = false;
    }

    void Input::onMouseMove(int x, int y)
    {
        if (m_mousePosValid)
        {
            m_mouseDeltaX += x - m_mouseX;
            // Invert vertical delta: screen Y grows downward; look code expects
            // mouse-up to be the opposite sign of raw Windows delta.
            m_mouseDeltaY += m_mouseY - y;
        }
        m_mouseX        = x;
        m_mouseY        = y;
        m_mousePosValid = true;
    }

    void Input::warpMouse(int x, int y)
    {
        m_mouseX        = x;
        m_mouseY        = y;
        m_mousePosValid = true;
    }

    void Input::onMouseButton(MouseButton button, bool down)
    {
        const int i = static_cast<int>(button);
        if (i < 0 || i >= static_cast<int>(MouseButton::Count))
            return;

        DigitalState& s = m_mouseButtons[i];
        if (down)
        {
            if (!s.down)
                s.pressed = true;
            s.down = true;
        }
        else
        {
            if (s.down)
                s.released = true;
            s.down = false;
        }
    }

    void Input::onMouseWheel(float delta)
    {
        m_mouseWheel += delta;
    }

    bool Input::mouseDown(MouseButton b) const
    {
        const int i = static_cast<int>(b);
        return i >= 0 && i < static_cast<int>(MouseButton::Count) && m_mouseButtons[i].down;
    }

    bool Input::mousePressed(MouseButton b) const
    {
        const int i = static_cast<int>(b);
        return i >= 0 && i < static_cast<int>(MouseButton::Count) && m_mouseButtons[i].pressed;
    }

    bool Input::mouseReleased(MouseButton b) const
    {
        const int i = static_cast<int>(b);
        return i >= 0 && i < static_cast<int>(MouseButton::Count) && m_mouseButtons[i].released;
    }

    bool Input::keyDown(Key key) const
    {
        return keyDown(static_cast<uint16_t>(key));
    }

    bool Input::keyPressed(Key key) const
    {
        return keyPressed(static_cast<uint16_t>(key));
    }

    bool Input::keyReleased(Key key) const
    {
        return keyReleased(static_cast<uint16_t>(key));
    }

    bool Input::keyDown(uint16_t vkCode) const
    {
        return vkCode < kMaxKeyCode && m_keys[vkCode].down;
    }

    bool Input::keyPressed(uint16_t vkCode) const
    {
        return vkCode < kMaxKeyCode && m_keys[vkCode].pressed;
    }

    bool Input::keyReleased(uint16_t vkCode) const
    {
        return vkCode < kMaxKeyCode && m_keys[vkCode].released;
    }

    bool Input::gamepadConnected(int padIndex) const
    {
        const int p = resolvePad(padIndex);
        if (p < 0)
            return false;
        return m_pads[p].connected;
    }

    bool Input::buttonDown(GamepadButton button, int padIndex) const
    {
        const int p = resolvePad(padIndex);
        if (p < 0)
            return false;
        const int i = static_cast<int>(button);
        if (i < 0 || i >= static_cast<int>(GamepadButton::Count))
            return false;
        return m_pads[p].buttons[i].down;
    }

    bool Input::buttonPressed(GamepadButton button, int padIndex) const
    {
        const int p = resolvePad(padIndex);
        if (p < 0)
            return false;
        const int i = static_cast<int>(button);
        if (i < 0 || i >= static_cast<int>(GamepadButton::Count))
            return false;
        return m_pads[p].buttons[i].pressed;
    }

    bool Input::buttonReleased(GamepadButton button, int padIndex) const
    {
        const int p = resolvePad(padIndex);
        if (p < 0)
            return false;
        const int i = static_cast<int>(button);
        if (i < 0 || i >= static_cast<int>(GamepadButton::Count))
            return false;
        return m_pads[p].buttons[i].released;
    }

    float Input::axis(GamepadAxis axisId, int padIndex) const
    {
        const int p = resolvePad(padIndex);
        if (p < 0)
            return 0.0f;
        const int i = static_cast<int>(axisId);
        if (i < 0 || i >= static_cast<int>(GamepadAxis::Count))
            return 0.0f;
        return m_pads[p].axes[i];
    }

    bool Input::actionDown(const char* name) const
    {
        return m_actions.down(*this, name);
    }

    bool Input::actionPressed(const char* name) const
    {
        return m_actions.pressed(*this, name);
    }

    bool Input::actionReleased(const char* name) const
    {
        return m_actions.released(*this, name);
    }

    float Input::actionAxis(const char* name) const
    {
        return m_actions.axis(*this, name);
    }

    void Input::updateDevices()
    {
        ++m_padPollTick;
        for (int p = 0; p < kMaxGamepads; ++p)
        {
            PadState& pad = m_pads[p];
            // Empty XInput slots are expensive; retry disconnected pads every ~0.5s.
            if (!pad.connected && ((m_padPollTick + static_cast<uint32_t>(p)) % 30u) != 0)
                continue;

            XINPUT_STATE xs{};
            const DWORD  result = XInputGetState(static_cast<DWORD>(p), &xs);

            if (result != ERROR_SUCCESS)
            {
                if (pad.connected)
                {
                    DE_LOG_INFO(LogCategory::Input, "Input: gamepad {} disconnected", p);
                    // Release any held buttons
                    for (DigitalState& b : pad.buttons)
                    {
                        if (b.down)
                        {
                            b.released = true;
                            b.down     = false;
                        }
                    }
                    for (float& a : pad.axes)
                        a = 0.0f;
                }
                pad.connected = false;
                continue;
            }

            if (!pad.connected)
                DE_LOG_INFO(LogCategory::Input, "Input: gamepad {} connected", p);
            pad.connected = true;

            const XINPUT_GAMEPAD& g = xs.Gamepad;

            auto updateButton = [](DigitalState& s, bool now)
            {
                if (now && !s.down)
                    s.pressed = true;
                if (!now && s.down)
                    s.released = true;
                s.down = now;
            };

            for (int bi = 0; bi < static_cast<int>(GamepadButton::Count); ++bi)
            {
                const WORD mask = xinputMask(static_cast<GamepadButton>(bi));
                const bool now  = (g.wButtons & mask) != 0;
                updateButton(pad.buttons[bi], now);
            }

            auto normStick = [](SHORT v) {
                float f = static_cast<float>(v) / 32767.0f;
                if (f > 1.0f)
                    f = 1.0f;
                if (f < -1.0f)
                    f = -1.0f;
                return f;
            };
            auto normTrigger = [](BYTE v) { return static_cast<float>(v) / 255.0f; };

            float lx = normStick(g.sThumbLX);
            float ly = normStick(g.sThumbLY);
            float rx = normStick(g.sThumbRX);
            float ry = normStick(g.sThumbRY);
            applyRadialDeadzone(lx, ly, kStickDeadzone);
            applyRadialDeadzone(rx, ry, kStickDeadzone);

            // XInput Y is up-positive; keep that for game code.
            pad.axes[static_cast<int>(GamepadAxis::LeftX)]        = lx;
            pad.axes[static_cast<int>(GamepadAxis::LeftY)]        = ly;
            pad.axes[static_cast<int>(GamepadAxis::RightX)]       = rx;
            pad.axes[static_cast<int>(GamepadAxis::RightY)]       = ry;
            pad.axes[static_cast<int>(GamepadAxis::LeftTrigger)]  = normTrigger(g.bLeftTrigger);
            pad.axes[static_cast<int>(GamepadAxis::RightTrigger)] = normTrigger(g.bRightTrigger);

            (void)kTriggerThreshold;
        }
    }

} // namespace Dark
