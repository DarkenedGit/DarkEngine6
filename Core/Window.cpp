#include "Core/Window.h"
#include "Core/Log.h"
#include "Input/Input.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <chrono>

namespace Dark
{
namespace {

auto g_startTime = std::chrono::steady_clock::now();

Window* windowFromHwnd(HWND hwnd)
{
    return reinterpret_cast<Window*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
}

} // namespace

// static
long long __stdcall Window::wndProc(HWND__* hwndRaw, unsigned msg, unsigned long long wp, long long lp)
{
    HWND hwnd = reinterpret_cast<HWND>(hwndRaw);

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lp);
        auto* self = static_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = reinterpret_cast<HWND__*>(hwnd);
        return TRUE;
    }

    Window* self = windowFromHwnd(hwnd);
    Input* input = self ? self->m_input : nullptr;

    if (self && self->m_msgHook)
    {
        if (self->m_msgHook(hwnd, msg, wp, lp, self->m_msgHookUser))
            return 1; // handled (ImGui)
    }

    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_CLOSE:
        if (self)
            self->m_closed = true;
        DestroyWindow(hwnd);
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (input)
        {
            const uint16_t vk = static_cast<uint16_t>(wp & 0xFFFF);
            const bool repeat = (lp & (1LL << 30)) != 0;
            input->onKeyDown(vk, repeat);
        }
        return 0;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (input)
        {
            const uint16_t vk = static_cast<uint16_t>(wp & 0xFFFF);
            input->onKeyUp(vk);
        }
        return 0;

    case WM_KILLFOCUS:
        if (input)
            input->onFocusLost();
        return 0;

    case WM_MOUSEMOVE:
        if (input)
        {
            const int x = static_cast<short>(lp & 0xFFFF);
            const int y = static_cast<short>((lp >> 16) & 0xFFFF);
            input->onMouseMove(x, y);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (input)
        {
            input->onMouseButton(MouseButton::Left, true);
            SetCapture(hwnd);
        }
        return 0;
    case WM_LBUTTONUP:
        if (input)
        {
            input->onMouseButton(MouseButton::Left, false);
            ReleaseCapture();
        }
        return 0;
    case WM_RBUTTONDOWN:
        if (input)
            input->onMouseButton(MouseButton::Right, true);
        return 0;
    case WM_RBUTTONUP:
        if (input)
            input->onMouseButton(MouseButton::Right, false);
        return 0;
    case WM_MBUTTONDOWN:
        if (input)
            input->onMouseButton(MouseButton::Middle, true);
        return 0;
    case WM_MBUTTONUP:
        if (input)
            input->onMouseButton(MouseButton::Middle, false);
        return 0;

    case WM_MOUSEWHEEL:
        if (input)
        {
            const short delta = static_cast<short>((wp >> 16) & 0xFFFF);
            input->onMouseWheel(static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA));
        }
        return 0;

    default:
        break;
    }

    return DefWindowProcA(hwnd, msg, static_cast<WPARAM>(wp), static_cast<LPARAM>(lp));
}

Window::Window(const char* title, uint32_t width, uint32_t height)
    : m_width(width), m_height(height)
{
    WNDCLASSEXA wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = reinterpret_cast<WNDPROC>(&Window::wndProc);
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "DarkEngine6WndClass";
    RegisterClassExA(&wc);

    RECT rect{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        wc.hInstance,
        this // CREATESTRUCT.lpCreateParams → GWLP_USERDATA
    );

    if (!hwnd)
    {
        DE_LOG_ERROR("Window: CreateWindowEx failed ({})", GetLastError());
        m_closed = true;
        return;
    }

    m_hwnd = reinterpret_cast<HWND__*>(hwnd);
    DE_LOG_INFO("Window created: {}x{}", width, height);
}

Window::~Window()
{
    if (m_hwnd)
    {
        DestroyWindow(reinterpret_cast<HWND>(m_hwnd));
        m_hwnd = nullptr;
    }
}

bool Window::shouldClose() const
{
    return m_closed;
}

void Window::pollEvents()
{
    MSG msg{};
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            m_closed = true;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

float Window::getTime() const
{
    using namespace std::chrono;
    auto elapsed = steady_clock::now() - g_startTime;
    return duration<float>(elapsed).count();
}

void* Window::nativeHandle() const
{
    return m_hwnd;
}

} // namespace Dark
