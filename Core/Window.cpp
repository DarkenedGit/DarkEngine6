#include "Core/Window.h"
#include "Core/Log.h"
#include "Input/Input.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <chrono>
#include <cmath>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

namespace Dark
{

    auto g_startTime = std::chrono::steady_clock::now();

    Window* windowFromHwnd(HWND hwnd)
    {
        return reinterpret_cast<Window*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    }

    float dpiScaleFromDpi(UINT dpi)
    {
        return (dpi > 0) ? (static_cast<float>(dpi) / 96.0f) : 1.0f;
    }

    float queryMonitorDpiScale(HMONITOR monitor)
    {
        using PFN_GetDpiForMonitor = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        static PFN_GetDpiForMonitor s_getDpiForMonitor = nullptr;
        static bool s_tried = false;
        if (!s_tried)
        {
            s_tried = true;
            if (HMODULE shcore = LoadLibraryA("shcore.dll"))
                s_getDpiForMonitor = reinterpret_cast<PFN_GetDpiForMonitor>(
                    GetProcAddress(shcore, "GetDpiForMonitor"));
        }
        if (s_getDpiForMonitor)
        {
            UINT xdpi = 96, ydpi = 96;
            if (SUCCEEDED(s_getDpiForMonitor(monitor, 0 /* MDT_EFFECTIVE_DPI */, &xdpi, &ydpi)))
                return dpiScaleFromDpi(xdpi);
        }

        HDC dc = GetDC(nullptr);
        const int xdpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if (dc)
            ReleaseDC(nullptr, dc);
        return dpiScaleFromDpi(xdpi > 0 ? static_cast<UINT>(xdpi) : 96);
    }

    float queryHwndDpiScale(HWND hwnd)
    {
        if (HMODULE user32 = GetModuleHandleA("user32.dll"))
        {
            using PFN_GetDpiForWindow = UINT(WINAPI*)(HWND);
            auto* fn = reinterpret_cast<PFN_GetDpiForWindow>(GetProcAddress(user32, "GetDpiForWindow"));
            if (fn)
            {
                const UINT dpi = fn(hwnd);
                if (dpi > 0)
                    return dpiScaleFromDpi(dpi);
            }
        }
        return queryMonitorDpiScale(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
    }

    bool adjustWindowRectForDpi(RECT& rect, DWORD style, UINT dpi)
    {
        if (HMODULE user32 = GetModuleHandleA("user32.dll"))
        {
            using PFN_AdjustWindowRectExForDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
            auto* fn = reinterpret_cast<PFN_AdjustWindowRectExForDpi>(
                GetProcAddress(user32, "AdjustWindowRectExForDpi"));
            if (fn)
                return fn(&rect, style, FALSE, 0, dpi) != FALSE;
        }
        return AdjustWindowRect(&rect, style, FALSE) != FALSE;
    }

    void Window::enableProcessDpiAwareness()
    {
        static bool s_done = false;
        if (s_done)
            return;
        s_done = true;

        if (HMODULE user32 = GetModuleHandleA("user32.dll"))
        {
            using PFN_SetProcessDpiAwarenessContext = BOOL(WINAPI*)(HANDLE);
            auto* setProcess = reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
            if (setProcess && setProcess(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4))))
                return; // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2

            using PFN_SetThreadDpiAwarenessContext = HANDLE(WINAPI*)(HANDLE);
            auto* setThread = reinterpret_cast<PFN_SetThreadDpiAwarenessContext>(
                GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
            if (setThread)
            {
                setThread(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));
                return;
            }
        }

        if (HMODULE shcore = LoadLibraryA("shcore.dll"))
        {
            using PFN_SetProcessDpiAwareness = HRESULT(WINAPI*)(int);
            auto* fn = reinterpret_cast<PFN_SetProcessDpiAwareness>(
                GetProcAddress(shcore, "SetProcessDpiAwareness"));
            if (fn)
            {
                fn(2); // PROCESS_PER_MONITOR_DPI_AWARE
                return;
            }
        }

