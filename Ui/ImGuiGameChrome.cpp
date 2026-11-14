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
#include <string>
#include <string_view>
#include <vector>

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

namespace
{

bool asciiText(const char* text)
{
    if (!text)
        return false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != 0; ++p)
    {
        if (*p >= 128u)
            return false;
    }
    return true;
}

float measureTracked(ImFont* font, float size, const char* text, float tracking)
{
    if (!font || !text || text[0] == '\0')
        return 0.0f;
    if (!asciiText(text))
        return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
    float width = 0.0f;
    bool  any   = false;
    char  glyph[2] = {};
    for (const char* p = text; *p != '\0'; ++p)
    {
        if (any)
            width += tracking;
        any      = true;
        glyph[0] = *p;
        width += font->CalcTextSizeA(size, FLT_MAX, 0.0f, glyph).x;
    }
    return width;
}

void drawTracked(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text, float tracking)
{
    if (!dl || !font || !text || text[0] == '\0')
        return;
    if (!asciiText(text))
    {
        dl->AddText(font, size, pos, color, text);
        return;
    }
    char glyph[2] = {};
    for (const char* p = text; *p != '\0'; ++p)
    {
        glyph[0] = *p;
        dl->AddText(font, size, pos, color, glyph);
        pos.x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, glyph).x + tracking;
    }
}

int helixAlpha(float alpha, float edge, int base)
{
    int a = static_cast<int>(static_cast<float>(base) * alpha * edge + 0.5f);
    if (a < 0)
        a = 0;
    if (a > 255)
        a = 255;
    return a;
}

void drawDnaHelix(ImDrawList* dl, ImVec2 origin, float width, float height, float phase, float alpha, float turns)
{
    if (!dl || width < 4.0f || height < 4.0f || alpha <= 0.0f)
        return;
    constexpr int kSamples = 52;
    ImVec2 strandA[kSamples];
    ImVec2 strandB[kSamples];
    float  fade[kSamples];
    const float cy  = origin.y + height * 0.5f;
    const float amp = height * 0.42f;
    const float tau = 6.2831853f;
    for (int i = 0; i < kSamples; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSamples - 1);
        float       edge = 1.0f;
        if (t < 0.16f)
            edge = t / 0.16f;
        else if (t > 0.84f)
            edge = (1.0f - t) / 0.16f;
        fade[i] = edge;
        const float s = std::sin(t * tau * turns + phase);
        const float x = origin.x + t * width;
        strandA[i] = ImVec2(x, cy - s * amp);
        strandB[i] = ImVec2(x, cy + s * amp);
    }
    const float core = height * 0.04f > 1.15f ? height * 0.04f : 1.15f;
    for (int i = 0; i < kSamples - 1; ++i)
    {
        const float edge = fade[i] < fade[i + 1] ? fade[i] : fade[i + 1];
        dl->AddLine(strandA[i], strandA[i + 1], IM_COL32(64, 176, 184, helixAlpha(alpha, edge, 70)), core * 3.4f);
        dl->AddLine(strandB[i], strandB[i + 1], IM_COL32(64, 176, 184, helixAlpha(alpha, edge, 70)), core * 3.4f);
        dl->AddLine(strandA[i], strandA[i + 1], IM_COL32(154, 236, 230, helixAlpha(alpha, edge, 220)), core);
        dl->AddLine(strandB[i], strandB[i + 1], IM_COL32(198, 248, 244, helixAlpha(alpha, edge, 170)), core * 0.75f);
    }
    for (int i = 3; i < kSamples - 3; i += 4)
    {
        const int dot = helixAlpha(alpha, fade[i], 200);
        dl->AddLine(strandA[i], strandB[i], IM_COL32(140, 220, 216, helixAlpha(alpha, fade[i], 80)), 1.0f);
        dl->AddCircleFilled(strandA[i], core * 0.9f, IM_COL32(220, 255, 252, dot));
        dl->AddCircleFilled(strandB[i], core * 0.7f, IM_COL32(170, 236, 232, dot));
    }
}

bool anyLevel(const Dark::MainMenu& menu)
{
    for (const Dark::MainMenuEntry& entry : menu.entries())
    {
        if (entry.role == Dark::MainMenuRole::Level)
            return true;
    }
    return false;
}

void stepRole(Dark::MainMenu& menu, Dark::MainMenuRole role, int delta)
{
    std::vector<size_t> ids;
    ids.reserve(menu.entries().size());
    for (size_t i = 0; i < menu.entries().size(); ++i)
    {
        if (menu.entries()[i].role == role)
            ids.push_back(i);
    }
    if (ids.empty() || delta == 0)
        return;
    int  pos   = 0;
    bool found = false;
    const size_t cur = menu.selectedIndex();
    for (int i = 0; i < static_cast<int>(ids.size()); ++i)
    {
        if (ids[static_cast<size_t>(i)] == cur)
        {
            pos   = i;
            found = true;
            break;
        }
    }
    if (!found)
    {
        menu.moveTo(ids[0]);
        return;
    }
    const int count = static_cast<int>(ids.size());
    int       next  = pos + delta;
    next %= count;
    if (next < 0)
        next += count;
    menu.moveTo(ids[static_cast<size_t>(next)]);
}

