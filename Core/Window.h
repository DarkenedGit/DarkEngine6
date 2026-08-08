#pragma once
#include <cstdint>

struct HWND__;

namespace Dark
{

class Input;

// Optional early message hook (e.g. ImGui). Return true if the message was handled.
using WindowMessageHook = bool (*)(void* hwnd, unsigned msg, unsigned long long wParam, long long lParam, void* user);

class Window
{
public:
    Window(const char* title, uint32_t width, uint32_t height);
    ~Window();

    bool     shouldClose() const;
    void     pollEvents();
    float    getTime() const; // seconds since window creation

    uint32_t width()  const { return m_width; }
    uint32_t height() const { return m_height; }
    void*    nativeHandle() const; // HWND on Win32

    // Keyboard / focus events are forwarded to this Input (optional).
    void setInput(Input* input) { m_input = input; }
    Input* input() const { return m_input; }

    void setMessageHook(WindowMessageHook hook, void* user = nullptr)
    {
        m_msgHook = hook;
        m_msgHookUser = user;
    }

private:
    static long long __stdcall wndProc(HWND__* hwnd, unsigned msg, unsigned long long wp, long long lp);

    HWND__*  m_hwnd   = nullptr;
    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    bool     m_closed = false;
    Input*   m_input  = nullptr;

    WindowMessageHook m_msgHook     = nullptr;
    void*             m_msgHookUser = nullptr;
};

} // namespace Dark

