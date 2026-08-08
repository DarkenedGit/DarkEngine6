#pragma once

#include <cstdint>

namespace Dark
{
class Renderer;
class Window;
}

// Thin Dear ImGui bootstrap for the DarkEngine editor (Win32 + D3D12).
class EditorImGui
{
public:
    bool init(Dark::Window& window, Dark::Renderer& renderer);
    void shutdown(Dark::Renderer& renderer);

    void beginFrame();
    // Records ImGui draw data into the current command list (call before endFrame/present).
    void render(Dark::Renderer& renderer);
    void endFrame();

    bool wantCaptureMouse() const;
    bool wantCaptureKeyboard() const;

    bool isReady() const { return m_ready; }

private:
    bool createSrvHeap(Dark::Renderer& renderer);

    bool m_ready = false;
    void* m_hwnd = nullptr;

    // ID3D12DescriptorHeap* stored as void* to keep header free of d3d in some TUs;
    // implemented in .cpp
    struct Impl;
    Impl* m_impl = nullptr;
};
