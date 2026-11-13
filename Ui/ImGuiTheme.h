#pragma once

#include "Core/UiPalette.h"

#include <imgui.h>

namespace Dark
{
    inline ImVec4 toImVec4(UiColor c)
    {
        return ImVec4(c.r, c.g, c.b, c.a);
    }

    inline ImU32 toImU32(UiColor c)
    {
        return IM_COL32(static_cast<int>(c.r * 255.0f + 0.5f), static_cast<int>(c.g * 255.0f + 0.5f), static_cast<int>(c.b * 255.0f + 0.5f),
                        static_cast<int>(c.a * 255.0f + 0.5f));
    }

    inline ImU32 toImU32(UiColor c, int alpha)
    {
        return IM_COL32(static_cast<int>(c.r * 255.0f + 0.5f), static_cast<int>(c.g * 255.0f + 0.5f), static_cast<int>(c.b * 255.0f + 0.5f), alpha);
    }

    // DarkEngine chrome + host accent. Replaces ImGui::StyleColorsDark().
    // Call before Style.ScaleAllSizes so rounding/padding scale with DPI.
    void applyImGuiTheme(UiAccent accent, ImGuiStyle* dst = nullptr);

    // Fake window umbra + title-bar accent. Call after submitting UI, before ImGui::Render().
    void drawImGuiDecorations();

    // Full-viewport dock host with a transparent center so the 3D/2D scene shows through.
    // bottomReservePx leaves room for a status bar. topReservePx leaves room for a toolbar under the menu bar.
    // Builds a default Editor layout the first time this dock id has no node.
    void beginPassthruDockSpace(const char* hostName, float bottomReservePx = 0.0f, float topReservePx = 0.0f);
} // namespace Dark
