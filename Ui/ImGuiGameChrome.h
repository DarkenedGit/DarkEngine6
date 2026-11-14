#pragma once

#include "Ui/ImGuiHost.h"

#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"

#include <imgui.h>

#include <string>
#include <vector>

namespace Dark
{
    class MainMenu;
    class QuestRuntime;

    // ImGui-only game chrome. Do not include this from a DarkEngine translation unit.
    // cancelHides: Esc / B / Back calls hide() (resume). Otherwise those keys quit.
    // The frame the card appears ignores those keys, so the key that opened it does not also close it.
    void drawMainMenuImGui(MainMenu& menu, const UiFonts& fonts, bool cancelHides = false);
    void setGameNavEnabled(bool enabled);

    // Centered or caller-positioned card. Always pair with EndGameCard, even when Begin returns false.
    // title draws a heading row. When title is null and open is set, a window name that is not an id is the title.
    // The heading row drags the card unless extra includes NoMove, and draws a close button when open is set.
    bool BeginGameCard(const char* name, bool* open, UiAccent accent, ImGuiWindowFlags extra = 0, const char* title = nullptr);
    void EndGameCard();

    struct QuestHudLine
    {
        std::string text;
        ImU32       color = 0;
        bool        title = false;
    };

    void collectQuestLines(const QuestRuntime& quest, std::vector<QuestHudLine>& out);
    void drawQuestPlate(ImDrawList* dl, ImVec2 origin, float width, const std::vector<QuestHudLine>& lines, ImFont* body, ImFont* heading);
    void drawObjectiveDiamond(ImDrawList* dl, ImVec2 vpPos, ImVec2 vpSize, const Math::Matrix4f& viewProj, const Math::Vector3f& worldTarget, const Math::Vector3f& from);
} // namespace Dark