void coverUv(float viewW, float viewH, ImVec2& uv0, ImVec2& uv1)
{
    constexpr float kImageAspect = 16.0f / 9.0f;
    const float     viewAspect   = viewH > 1.0f ? viewW / viewH : kImageAspect;
    uv0 = ImVec2(0.0f, 0.0f);
    uv1 = ImVec2(1.0f, 1.0f);
    if (viewAspect > kImageAspect)
    {
        const float visible = kImageAspect / viewAspect;
        const float pad     = (1.0f - visible) * 0.5f;
        uv0.y = pad;
        uv1.y = 1.0f - pad;
    }
    else
    {
        const float visible = viewAspect / kImageAspect;
        const float pad     = (1.0f - visible) * 0.5f;
        uv0.x = pad;
        uv1.x = 1.0f - pad;
    }
}

std::string upperAscii(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char ch : text)
    {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 'a' && c <= 'z')
            out.push_back(static_cast<char>(c - 'a' + 'A'));
        else
            out.push_back(ch);
    }
    return out;
}

} // namespace

void Dark::drawMainMenuImGui(MainMenu& menu, const UiFonts& fonts, bool cancelHides, const MainMenuChrome& chrome)
{
    if (!menu.visible())
        return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!vp || vp->Size.x < 8.0f || vp->Size.y < 8.0f)
        return;

    MainMenuPage page = chrome.page ? *chrome.page : MainMenuPage::Front;
    const auto setPage = [&](MainMenuPage next) {
        page = next;
        if (chrome.page)
            *chrome.page = next;
    };

    ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->Size, ImGuiCond_Always);
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavInputs
        | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (!ImGui::Begin("##crisper", nullptr, flags))
    {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        return;
    }

    const bool   appearing  = ImGui::IsWindowAppearing();
    const ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
    const bool   mouseMoved = mouseDelta.x != 0.0f || mouseDelta.y != 0.0f;
    ImFont*      titleFont  = fonts.menuTitle ? fonts.menuTitle : (fonts.display ? fonts.display : ImGui::GetFont());
    ImFont*      itemFont   = fonts.menuItem ? fonts.menuItem : (fonts.heading ? fonts.heading : ImGui::GetFont());
    ImFont*      bodyFont   = fonts.body ? fonts.body : itemFont;
    ImDrawList*  dl         = ImGui::GetWindowDrawList();

    const float marginX = vp->Size.x * 0.055f > uiPx(36.0f) ? vp->Size.x * 0.055f : uiPx(36.0f);
    const float titlePx = titleFont->FontSize;
    const float itemPx  = itemFont->FontSize;
    const float subPx   = itemPx * 0.42f;
    const float itemTrack = itemPx * 0.16f;
    const float subTrack  = subPx * 0.22f;
    const float titleTrack = titlePx * 0.075f;
    const char* title = menu.title().empty() ? "CRISPER" : menu.title().c_str();
    const float titleW = measureTracked(titleFont, titlePx, title, titleTrack);
    const float titleX = vp->Pos.x + marginX;
    const float titleY = vp->Pos.y + vp->Size.y * 0.145f;
    const float phase  = static_cast<float>(ImGui::GetTime()) * 0.65f;
    const ImU32 white  = IM_COL32(244, 246, 248, 255);
    const ImU32 muted  = IM_COL32(168, 178, 188, 210);

    const ImVec2 screenMax(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    if (chrome.background != 0)
    {
        ImVec2 uv0(0.0f, 0.0f);
        ImVec2 uv1(1.0f, 1.0f);
        coverUv(vp->Size.x, vp->Size.y, uv0, uv1);
        dl->AddImage(static_cast<ImTextureID>(chrome.background), vp->Pos, screenMax, uv0, uv1, IM_COL32_WHITE);
    }
    else
        dl->AddRectFilled(vp->Pos, screenMax, IM_COL32(6, 10, 16, 255));

    const float fadeW = vp->Size.x * 0.48f;
    dl->AddRectFilledMultiColor(vp->Pos, ImVec2(vp->Pos.x + fadeW, screenMax.y), IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 170));

    drawDnaHelix(dl, ImVec2(titleX - titlePx * 0.12f, titleY + titlePx * 0.12f), titleW + titlePx * 0.28f, titlePx * 0.62f, phase, 0.9f, 1.65f);
    drawTracked(dl, titleFont, titlePx, ImVec2(titleX, titleY), white, title, titleTrack);

    const float menuTop = titleY + titlePx + vp->Size.y * 0.05f;
    const float primaryW = vp->Size.x * 0.40f;
    const bool  levels   = anyLevel(menu);
    const bool  front    = page == MainMenuPage::Front;

    auto hitRow = [&](const char* id, ImVec2 pos, ImVec2 size, size_t index, bool canFocus) {
        ImGui::SetCursorScreenPos(pos);
        ImGui::PushID(id);
        ImGui::PushID(static_cast<int>(index));
        const bool clicked = ImGui::InvisibleButton("##hit", size);
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        ImGui::PopID();
        if (canFocus && mouseMoved && hovered)
            menu.moveTo(index);
        if (clicked)
        {
            menu.moveTo(index);
            menu.confirm();
        }
        return ImGui::GetItemRectMin();
    };

    if (front)
    {
        float y = menuTop;
        const float gap = itemPx * 0.62f;
        for (size_t i = 0; i < menu.entries().size(); ++i)
        {
            const MainMenuEntry& entry = menu.entries()[i];
            if (entry.role != MainMenuRole::Action)
                continue;
            const bool  selected = i == menu.selectedIndex();
            const bool  lit      = selected && !menu.levelFocus();
            const float blockH   = entry.subtitle.empty() ? itemPx : itemPx + subPx * 1.35f;
            const float rowH     = blockH + gap * 0.45f;
            const ImVec2 rowPos(vp->Pos.x + marginX, y);
            hitRow("act", rowPos, ImVec2(primaryW, rowH), i, true);

            if (lit)
            {
                const float helixW = vp->Size.x * 0.32f;
                const float helixH = itemPx * 1.35f;
                drawDnaHelix(dl, ImVec2(rowPos.x - itemPx * 0.85f, rowPos.y + itemPx * 0.5f - helixH * 0.5f), helixW, helixH, phase + 0.4f, 0.95f, 2.15f);
            }

            int alpha = 215;
            if (menu.levelFocus())
                alpha = 150;
            else if (lit)
                alpha = 255;
            const ImU32 labelCol = IM_COL32(244, 246, 248, alpha);
            drawTracked(dl, itemFont, itemPx, rowPos, labelCol, entry.title.c_str(), itemTrack);
            if (!entry.subtitle.empty())
            {
                const float subY = rowPos.y + itemPx * 0.95f;
                drawTracked(dl, itemFont, subPx, ImVec2(rowPos.x, subY), muted, entry.subtitle.c_str(), subTrack);
            }
            y += blockH + gap;
        }

        if (levels)
        {
            float levelW = vp->Size.x * 0.20f;
            if (levelW < uiPx(220.0f))
                levelW = uiPx(220.0f);
            if (levelW > uiPx(420.0f))
                levelW = uiPx(420.0f);
            const float levelX = vp->Pos.x + vp->Size.x - levelW - vp->Size.x * 0.04f;
            const float headPx = itemPx * 0.38f;
            float       headY  = menuTop;
            drawTracked(dl, itemFont, headPx, ImVec2(levelX, headY), IM_COL32(140, 156, 168, 180), "DEBUG", headPx * 0.18f);
            headY += headPx * 1.35f;
            drawTracked(dl, itemFont, headPx * 1.15f, ImVec2(levelX, headY), IM_COL32(186, 198, 208, 220), "JUMP TO LEVEL", headPx * 0.14f);
            headY += headPx * 1.7f;
            dl->AddLine(ImVec2(levelX, headY), ImVec2(levelX + levelW * 0.55f, headY), IM_COL32(120, 210, 206, 90), 1.0f);
            headY += uiPx(10.0f);

            const float rowH = headPx * 2.05f;
            int         levelCount = 0;
            for (const MainMenuEntry& entry : menu.entries())
            {
                if (entry.role == MainMenuRole::Level)
                    ++levelCount;
            }
            const float natural = static_cast<float>(levelCount) * rowH;
            const float maxH    = screenMax.y - headY - uiPx(24.0f);
            const float listH   = natural < maxH ? natural : (maxH > rowH ? maxH : rowH);
            ImGui::SetCursorScreenPos(ImVec2(levelX, headY));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::BeginChild("##leveljump", ImVec2(levelW, listH), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavInputs);
            ImDrawList* listDl = ImGui::GetWindowDrawList();
            const float namePx = headPx * 1.2f;
            int         shown  = 0;
            for (size_t i = 0; i < menu.entries().size(); ++i)
            {
                const MainMenuEntry& entry = menu.entries()[i];
                if (entry.role != MainMenuRole::Level)
                    continue;
                const std::string name = upperAscii(entry.title.empty() ? entry.id : entry.title);
                ImGui::SetCursorPos(ImVec2(0.0f, static_cast<float>(shown) * rowH));
                ImGui::PushID(static_cast<int>(i));
                const bool clicked = ImGui::InvisibleButton("##lvl", ImVec2(levelW, rowH));
                const bool hovered = ImGui::IsItemHovered();
                if (i == menu.selectedIndex())
                    ImGui::SetScrollHereY(0.5f);
                ImGui::PopID();
                if (mouseMoved && hovered)
                    menu.moveTo(i);
                if (clicked)
                {
                    menu.moveTo(i);
                    menu.confirm();
                }
                const ImVec2 rowMin = ImGui::GetItemRectMin();
                const bool   lit    = i == menu.selectedIndex() && menu.levelFocus();
                const int    alpha  = lit ? 255 : (menu.levelFocus() ? 170 : 130);
                if (lit)
                    listDl->AddLine(ImVec2(rowMin.x, rowMin.y + rowH * 0.5f), ImVec2(rowMin.x + uiPx(12.0f), rowMin.y + rowH * 0.5f), IM_COL32(150, 236, 230, 230), 2.0f);
                listDl->PushClipRect(rowMin, ImVec2(rowMin.x + levelW - uiPx(4.0f), rowMin.y + rowH), true);
                drawTracked(listDl, itemFont, namePx, ImVec2(rowMin.x + uiPx(18.0f), rowMin.y + (rowH - namePx) * 0.5f), IM_COL32(236, 240, 244, alpha), name.c_str(), namePx * 0.08f);
                listDl->PopClipRect();
                ++shown;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    }
    else
    {
        float y = menuTop;
        const char* heading = page == MainMenuPage::Settings ? "SETTINGS" : "CREDITS";
        drawTracked(dl, itemFont, itemPx, ImVec2(vp->Pos.x + marginX, y), white, heading, itemTrack);
        y += itemPx * 1.6f;
        if (page == MainMenuPage::Settings)
        {
            drawTracked(dl, itemFont, subPx, ImVec2(vp->Pos.x + marginX, y), muted, "MASTER VOLUME", subTrack);
            y += subPx * 1.8f;
            if (chrome.masterVolume)
            {
                float percent = *chrome.masterVolume * 100.0f;
                ImGui::SetCursorScreenPos(ImVec2(vp->Pos.x + marginX, y));
                ImGui::PushFont(bodyFont);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.14f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1.0f, 1.0f, 1.0f, 0.18f));
                ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.55f, 0.90f, 0.88f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.75f, 1.0f, 0.96f, 1.0f));
                ImGui::SetNextItemWidth(uiPx(360.0f));
                if (ImGui::SliderFloat("##master", &percent, 0.0f, 100.0f, "%.0f%%"))
                    *chrome.masterVolume = percent / 100.0f;
                const float sliderH = ImGui::GetFrameHeight();
                ImGui::PopStyleColor(5);
                ImGui::PopFont();
                y += sliderH + itemPx;
            }
        }
        else
        {
            const char* lines[] = { "DARKENGINE6", "SANDBOX" };
            for (const char* line : lines)
            {
                drawTracked(dl, itemFont, subPx * 1.15f, ImVec2(vp->Pos.x + marginX, y), muted, line, subTrack);
                y += subPx * 1.8f;
            }
            y += itemPx * 0.4f;
        }

        const float backH = itemPx * 1.15f;
        ImGui::SetCursorScreenPos(ImVec2(vp->Pos.x + marginX, y));
        const bool back = ImGui::InvisibleButton("##menu_back", ImVec2(primaryW, backH));
        const bool backHot = ImGui::IsItemHovered();
        drawTracked(dl, itemFont, itemPx * 0.72f, ImVec2(vp->Pos.x + marginX, y), backHot ? white : muted, "BACK", itemTrack);
        if (back)
            setPage(MainMenuPage::Front);
    }

    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool backKey = ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_GamepadBack);
    if (focused && !front)
    {
        if (!appearing && backKey)
            setPage(MainMenuPage::Front);
    }
    else if (focused && front && !menu.entries().empty())
    {
        const MainMenuRole column = menu.levelFocus() ? MainMenuRole::Level : MainMenuRole::Action;
        int                delta  = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_S) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown))
            delta += 1;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_W) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp))
            delta -= 1;
        if (delta != 0)
            stepRole(menu, column, delta);
        if (levels && (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight)))
            menu.setLevelFocus(true);
        if (levels && (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft)))
            menu.setLevelFocus(false);
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown))
            menu.confirm();
        if (!appearing && backKey)
        {
            if (cancelHides)
                menu.hide();
            else
                menu.quit();
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
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
