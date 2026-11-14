#include "Ui/ImGuiGameChrome.h"

#include "Gameplay/QuestRuntime.h"
#include "Math/MathHelper.h"
#include "Ui/Icons.h"
#include "Ui/ImGuiTheme.h"
#include "Ui/MainMenu.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <format>

void Dark::setGameNavEnabled(bool enabled)
{
    if (ImGui::GetCurrentContext() == nullptr)
        return;
    ImGuiIO& io = ImGui::GetIO();
    if (enabled)
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    else
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
}

bool Dark::BeginGameCard(const char* name, bool* open, UiAccent accent, ImGuiWindowFlags extra, const char* title)
{
    const float rounding = uiPx(6.0f);
    const float stripH   = uiPx(4.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toImVec4(UiPalette::kPanel.withAlpha(0.96f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | extra;
    const bool             shown = ImGui::Begin(name, open, flags);
    if (shown)
    {
        const ImVec2 origin = ImGui::GetWindowPos();
        const float  width  = ImGui::GetWindowWidth();
        const ImU32  strip  = toImU32(UiPalette::accent(accent));
        ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + stripH), strip, rounding, ImDrawFlags_RoundCornersTop);
        ImGui::Dummy(ImVec2(0.0f, stripH + uiPx(2.0f)));

        const char* shownTitle = title;
        if (!shownTitle && open && name && name[0] != '#')
            shownTitle = name;
        if (shownTitle && shownTitle[0] != '\0')
        {
            ImFont* heading = uiHeadingFont();
            if (heading)
                ImGui::PushFont(heading);
            const float  btn      = ImGui::GetFrameHeight();
            const bool   canClose = open != nullptr;
            const float  closeW   = canClose ? btn : 0.0f;
            const float  rowW     = ImGui::GetContentRegionAvail().x;
            const float  dragW    = rowW > closeW ? rowW - closeW : 0.0f;
            const ImVec2 rowPos   = ImGui::GetCursorScreenPos();
            const bool   canMove  = (extra & ImGuiWindowFlags_NoMove) == 0;
            if (canMove)
            {
                ImGui::InvisibleButton("##carddrag", ImVec2(dragW, btn));
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    const ImVec2 p = ImGui::GetWindowPos();
                    const ImVec2 d = ImGui::GetIO().MouseDelta;
                    ImGui::SetWindowPos(ImVec2(p.x + d.x, p.y + d.y));
                }
            }
            else
                ImGui::Dummy(ImVec2(dragW, btn));

            ImFont*         font = ImGui::GetFont();
            const float     textY = rowPos.y + (btn - font->FontSize) * 0.5f;
            ImDrawList*     dl    = ImGui::GetWindowDrawList();
            const float     textR = rowPos.x + dragW - uiPx(4.0f);
            dl->PushClipRect(rowPos, ImVec2(textR > rowPos.x ? textR : rowPos.x + dragW, rowPos.y + btn), true);
            dl->AddText(font, font->FontSize, ImVec2(rowPos.x, textY), toImU32(UiPalette::kText), shownTitle);
            dl->PopClipRect();

            if (canClose)
            {
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                if (ImGui::Button(ICON_FA_XMARK "##cardclose", ImVec2(btn, btn)))
                    *open = false;
                ImGui::PopStyleColor();
            }
            if (heading)
                ImGui::PopFont();
        }
    }
    return shown;
}

