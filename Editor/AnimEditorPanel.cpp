#include "Editor/AnimEditorPanel.h"
#include "Editor/EditorImGui.h"
#include "Animation/AnimGraphJson.h"
#include "Assets/ImageCache.h"
#include "Core/Log.h"
#include "Ui/Icons.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Dark;

namespace
{
std::filesystem::path sidecarPathForModel(const Model& model)
{
    std::filesystem::path p = model.sourcePath();
    if (p.empty())
        return {};
    p.replace_extension(".anim.json");
    return p;
}

const char* clipNameAt(const AnimationSet* set, uint32_t index)
{
    if (!set)
        return "(none)";
    const AnimationClip* clip = set->clipAt(index);
    return clip ? clip->name.c_str() : "(invalid)";
}

AnimCondition::Op opFromIndex(int i)
{
    switch (i)
    {
    case 1:  return AnimCondition::Op::Lt;
    case 2:  return AnimCondition::Op::Ge;
    case 3:  return AnimCondition::Op::Le;
    case 4:  return AnimCondition::Op::Eq;
    case 5:  return AnimCondition::Op::Ne;
    default: return AnimCondition::Op::Gt;
    }
}

int indexFromOp(AnimCondition::Op op)
{
    switch (op)
    {
    case AnimCondition::Op::Lt: return 1;
    case AnimCondition::Op::Ge: return 2;
    case AnimCondition::Op::Le: return 3;
    case AnimCondition::Op::Eq: return 4;
    case AnimCondition::Op::Ne: return 5;
    case AnimCondition::Op::Gt:
    default:                    return 0;
    }
}

std::string uniqueStateName(const std::vector<AnimStateDef>& states, const char* base)
{
    std::string name = base;
    int         n    = 2;
    auto        used = [&](const std::string& s) {
        for (const AnimStateDef& st : states)
        {
            if (st.name == s)
                return true;
        }
        return false;
    };
    while (used(name))
        name = std::string(base) + std::to_string(n++);
    return name;
}

void remapTransitionsAfterStateRemoved(AnimGraphDef& def, uint32_t removed)
{
    std::vector<AnimTransitionDef> kept;
    kept.reserve(def.transitions.size());
    for (AnimTransitionDef t : def.transitions)
    {
        if (t.to == removed)
            continue;
        if (t.from == removed)
            continue;
        if (t.from != kAnyState && t.from > removed)
            --t.from;
        if (t.to > removed)
            --t.to;
        kept.push_back(std::move(t));
    }
    def.transitions = std::move(kept);
    if (def.defaultState == removed)
        def.defaultState = 0;
    else if (def.defaultState > removed)
        --def.defaultState;
}

void remapConditionsAfterParamRemoved(AnimGraphDef& def, uint32_t removed)
{
    for (AnimTransitionDef& t : def.transitions)
    {
        std::vector<AnimCondition> kept;
        for (AnimCondition c : t.when)
        {
            if (c.paramIndex == removed)
                continue;
            if (c.paramIndex > removed)
                --c.paramIndex;
            kept.push_back(c);
        }
        t.when = std::move(kept);
    }
}
} // namespace

void AnimEditorPanel::applyPlayback(AnimGraphInstance& graph) const
{
    graph.setPreviewPaused(m_paused);
    graph.setPreviewSpeedScale(m_speedScale);
}

void AnimEditorPanel::onParamEdited(AnimGraphInstance& graph)
{
    graph.setStateLocked(false);
    graph.clearPath();
}

void AnimEditorPanel::draw(AnimGraphComponent& ag, AssetManager& assets, bool* open)
{
    if (open && !*open)
        return;

    if (!ImGui::Begin("Animation", open))
    {
        ImGui::End();
        return;
    }

    if (!ag.model || !ag.model->skeleton())
    {
        ImGui::TextWrapped("Select a skinned glTF with a skeleton to edit animation.");
        ImGui::End();
        return;
    }

    if (!ag.animSet)
        ag.animSet = ag.model->animationSet();

    if (!ag.graphDef)
    {
        ImGui::TextWrapped("No *.anim.json sidecar. Create one from this model's clips, then tune states and blends live.");
        if (ag.animSet && ag.animSet->clipCount() > 0)
        {
            if (ImGui::Button(ICON_FA_CIRCLE_PLUS "  Create anim.json from clips"))
                createGraphFromClips(ag, assets);
        }
        else
            ImGui::TextDisabled("Model has no animation clips.");
        if (m_status[0])
        {
            ImGui::Separator();
            ImGui::TextWrapped("%s", m_status);
        }
        ImGui::End();
        return;
    }

    if (!ag.graph.def())
        ag.graph.bind(ag.graphDef.get(), ag.model->skeleton());

    AnimGraphDef& def = *ag.graphDef;
    if (!def.animSet)
        def.animSet = ag.animSet;

    const std::filesystem::path sidecar = sidecarPathForModel(*ag.model);
    ImGui::TextUnformatted(sidecar.empty() ? "(in-memory graph)" : sidecar.filename().string().c_str());
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save"))
        saveGraph(ag, assets);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Reload") && !sidecar.empty())
        reloadGraph(ag, assets);
    ImGui::SameLine();
    bool locked = ag.graph.stateLocked();
    if (ImGui::Checkbox("Lock state", &locked))
        ag.graph.setStateLocked(locked);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When locked, picking a state walks the graph. Editing parameters unlocks and lets conditions drive transitions.");

    drawPlayback(ag);
    ImGui::Separator();
    drawStatePicker(ag);
    ImGui::Separator();
    drawParameters(ag.graph);

    if (ImGui::CollapsingHeader("States", ImGuiTreeNodeFlags_DefaultOpen))
        drawStateEditor(ag);
    if (ImGui::CollapsingHeader("Transitions", ImGuiTreeNodeFlags_DefaultOpen))
        drawTransitionEditor(ag);
    if (ImGui::CollapsingHeader("Notifies"))
        drawNotifyEditor(def);

    if (m_status[0])
    {
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_status);
    }
    ImGui::End();
}

