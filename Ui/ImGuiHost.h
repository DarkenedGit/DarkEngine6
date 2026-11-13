#pragma once

#include "Core/UiPalette.h"

#include <cstdint>

struct ImFont;

namespace Dark
{
class Renderer;
class Texture2D;
class Window;
}

// Unscaled design sizes. ImGuiHost multiplies these by the window DPI.
constexpr float kUiFontBodyPx    = 15.0f;
constexpr float kUiFontHeadingPx = 20.0f;
constexpr float kUiFontMonoPx    = 14.0f;

struct UiFonts
{
    ImFont* body    = nullptr;
    ImFont* heading = nullptr;
    ImFont* mono    = nullptr;
};

// Thin Dear ImGui bootstrap (Win32 + D3D12) shared by Editor, Sandbox, and VisualDebugger.
class ImGuiHost
{
public:
    // viewports is independent of docking. Torn-off OS windows force square, opaque chrome.
    bool init(Dark::Window& window, Dark::Renderer& renderer, const char* iniFilename = "imgui.ini", bool docking = false,
              Dark::UiAccent accent = Dark::UiAccent::Engine, bool viewports = false);
    void shutdown(Dark::Renderer& renderer);

    void beginFrame();
    void render(Dark::Renderer& renderer);
    void endFrame();

    bool wantCaptureMouse() const;
    bool wantCaptureKeyboard() const;
    bool wantTextInput() const;

    bool isReady() const { return m_ready; }

    const UiFonts& fonts() const { return m_fonts; }

    // Copies the texture's SRV into ImGui's heap. Returns an ImTextureID for ImGui::Image / ImDrawList::AddImage,
    // or 0 on failure. The texture must outlive ImGui use; slots are not recycled until shutdown.
    uint64_t addTexture(Dark::Renderer& renderer, const Dark::Texture2D& texture);

private:
    bool createSrvHeap(Dark::Renderer& renderer);
    void syncDpi();

    bool    m_ready     = false;
    bool    m_viewports = false;
    void*   m_hwnd      = nullptr;
    UiFonts m_fonts;

    struct Impl;
    Impl* m_impl = nullptr;
};
