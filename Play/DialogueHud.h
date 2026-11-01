#pragma once

// ImGui dialogue HUD shared by Sandbox and Editor.
// Do not include this from a DarkEngine translation unit. Play/*.cpp is compiled into
// DarkEngine, and DarkEngine does not link Dear ImGui. UnitTests must not include it either.

#include "Play/DialogueController.h"

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"

#include <imgui.h>

#include <cfloat>
#include <cstdio>
#include <string>

namespace Dark
{

    struct DialogueHudView
    {
        Math::Matrix4f viewProj{};
        ImDrawList*    list          = nullptr;
        float          projectWidth  = 0.0f;
        float          projectHeight = 0.0f;
        const char*    windowName    = "Dialogue";
    };

    inline void drawDialogueHud(DialogueController& talk, const DialogueHudView& view)
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        if (!vp)
            return;
        ImDrawList* dl   = view.list;
        ImFont*     font = ImGui::GetFont();
        const float fs   = ImGui::GetFontSize();
        const float vw   = view.projectWidth;
        const float vh   = view.projectHeight;
        World*      world = talk.world();

        auto project = [&](const Math::Vector3f& at, ImVec2& out) {
            const Math::Vector4f clip = view.viewProj * Math::Vector4f(at, 1.0f);
            if (clip.w <= 1.0e-4f || clip.z / clip.w < 0.0f || clip.z / clip.w > 1.0f)
                return false;
            out.x = vp->Pos.x + (clip.x / clip.w * 0.5f + 0.5f) * vw;
            out.y = vp->Pos.y + (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * vh;
            return true;
        };

        DialogueRuntime& runtime = talk.runtime();
        if (runtime.active() && !runtime.subtitle().empty() && dl && font)
        {
            const Entity speaker = runtime.speaker();
            const TransformComponent* gxf = world && speaker.valid() && world->alive(speaker) ? world->get<TransformComponent>(speaker) : nullptr;
            if (gxf)
            {
                ImVec2 at{};
                if (project(Math::Vector3f(gxf->position.x, gxf->position.y + 2.1f, gxf->position.z), at))
                {
                    const char*  text = runtime.subtitle().c_str();
                    const ImVec2 ts   = font->CalcTextSizeA(fs, 260.0f, 260.0f, text);
                    at.x -= ts.x * 0.5f;
                    at.y -= ts.y;
                    dl->AddRectFilled(ImVec2(at.x - 6.0f, at.y - 3.0f), ImVec2(at.x + ts.x + 6.0f, at.y + ts.y + 3.0f), IM_COL32(10, 14, 12, 200), 3.0f);
                    dl->AddText(font, fs, at, IM_COL32(255, 255, 255, 255), text, nullptr, 260.0f);
                }
            }
            const std::string line = runtime.speakerName() + ": " + runtime.subtitle();
            const float       size = fs * 1.15f;
            const ImVec2      ts   = font->CalcTextSizeA(size, FLT_MAX, 0.0f, line.c_str());
            const ImVec2      at{ vp->Pos.x + (vp->Size.x - ts.x) * 0.5f, vp->Pos.y + vp->Size.y - 150.0f };
            dl->AddText(font, size, ImVec2(at.x + 1.5f, at.y + 1.5f), IM_COL32(0, 0, 0, 220), line.c_str());
            dl->AddText(font, size, at, IM_COL32(235, 235, 210, 255), line.c_str());
        }

        if (!runtime.active() && dl)
        {
            const Entity target = talk.pickTarget();
            const TransformComponent* xf = world && target.valid() ? world->get<TransformComponent>(target) : nullptr;
            if (xf)
            {
                ImVec2 at{};
                if (project(Math::Vector3f(xf->position.x, xf->position.y + 2.0f, xf->position.z), at))
                {
                    const char*  text = "[T] Talk";
                    const ImVec2 ts   = ImGui::CalcTextSize(text);
                    at.x -= ts.x * 0.5f;
                    at.y -= ts.y;
                    dl->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 200), text);
                    dl->AddText(at, IM_COL32(255, 220, 90, 255), text);
                }
            }
        }

        if (!runtime.choiceOpen() || !runtime.graph())
            return;
        const DialogueNode* node = runtime.graph()->findNode(runtime.nodeId());
        if (!node)
            return;
        const char* windowName = view.windowName ? view.windowName : "Dialogue";
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y - 220.0f), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Always);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
        if (!ImGui::Begin(windowName, nullptr, flags))
        {
            ImGui::End();
            return;
        }
        const std::vector<int>& indices = runtime.choiceIndices();
        int shown = static_cast<int>(indices.size());
        if (shown > 8)
            shown = 8;
        for (int i = 0; i < shown; ++i)
        {
            const int choice = indices[static_cast<size_t>(i)];
            if (choice < 0 || choice >= static_cast<int>(node->choices.size()))
                continue;
            const std::string& text = node->choices[static_cast<size_t>(choice)].text;
            char label[160];
            std::snprintf(label, sizeof(label), "%d  %s", i + 1, text.c_str());
            if (ImGui::Selectable(label, i == runtime.selected()))
            {
                talk.moveChoice(i - runtime.selected());
                talk.commitChoice();
            }
        }
        ImGui::End();
    }

} // namespace Dark
