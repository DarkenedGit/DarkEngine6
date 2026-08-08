#pragma once

#include "Input/InputCodes.h"
#include "Input/ActionMap.h"

#include <cstdint>

namespace Dark
{

// Polled keyboard + XInput gamepad state with edge detection (pressed / released).
//
// Usage each frame (Application does this):
//   input.beginFrame();
//   window.pollEvents();   // feeds key events
//   input.updateDevices(); // polls gamepads
//   // then read down/pressed/action*
class Input
{
public:
    Input() = default;

    // Clear one-frame edges. Call once at the start of each frame.
    void beginFrame();

    // Poll XInput pads. Call after beginFrame + window event pump.
    void updateDevices();

    // ── Keyboard (fed by Window WndProc) ─────────────────────────────────────
    void onKeyDown(uint16_t vkCode, bool repeat);
    void onKeyUp(uint16_t vkCode);
    void onFocusLost(); // release all keys (Alt-Tab, click away)

    bool keyDown(Key key) const;
    bool keyPressed(Key key) const;   // edge: transitioned to down this frame
    bool keyReleased(Key key) const;  // edge: transitioned to up this frame

    bool keyDown(uint16_t vkCode) const;
    bool keyPressed(uint16_t vkCode) const;
    bool keyReleased(uint16_t vkCode) const;

    // ── Mouse (fed by Window WndProc) ────────────────────────────────────────
    void onMouseMove(int x, int y);
    void onMouseButton(MouseButton button, bool down);
    void onMouseWheel(float delta); // +1 typically per notch up

    int   mouseX() const { return m_mouseX; }
    int   mouseY() const { return m_mouseY; }
    int   mouseDeltaX() const { return m_mouseDeltaX; }
    int   mouseDeltaY() const { return m_mouseDeltaY; }
    float mouseWheel() const { return m_mouseWheel; }

    bool mouseDown(MouseButton b) const;
    bool mousePressed(MouseButton b) const;
    bool mouseReleased(MouseButton b) const;

    // ── Gamepad ──────────────────────────────────────────────────────────────
    bool gamepadConnected(int padIndex = 0) const;

    bool buttonDown(GamepadButton button, int padIndex = 0) const;
    bool buttonPressed(GamepadButton button, int padIndex = 0) const;
    bool buttonReleased(GamepadButton button, int padIndex = 0) const;

    // Axes in [-1, 1] for sticks, [0, 1] for triggers. Deadzone applied to sticks.
    float axis(GamepadAxis axis, int padIndex = 0) const;

    // ── Named actions / runtime commands ─────────────────────────────────────
    ActionMap&       actions()       { return m_actions; }
    const ActionMap& actions() const { return m_actions; }

    bool actionDown(const char* name) const;
    bool actionPressed(const char* name) const;
    bool actionReleased(const char* name) const;

    // Combined 2D stick from an action pair of axes, or keyboard fallbacks
    // registered on the ActionMap (optional helpers for camera/move).
    float actionAxis(const char* name) const;

private:
    struct DigitalState
    {
        bool down     = false;
        bool pressed  = false;
        bool released = false;
    };

    struct PadState
    {
        bool        connected = false;
        DigitalState buttons[static_cast<int>(GamepadButton::Count)]{};
        float       axes[static_cast<int>(GamepadAxis::Count)]{};
    };

    DigitalState m_keys[kMaxKeyCode]{};
    PadState     m_pads[kMaxGamepads]{};
    ActionMap    m_actions;

    DigitalState m_mouseButtons[static_cast<int>(MouseButton::Count)]{};
    int   m_mouseX = 0;
    int   m_mouseY = 0;
    int   m_mouseDeltaX = 0;
    int   m_mouseDeltaY = 0;
    float m_mouseWheel = 0.0f;
    bool  m_mousePosValid = false;

    static float applyStickDeadzone(float v, float deadzone);
};

} // namespace Dark
