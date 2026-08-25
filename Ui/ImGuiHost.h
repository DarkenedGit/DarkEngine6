#pragma once

#include <cstdint>

namespace Dark
{
class Renderer;
class Window;
}

// Thin Dear ImGui bootstrap (Win32 + D3D12) shared by Editor and VisualDebugger.
class ImGuiHost
{
public:
    bool init(Dark::Window& window, Dark::Renderer& renderer, const char* iniFilename = "imgui.ini", bool docking = false);
    void shutdown(Dark::Renderer& renderer);

    void beginFrame();
    void render(Dark::Renderer& renderer);
    void endFrame();

    bool wantCaptureMouse() const;
    bool wantCaptureKeyboard() const;

    bool isReady() const { return m_ready; }

private:
    bool createSrvHeap(Dark::Renderer& renderer);

    bool  m_ready = false;
    void* m_hwnd  = nullptr;

    struct Impl;
    Impl* m_impl = nullptr;
};
