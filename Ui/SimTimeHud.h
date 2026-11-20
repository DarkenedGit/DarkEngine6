#pragma once

// ImGui sim-time controls for Sandbox Dev Tools and the Editor PLAY card.
// Do not include this from a DarkEngine translation unit. Play/*.cpp is compiled into
// DarkEngine, and DarkEngine does not link Dear ImGui. UnitTests must not include it either.

#include "Core/Log.h"
#include "Play/PlaySession.h"
#include "Ui/Icons.h"

#include <imgui.h>

namespace Dark
{

    inline void drawSimTimeControls(PlaySession& play, const char* pauseLabel, const char* pauseHint, const char* stepHint)
    {
        bool paused = play.simPaused();
        if (ImGui::Checkbox(pauseLabel, &paused))
        {
            play.setSimPaused(paused);
            DE_LOG_INFO("Sim time paused = {}", paused);
        }
        if (pauseHint && pauseHint[0])
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", pauseHint);
        }
        ImGui::BeginDisabled(!play.simPaused());
        if (ImGui::Button(ICON_FA_FORWARD_STEP "  Step"))
            play.requestSimStep();
        ImGui::EndDisabled();
        if (stepHint && stepHint[0])
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", stepHint);
        }
        float scale = play.simTimeScale();
        if (ImGui::SliderFloat("Sim time", &scale, kSimTimeScaleMin, kSimTimeScaleMax, "%.2fx", ImGuiSliderFlags_Logarithmic))
            play.setSimTimeScale(scale);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("1 is real time. Below 1 is slow motion. Above 1 fast-forwards the world. Pause freezes every clock. Music keeps playing.");
        ImGui::SameLine();
        if (ImGui::Button("1x"))
            play.setSimTimeScale(1.0f);
    }

} // namespace Dark