        if (HMODULE user32 = GetModuleHandleA("user32.dll"))
        {
            using PFN_SetProcessDPIAware = BOOL(WINAPI*)();
            auto* fn = reinterpret_cast<PFN_SetProcessDPIAware>(GetProcAddress(user32, "SetProcessDPIAware"));
            if (fn)
                fn();
        }
    }

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

        case WM_SETFOCUS:
            if (self)
                self->m_focused = true;
            return 0;

        case WM_KILLFOCUS:
            if (self)
                self->m_focused = false;
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

        case WM_SIZE:
            if (self)
            {
                if (wp == SIZE_MINIMIZED)
                    self->m_minimized = true;
                else
                {
                    self->m_minimized = false;
                    const uint32_t w = static_cast<uint32_t>(lp & 0xFFFF);
                    const uint32_t h = static_cast<uint32_t>((lp >> 16) & 0xFFFF);
                    if (w > 0 && h > 0)
                    {
                        if (w != self->m_width || h != self->m_height)
                            self->m_sizeChanged = true;
                        self->m_width  = w;
                        self->m_height = h;
                    }
                }
            }
            return 0;

        case WM_DPICHANGED:
            if (self)
            {
                self->m_dpiScale = dpiScaleFromDpi(static_cast<UINT>(HIWORD(wp)));
                const RECT* suggested = reinterpret_cast<const RECT*>(lp);
                if (suggested)
                {
                    SetWindowPos(
                        hwnd,
                        nullptr,
                        suggested->left,
                        suggested->top,
                        suggested->right - suggested->left,
                        suggested->bottom - suggested->top,
                        SWP_NOZORDER | SWP_NOACTIVATE);
                }
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
        enableProcessDpiAwareness();

        const HMONITOR primary = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        m_dpiScale = queryMonitorDpiScale(primary);
        if (m_dpiScale < 0.5f)
            m_dpiScale = 1.0f;

        // AppConfig width/height are 96-DPI logical pixels. Convert to physical so
        // the swapchain, mouse messages, and ImGui all share one coordinate space.
        const uint32_t physW = static_cast<uint32_t>(std::lround(static_cast<float>(width) * m_dpiScale));
        const uint32_t physH = static_cast<uint32_t>(std::lround(static_cast<float>(height) * m_dpiScale));
        m_width  = physW > 0 ? physW : width;
        m_height = physH > 0 ? physH : height;

        WNDCLASSEXA wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = reinterpret_cast<WNDPROC>(&Window::wndProc);
        wc.hInstance     = GetModuleHandleA(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = "DarkEngine6WndClass";
        RegisterClassExA(&wc);

        const DWORD style = WS_OVERLAPPEDWINDOW;
        RECT rect{ 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
        const UINT dpi = static_cast<UINT>(std::lround(m_dpiScale * 96.0f));
        adjustWindowRectForDpi(rect, style, dpi);

        HWND hwnd = CreateWindowExA(
            0,
            wc.lpszClassName,
            title,
            style | WS_VISIBLE,
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
        m_dpiScale = queryHwndDpiScale(hwnd);
        m_minimized = IsIconic(hwnd) != FALSE;
        m_focused   = (GetForegroundWindow() == hwnd);

        RECT client{};
        if (GetClientRect(hwnd, &client))
        {
            const uint32_t cw = static_cast<uint32_t>(client.right - client.left);
            const uint32_t ch = static_cast<uint32_t>(client.bottom - client.top);
            if (cw > 0 && ch > 0)
            {
                m_width  = cw;
                m_height = ch;
            }
        }
        m_sizeChanged = false;

        DE_LOG_INFO("Window created: {}x{} (dpi scale {:.2f})", m_width, m_height, m_dpiScale);
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
        if (m_cursorCaptured)
            recenterCapturedCursor();
    }

    void Window::setCursorCaptured(bool capture)
    {
        if (m_cursorCaptured == capture)
            return;
        m_cursorCaptured = capture;
        HWND hwnd = reinterpret_cast<HWND>(m_hwnd);
        if (!hwnd)
            return;
        if (capture)
        {
            RECT client{};
            GetClientRect(hwnd, &client);
            POINT tl{ client.left, client.top };
            POINT br{ client.right, client.bottom };
            ClientToScreen(hwnd, &tl);
            ClientToScreen(hwnd, &br);
            RECT clip{ tl.x, tl.y, br.x, br.y };
            ClipCursor(&clip);
            while (ShowCursor(FALSE) >= 0)
            {
            }
            recenterCapturedCursor();
        }
        else
        {
            ClipCursor(nullptr);
            while (ShowCursor(TRUE) < 0)
            {
            }
        }
    }

    void Window::recenterCapturedCursor()
    {
        HWND hwnd = reinterpret_cast<HWND>(m_hwnd);
        if (!hwnd || m_width == 0 || m_height == 0)
            return;
        const int cx = static_cast<int>(m_width / 2);
        const int cy = static_cast<int>(m_height / 2);
        POINT p{ cx, cy };
        ClientToScreen(hwnd, &p);
        SetCursorPos(p.x, p.y);
        if (m_input)
            m_input->warpMouse(cx, cy);
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

    bool Window::takeSizeChanged()
    {
        const bool changed = m_sizeChanged;
        m_sizeChanged = false;
        return changed;
    }

} // namespace Dark