void Dark::EndGameCard()
{
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void Dark::drawMainMenuImGui(MainMenu& menu, const UiFonts& fonts, bool cancelHides)
{
    if (!menu.visible())
        return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!vp)
        return;

    const ImVec4 dim = ImGui::GetStyleColorVec4(ImGuiCol_ModalWindowDimBg);
    ImGui::GetBackgroundDrawList(const_cast<ImGuiViewport*>(vp))->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), ImGui::ColorConvertFloat4ToU32(dim));

    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(uiPx(560.0f), 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowFocus();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNavInputs;
    if (!BeginGameCard("##mainmenu", nullptr, UiAccent::Sandbox, flags))
    {
        EndGameCard();
        return;
    }

    if (ImGui::IsWindowAppearing())
        ImGui::SetNavCursorVisible(true);

    ImFont* heading = fonts.heading ? fonts.heading : ImGui::GetFont();
    ImFont* body    = fonts.body ? fonts.body : ImGui::GetFont();
    ImGui::PushFont(heading);
    const char* title = menu.title().empty() ? "DarkEngine6" : menu.title().c_str();
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::Spacing();

    const auto iconForKind = [](MainMenuKind kind) {
        switch (kind)
        {
        case MainMenuKind::BuiltIn:
            return ICON_FA_PLAY;
        case MainMenuKind::Quit:
            return ICON_FA_RIGHT_FROM_BRACKET;
        default:
            return ICON_FA_FILE;
        }
    };

    const ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
    const bool   mouseMoved = mouseDelta.x != 0.0f || mouseDelta.y != 0.0f;
    ImGui::PushFont(body);
    for (size_t i = 0; i < menu.entries().size(); ++i)
    {
        const Dark::MainMenuEntry& entry    = menu.entries()[i];
        const bool                 selected = i == menu.selectedIndex();
        const float                line     = ImGui::GetTextLineHeight();
        const float                rowH     = entry.subtitle.empty() ? ImGui::GetFrameHeight() * 1.6f : line * 2.35f;
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_None, ImVec2(0.0f, rowH)))
        {
            menu.moveTo(i);
            menu.confirm();
        }
        if (selected)
            ImGui::SetItemDefaultFocus();
        if (mouseMoved && ImGui::IsItemHovered())
            menu.moveTo(i);

        const float blockH = entry.subtitle.empty() ? line : line + line * 1.15f;
        const float textX  = rowPos.x + ImGui::GetStyle().FramePadding.x;
        const float textY  = rowPos.y + (rowH - blockH) * 0.5f;
        char        label[192];
        std::snprintf(label, sizeof(label), "%s  %s", iconForKind(entry.kind), entry.title.c_str());
        const ImU32 textCol = entry.kind == MainMenuKind::Quit ? toImU32(UiPalette::kDanger) : toImU32(UiPalette::kText);
        ImGui::GetWindowDrawList()->AddText(body, body->FontSize, ImVec2(textX, textY), textCol, label);
        if (!entry.subtitle.empty())
            ImGui::GetWindowDrawList()->AddText(body, body->FontSize, ImVec2(textX, textY + line * 1.15f), toImU32(UiPalette::kTextMuted), entry.subtitle.c_str());
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(UiPalette::kTextMuted));
    const char* hint = menu.hint().empty() ? "Arrows / WASD    Enter    Esc" : menu.hint().c_str();
    ImGui::TextUnformatted(hint);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !menu.entries().empty())
    {
        const bool appearing = ImGui::IsWindowAppearing();
        int        delta     = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_S) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown))
            delta += 1;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_W) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp))
            delta -= 1;
        if (delta != 0)
        {
            const int n   = static_cast<int>(menu.entries().size());
            int       idx = static_cast<int>(menu.selectedIndex()) + delta;
            idx %= n;
            if (idx < 0)
                idx += n;
            menu.moveTo(static_cast<size_t>(idx));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown))
            menu.confirm();
        if (!appearing && (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_GamepadBack)))
        {
            if (cancelHides)
                menu.hide();
            else
                menu.quit();
        }
    }

    EndGameCard();
}

void Dark::collectQuestLines(const QuestRuntime& quest, std::vector<QuestHudLine>& out)
{
    out.clear();
    if (!quest.active())
        return;
    const QuestDef& def = quest.def();
    out.push_back(QuestHudLine{ def.title, toImU32(UiPalette::kWarn), true });
    if (quest.complete())
        out.push_back(QuestHudLine{ "Complete. Press J for rewards.", toImU32(UiPalette::kOk), false });
    for (size_t i = 0; i < def.objectives.size(); ++i)
    {
        const ObjectiveDef&   o  = def.objectives[i];
        const ObjectiveState& st = quest.state(i);
        if (!st.done && !quest.open(i))
            continue;
        std::string text = std::string(st.done ? "[x] " : "[ ] ") + o.text;
        if (o.count > 1)
            text += std::format(" ({}/{})", st.progress, o.count);
        if (o.optional)
            text += "  optional";
        ImU32 color = toImU32(UiPalette::kText);
        if (st.done)
            color = toImU32(UiPalette::kOk, 200);
        else if (o.optional)
            color = toImU32(UiPalette::kTextMuted);
        out.push_back(QuestHudLine{ std::move(text), color, false });
    }
}