void AnimEditorPanel::drawPlayback(AnimGraphComponent& ag)
{
    AnimPlayer& player = ag.graph.player();
    if (ImGui::Button(m_paused ? ICON_FA_PLAY "  Play" : ICON_FA_PAUSE "  Pause"))
        m_paused = !m_paused;
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ROTATE_RIGHT "  Restart"))
        ag.graph.requestState(ag.graph.currentState());
    ImGui::SliderFloat("Preview speed", &m_speedScale, 0.0f, 2.0f, "%.2fx");

    const AnimationClip* clip = nullptr;
    if (ag.animSet)
        clip = ag.animSet->clipAt(player.incomingClip());
    const float dur = clip ? clip->duration : 0.0f;
    float       t   = player.time();
    ImGui::Text("Clip: %s", player.clipName());
    if (dur > 0.0f)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.2f / %.2f", t, dur);
        if (ImGui::SliderFloat("Time", &t, 0.0f, dur, buf))
        {
            player.setTime(t);
            AnimNotifyQueue q;
            player.update(0.0f, q);
        }
    }
    ImGui::ProgressBar(player.blendAlpha(), ImVec2(-1.0f, 0.0f), "Blend");
    if (player.outgoingClip() != AnimPlayer::kInvalidClip)
        ImGui::TextDisabled("Crossfade from %s", clipNameAt(ag.animSet.get(), player.outgoingClip()));
}

void AnimEditorPanel::drawStatePicker(AnimGraphComponent& ag)
{
    AnimGraphDef& def = *ag.graphDef;
    ImGui::Text("State: %s", ag.graph.currentStateName());

    std::string pathLine = ag.graph.currentStateName();
    if (ag.graph.pathPending())
    {
        for (uint32_t i = ag.graph.pendingPathCursor(); i < ag.graph.pendingPathLength(); ++i)
        {
            const uint32_t ti = ag.graph.pendingPathTransition(i);
            if (ti >= def.transitions.size())
                break;
            const uint32_t to = def.transitions[ti].to;
            if (to < def.states.size())
            {
                pathLine += "  ->  ";
                pathLine += def.states[to].name;
            }
        }
    }
    ImGui::TextWrapped("Path: %s", pathLine.c_str());
    ImGui::TextDisabled("Click a state to walk every transition blend on the shortest path.");

    const uint32_t current = ag.graph.currentState();
    for (uint32_t i = 0; i < def.states.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        const bool sel = (i == current);
        if (sel)
            ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        char label[128];
        std::snprintf(label, sizeof(label), "%s##pick%u", def.states[i].name.c_str(), i);
        if (ImGui::Selectable(label, sel, 0, ImVec2(0.0f, 0.0f)))
            ag.graph.requestState(i);
        if (sel)
            ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

void AnimEditorPanel::drawParameters(AnimGraphInstance& graph)
{
    const AnimGraphDef* def = graph.def();
    if (!def)
        return;
    ImGui::SeparatorText("Parameters");
    if (def->params.empty())
    {
        ImGui::TextDisabled("No parameters. Transitions with empty 'when' always fire.");
        return;
    }
    for (uint32_t i = 0; i < def->params.size(); ++i)
    {
        const AnimParamDef& p = def->params[i];
        ImGui::PushID(static_cast<int>(i) + 9000);
        if (p.type == AnimParamType::Float)
        {
            float v = graph.getFloat(p.name);
            if (ImGui::DragFloat(p.name.c_str(), &v, 0.05f, 0.0f, 30.0f))
            {
                graph.setFloat(p.name, v);
                onParamEdited(graph);
            }
        }
        else if (p.type == AnimParamType::Bool)
        {
            bool v = graph.getBool(p.name);
            if (ImGui::Checkbox(p.name.c_str(), &v))
            {
                graph.setBool(p.name, v);
                onParamEdited(graph);
            }
        }
        else
        {
            if (ImGui::Button(p.name.c_str()))
            {
                graph.setTrigger(p.name);
                onParamEdited(graph);
            }
        }
        ImGui::PopID();
    }
}
#include "Editor/AnimEditorPanel_More.inl"
