#include "Ui/ImGuiTheme.h"

#include <imgui_internal.h>

namespace Dark
{
    namespace
    {
        UiAccent g_accent = UiAccent::Engine;

        ImVec4 col(UiColor c)
        {
            return ImVec4(c.r, c.g, c.b, c.a);
        }

        ImVec4 col(UiColor c, float a)
        {
            return ImVec4(c.r, c.g, c.b, a);
        }

        ImVec4 lerp(const ImVec4& a, const ImVec4& b, float t)
        {
            return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
        }
    } // namespace

    void applyImGuiTheme(UiAccent accent, ImGuiStyle* dst)
    {
        g_accent = accent;

        ImGuiStyle* style  = dst ? dst : &ImGui::GetStyle();
        ImVec4*     colors = style->Colors;

        const UiColor accentC      = UiPalette::accent(accent);
        const UiColor accentBright = uiLerp(accentC, UiPalette::kText, 0.18f);
        const UiColor titleActive  = uiLerp(UiPalette::kVoid, accentC, 0.22f);
        const UiColor frameRest    = uiLerp(UiPalette::kRaised, accentC, 0.16f);

        style->WindowPadding     = ImVec2(10.0f, 8.0f);
        style->FramePadding      = ImVec2(8.0f, 4.0f);
        style->ItemSpacing       = ImVec2(8.0f, 6.0f);
        style->ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
        style->CellPadding       = ImVec2(6.0f, 4.0f);
        style->IndentSpacing     = 18.0f;
        style->ScrollbarSize     = 14.0f;
        style->GrabMinSize       = 10.0f;
        style->WindowRounding    = 6.0f;
        style->ChildRounding     = 4.0f;
        style->FrameRounding     = 4.0f;
        style->PopupRounding     = 6.0f;
        style->ScrollbarRounding = 8.0f;
        style->GrabRounding      = 3.0f;
        style->TabRounding       = 4.0f;
        style->WindowBorderSize  = 1.0f;
        style->ChildBorderSize   = 1.0f;
        style->PopupBorderSize   = 1.0f;
        style->FrameBorderSize   = 0.0f;
        style->TabBorderSize     = 0.0f;
        style->WindowTitleAlign  = ImVec2(0.0f, 0.5f);

        colors[ImGuiCol_Text]                      = col(UiPalette::kText);
        colors[ImGuiCol_TextDisabled]              = col(UiPalette::kTextDisabled);
        colors[ImGuiCol_WindowBg]                  = col(UiPalette::kVoid, 0.96f);
        colors[ImGuiCol_ChildBg]                   = col(UiPalette::kInset, 0.35f);
        colors[ImGuiCol_PopupBg]                   = col(UiPalette::kPanel, 0.98f);
        colors[ImGuiCol_Border]                    = col(UiPalette::kBorder, 0.55f);
        colors[ImGuiCol_BorderShadow]              = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_FrameBg]                   = col(frameRest, 0.78f);
        colors[ImGuiCol_FrameBgHovered]            = col(accentC, 0.38f);
        colors[ImGuiCol_FrameBgActive]             = col(accentC, 0.58f);
        colors[ImGuiCol_TitleBg]                   = col(UiPalette::kInset, 1.0f);
        colors[ImGuiCol_TitleBgActive]             = col(titleActive, 1.0f);
        colors[ImGuiCol_TitleBgCollapsed]          = col(UiPalette::kVoid, 0.72f);
        colors[ImGuiCol_MenuBarBg]                 = col(UiPalette::kRaised, 1.0f);
        colors[ImGuiCol_ScrollbarBg]               = col(UiPalette::kInset, 0.62f);
        colors[ImGuiCol_ScrollbarGrab]             = col(UiPalette::kBorder, 0.90f);
        colors[ImGuiCol_ScrollbarGrabHovered]      = col(UiPalette::kTextMuted, 0.70f);
        colors[ImGuiCol_ScrollbarGrabActive]       = col(accentC, 0.85f);
        colors[ImGuiCol_CheckMark]                 = col(accentC);
        colors[ImGuiCol_SliderGrab]                = col(accentC, 0.90f);
        colors[ImGuiCol_SliderGrabActive]          = col(accentBright);
        colors[ImGuiCol_Button]                    = col(accentC, 0.32f);
        colors[ImGuiCol_ButtonHovered]             = col(accentC, 0.82f);
        colors[ImGuiCol_ButtonActive]              = col(accentBright, 1.0f);
        colors[ImGuiCol_Header]                    = col(accentC, 0.22f);
        colors[ImGuiCol_HeaderHovered]             = col(accentC, 0.52f);
        colors[ImGuiCol_HeaderActive]              = col(accentC, 0.78f);
        colors[ImGuiCol_Separator]                 = col(UiPalette::kBorder, 0.55f);
        colors[ImGuiCol_SeparatorHovered]          = col(accentC, 0.72f);
        colors[ImGuiCol_SeparatorActive]           = col(accentC, 1.0f);
        colors[ImGuiCol_ResizeGrip]                = col(accentC, 0.18f);
        colors[ImGuiCol_ResizeGripHovered]         = col(accentC, 0.62f);
        colors[ImGuiCol_ResizeGripActive]          = col(accentC, 0.92f);
        colors[ImGuiCol_TabHovered]                = colors[ImGuiCol_HeaderHovered];
        colors[ImGuiCol_Tab]                       = lerp(colors[ImGuiCol_Header], colors[ImGuiCol_TitleBgActive], 0.80f);
        colors[ImGuiCol_TabSelected]               = lerp(colors[ImGuiCol_HeaderActive], colors[ImGuiCol_TitleBgActive], 0.55f);
        colors[ImGuiCol_TabSelectedOverline]       = col(accentC);
        colors[ImGuiCol_TabDimmed]                 = lerp(colors[ImGuiCol_Tab], colors[ImGuiCol_TitleBg], 0.80f);
        colors[ImGuiCol_TabDimmedSelected]         = lerp(colors[ImGuiCol_TabSelected], colors[ImGuiCol_TitleBg], 0.40f);
        colors[ImGuiCol_TabDimmedSelectedOverline] = col(accentC, 0.45f);
        colors[ImGuiCol_DockingPreview]            = col(accentC, 0.55f);
        colors[ImGuiCol_DockingEmptyBg]            = col(UiPalette::kRaised);
        colors[ImGuiCol_PlotLines]                 = col(UiPalette::kTextMuted);
        colors[ImGuiCol_PlotLinesHovered]          = col(UiPalette::kDanger);
        colors[ImGuiCol_PlotHistogram]             = col(UiPalette::kWarn);
        colors[ImGuiCol_PlotHistogramHovered]      = col(accentBright);
        colors[ImGuiCol_TableHeaderBg]             = col(UiPalette::kRaised);
        colors[ImGuiCol_TableBorderStrong]         = col(UiPalette::kBorder);
        colors[ImGuiCol_TableBorderLight]          = col(UiPalette::kBorder, 0.45f);
        colors[ImGuiCol_TableRowBg]                = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_TableRowBgAlt]             = col(UiPalette::kText, 0.04f);
        colors[ImGuiCol_TextLink]                  = col(accentC);
        colors[ImGuiCol_TextSelectedBg]            = col(accentC, 0.35f);
        colors[ImGuiCol_DragDropTarget]            = col(UiPalette::kWarn, 0.90f);
        colors[ImGuiCol_NavCursor]                 = col(accentC);
        colors[ImGuiCol_NavWindowingHighlight]     = col(UiPalette::kText, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg]         = col(UiPalette::kVoid, 0.45f);
        colors[ImGuiCol_ModalWindowDimBg]          = col(UiPalette::kVoid, 0.62f);
    }

    void drawImGuiDecorations()
    {
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        if (!ctx)
            return;

        ImGuiContext& g         = *ctx;
        const UiColor accent    = UiPalette::accent(g_accent);
        const ImU32   accentU32 = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.r, accent.g, accent.b, 1.0f));
        const ImU32   accentDim = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.r, accent.g, accent.b, 0.45f));

        for (int i = 0; i < g.Windows.Size; ++i)
        {
            ImGuiWindow* w = g.Windows[i];
            if (!w || !w->WasActive || w->Hidden)
                continue;
            if (w->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Tooltip))
                continue;
            if (w->DockNode != nullptr || w->DockNodeAsHost != nullptr)
                continue;
            if ((w->Flags & ImGuiWindowFlags_NoTitleBar) != 0)
                continue;
            if (w->Size.x < 16.0f || w->Size.y < 16.0f)
                continue;
            if (!w->Viewport)
                continue;

            const ImVec2 pmin = w->Pos;
            const ImVec2 pmax = ImVec2(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y);
            ImDrawList*  bg   = ImGui::GetBackgroundDrawList(static_cast<ImGuiViewport*>(w->Viewport));
            const float  rnd  = w->WindowRounding;

            for (int layer = 4; layer >= 1; --layer)
            {
                const float expand = static_cast<float>(layer) * 3.0f;
                const int   alpha  = 18 / layer;
                const ImU32 shadow = IM_COL32(0, 0, 0, alpha);
                bg->AddRectFilled(ImVec2(pmin.x - expand, pmin.y - expand + 2.0f), ImVec2(pmax.x + expand, pmax.y + expand + 2.0f), shadow, rnd + expand * 0.35f);
            }

            if (w->TitleBarHeight > 1.0f)
            {
                const ImRect title   = w->TitleBarRect();
                const bool   focused = (g.NavWindow != nullptr) && (g.NavWindow == w || g.NavWindow->RootWindowForTitleBarHighlight == w);
                const float  strip   = 2.0f;
                w->DrawList->AddRectFilled(ImVec2(title.Min.x, title.Max.y - strip), title.Max, focused ? accentU32 : accentDim);
            }
        }
    }

    void beginPassthruDockSpace(const char* hostName, float bottomReservePx)
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        if (!vp || !hostName)
            return;

        const float reserve = bottomReservePx > 0.0f ? bottomReservePx : 0.0f;
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - reserve));
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        const ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
        ImGui::Begin(hostName, nullptr, hostFlags);

        const ImGuiID dockId = ImGui::GetID("EditorDock");
        if (ImGui::DockBuilderGetNode(dockId) == nullptr)
        {
            ImGui::DockBuilderRemoveNode(dockId);
            ImGui::DockBuilderAddNode(dockId, static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace));
            ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetWindowSize());

            ImGuiID dockMain  = dockId;
            ImGuiID dockLeft  = 0;
            ImGuiID dockRight = 0;
            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, &dockLeft, &dockMain);
            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.26f, &dockRight, &dockMain);
            ImGui::DockBuilderDockWindow("Scene", dockLeft);
            ImGui::DockBuilderDockWindow("Inspector", dockRight);
            ImGui::DockBuilderDockWindow("Particle System", dockRight);
            ImGui::DockBuilderDockWindow("2D Level", dockRight);
            ImGui::DockBuilderFinish(dockId);
        }

        ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::End();
        ImGui::PopStyleVar(3);
    }
} // namespace Dark