void Dark::drawQuestPlate(ImDrawList* dl, ImVec2 origin, float width, const std::vector<QuestHudLine>& lines, ImFont* body, ImFont* heading)
{
    if (!dl || lines.empty())
        return;
    ImFont* textFont = body ? body : ImGui::GetFont();
    ImFont* headFont = heading ? heading : textFont;
    float   height   = 14.0f;
    for (const QuestHudLine& line : lines)
        height += (line.title ? headFont->FontSize : textFont->FontSize) + 6.0f;
    const float x = origin.x;
    float       y = origin.y;
    dl->AddRectFilled(ImVec2(x - 12.0f, y - 10.0f), ImVec2(x + width, y + height), toImU32(UiPalette::kVoid, 150), 4.0f);
    dl->AddRectFilled(ImVec2(x - 12.0f, y - 10.0f), ImVec2(x - 9.0f, y + height), toImU32(UiPalette::kWarn, 220));
    for (const QuestHudLine& line : lines)
    {
        ImFont*     font = line.title ? headFont : textFont;
        const float size = font->FontSize;
        dl->AddText(font, size, ImVec2(x + 1.0f, y + 1.0f), IM_COL32(0, 0, 0, 180), line.text.c_str(), nullptr, width - 8.0f);
        dl->AddText(font, size, ImVec2(x, y), line.color, line.text.c_str(), nullptr, width - 8.0f);
        y += size + 6.0f;
    }
}

void Dark::drawObjectiveDiamond(ImDrawList* dl, ImVec2 vpPos, ImVec2 vpSize, const Math::Matrix4f& viewProj, const Math::Vector3f& worldTarget, const Math::Vector3f& from)
{
    if (!dl)
        return;
    const Math::Vector4f clip = viewProj * Math::Vector4f(Math::Vector3f(worldTarget.x, worldTarget.y + 1.4f, worldTarget.z), 1.0f);
    float                nx   = clip.w > 1.0e-4f ? clip.x / clip.w : -clip.x;
    float                ny   = clip.w > 1.0e-4f ? clip.y / clip.w : -clip.y;
    if (clip.w <= 1.0e-4f)
    {
        const float m = Math::Max(std::fabs(nx), std::fabs(ny));
        nx            = m > 1.0e-6f ? nx / m : 0.0f;
        ny            = m > 1.0e-6f ? ny / m : -1.0f;
    }
    const float edge = Math::Max(std::fabs(nx), std::fabs(ny));
    if (edge > 0.9f)
    {
        nx *= 0.9f / edge;
        ny *= 0.9f / edge;
    }
    const ImVec2 c{ vpPos.x + (nx * 0.5f + 0.5f) * vpSize.x, vpPos.y + (1.0f - (ny * 0.5f + 0.5f)) * vpSize.y };
    const float  r = 9.0f;
    dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), toImU32(UiPalette::kWarn, 230));
    dl->AddQuad(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), IM_COL32(0, 0, 0, 200), 1.5f);
    char dist[24];
    std::snprintf(dist, sizeof(dist), "%.0f m", static_cast<double>((worldTarget - from).Magnitude()));
    const ImVec2 ds = ImGui::CalcTextSize(dist);
    dl->AddText(ImVec2(c.x - ds.x * 0.5f + 1.0f, c.y + r + 3.0f), IM_COL32(0, 0, 0, 200), dist);
    dl->AddText(ImVec2(c.x - ds.x * 0.5f, c.y + r + 2.0f), toImU32(UiPalette::kWarn), dist);
}
