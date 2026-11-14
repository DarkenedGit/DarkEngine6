#pragma once

#include "Ui/ImGuiHost.h"
#include "Ui/MainMenu.h"

#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"

#include <imgui.h>

#include <string>
#include <vector>

namespace Dark
{
    class QuestRuntime;

    // Passed by the host each frame. page and masterVolume are optional.
    // Level-role entries draw on the right when the page is Front.
    struct MainMenuChrome
    {
        uint64_t      background   = 0;
        MainMenuPage* page         = nullptr;
        float*        masterVolume = nullptr;
    };

    // ImGui-only game chrome. Do not include this from a DarkEngine translation unit.
    // cancelHides: Esc / B / Back calls hide() (resume) on the front page. Otherwise those keys quit.
    // On Settings or Credits they return to the front page. The frame the window appears ignores them.
    void drawMainMenuImGui(MainMenu& menu, const UiFonts& fonts, bool cancelHides = false, const MainMenuChrome& chrome = {});
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
