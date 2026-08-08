#pragma once

#include <cstdint>

namespace Dark
{

// Keyboard keys use Win32 virtual-key values where they match (A-Z, 0-9, etc.).
// Prefer these names in game code instead of raw VK_* constants.
enum class Key : uint16_t
{
    Unknown = 0,

    // Letters (same as 'A'..'Z')
    A = 'A', B = 'B', C = 'C', D = 'D', E = 'E', F = 'F', G = 'G',
    H = 'H', I = 'I', J = 'J', K = 'K', L = 'L', M = 'M', N = 'N',
    O = 'O', P = 'P', Q = 'Q', R = 'R', S = 'S', T = 'T', U = 'U',
    V = 'V', W = 'W', X = 'X', Y = 'Y', Z = 'Z',

    // Digits
    Digit0 = '0', Digit1 = '1', Digit2 = '2', Digit3 = '3', Digit4 = '4',
    Digit5 = '5', Digit6 = '6', Digit7 = '7', Digit8 = '8', Digit9 = '9',

    // Function keys (VK_F1 = 0x70 ...)
    F1 = 0x70, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    Escape    = 0x1B,
    Tab       = 0x09,
    CapsLock  = 0x14,
    Space     = 0x20,
    Enter     = 0x0D,
    Backspace = 0x08,
    Delete    = 0x2E,
    Insert    = 0x2D,
    Home      = 0x24,
    End       = 0x23,
    PageUp    = 0x21,
    PageDown  = 0x22,

    Left  = 0x25,
    Up    = 0x26,
    Right = 0x27,
    Down  = 0x28,

    LeftShift    = 0xA0,
    RightShift   = 0xA1,
    LeftControl  = 0xA2,
    RightControl = 0xA3,
    LeftAlt      = 0xA4,
    RightAlt     = 0xA5,

    // OEM
    Minus      = 0xBD,
    Equal      = 0xBB,
    LeftBracket  = 0xDB,
    RightBracket = 0xDD,
    Backslash  = 0xDC,
    Semicolon  = 0xBA,
    Apostrophe = 0xDE,
    Comma      = 0xBC,
    Period     = 0xBE,
    Slash      = 0xBF,
    Grave      = 0xC0,
};

// Xbox-layout gamepad buttons (XInput).
enum class GamepadButton : uint8_t
{
    A = 0,
    B,
    X,
    Y,
    LeftShoulder,
    RightShoulder,
    Back,   // View / Select
    Start,  // Menu
    LeftThumb,
    RightThumb,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Count
};

enum class GamepadAxis : uint8_t
{
    LeftX = 0,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count
};

enum class MouseButton : uint8_t
{
    Left = 0,
    Right,
    Middle,
    Count
};

inline constexpr int kMaxGamepads = 4;
inline constexpr uint16_t kMaxKeyCode = 256;

} // namespace Dark
