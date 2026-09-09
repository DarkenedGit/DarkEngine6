#pragma once

#include "Core/UiPalette.h"

#include <imgui.h>

namespace Dark
{
    inline ImVec4 toImVec4(UiColor c)
    {
        return ImVec4(c.r, c.g, c.b, c.a);
    }

    // DarkEngine chrome + host accent. Replaces ImGui::StyleColorsDark().
    // Call before Style.ScaleAllSizes so rounding/padding scale with DPI.
    void applyImGuiTheme(UiAccent accent, ImGuiStyle* dst = nullptr);

    // Fake window umbra + title-bar accent. Call after submitting UI, before ImGui::Render().
    void drawImGuiDecorations();

    // Full-viewport dock host with a transparent center so the 3D/2D scene shows through.
    // bottomReservePx leaves room for a status bar. Builds a default Editor layout on first use.
    void beginPassthruDockSpace(const char* hostName, float bottomReservePx = 0.0f);
} // namespace Dark
