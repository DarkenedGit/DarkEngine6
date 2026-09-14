#include "Editor/AnimEditorPanel.h"
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

bool comboClip(const char* label, const AnimationSet& set, uint32_t& clipIndex)
{
    const char* preview = clipNameAt(&set, clipIndex);
    bool        changed = false;
    if (!ImGui::BeginCombo(label, preview))
        return false;
    for (uint32_t i = 0; i < set.clipCount(); ++i)
    {
        const bool sel = (i == clipIndex);
        if (ImGui::Selectable(clipNameAt(&set, i), sel))
        {
            clipIndex = i;
            changed   = true;
        }
        if (sel)
            ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return changed;
}

bool comboState(const char* label, const AnimGraphDef& def, uint32_t& stateIndex, bool allowAny)
{
    const char* preview = "(invalid)";
    if (allowAny && stateIndex == kAnyState)
        preview = "*";
    else if (stateIndex < def.states.size())
        preview = def.states[stateIndex].name.c_str();

    bool changed = false;
    if (!ImGui::BeginCombo(label, preview))
        return false;
    if (allowAny)
    {
        const bool sel = (stateIndex == kAnyState);
        if (ImGui::Selectable("*", sel))
        {
            stateIndex = kAnyState;
            changed    = true;
        }
    }
    for (uint32_t i = 0; i < def.states.size(); ++i)
    {
        const bool sel = (stateIndex == i);
        if (ImGui::Selectable(def.states[i].name.c_str(), sel))
        {
            stateIndex = i;
            changed    = true;
        }
        if (sel)
            ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return changed;
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

void AnimEditorPanel::drawStateEditor(AnimGraphComponent& ag)
{
    AnimGraphDef&       def    = *ag.graphDef;
    const AnimationSet* set    = def.animSet.get();
    const uint32_t      current = ag.graph.currentState();

    if (ImGui::Button(ICON_FA_CIRCLE_PLUS "  Add state") && set && set->clipCount() > 0)
    {
        AnimStateDef st;
        st.name      = uniqueStateName(def.states, "State");
        st.clipIndex = 0;
        st.loop      = true;
        def.states.push_back(std::move(st));
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(def.states.size() <= 1);
    if (ImGui::Button(ICON_FA_TRASH "  Remove current") && current < def.states.size())
    {
        remapTransitionsAfterStateRemoved(def, current);
        def.states.erase(def.states.begin() + static_cast<int>(current));
        m_selectedTransition = -1;
        ag.graph.rebindKeepingState();
    }
    ImGui::EndDisabled();

    for (uint32_t i = 0; i < def.states.size(); ++i)
    {
        AnimStateDef& st = def.states[i];
        ImGui::PushID(static_cast<int>(i) + 2000);
        ImGui::Separator();
        char nameBuf[64]{};
#if defined(_MSC_VER)
        strncpy_s(nameBuf, st.name.c_str(), _TRUNCATE);
#else
        std::strncpy(nameBuf, st.name.c_str(), sizeof(nameBuf) - 1);
#endif
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
            st.name = nameBuf;

        if (set && comboClip("Clip", *set, st.clipIndex) && i == current)
            ag.graph.player().playIndex(st.clipIndex, 0.12f, true);

        if (ImGui::DragFloat("Speed", &st.speed, 0.01f, 0.0f, 4.0f) && i == current)
            ag.graph.player().setSpeed(m_paused ? 0.0f : st.speed * m_speedScale);

        if (ImGui::Checkbox("Loop", &st.loop) && i == current)
            ag.graph.player().setLoopOverride(st.loop ? 1 : 0);

        bool isDefault = (def.defaultState == i);
        if (ImGui::Checkbox("Default", &isDefault) && isDefault)
            def.defaultState = i;
        ImGui::PopID();
    }
}

void AnimEditorPanel::drawTransitionEditor(AnimGraphComponent& ag)
{
    AnimGraphDef& def = *ag.graphDef;

    if (ImGui::Button(ICON_FA_CIRCLE_PLUS "  Add transition") && !def.states.empty())
    {
        AnimTransitionDef t;
        t.from         = ag.graph.currentState();
        t.to           = (t.from + 1) % static_cast<uint32_t>(def.states.size());
        t.blendSec     = 0.15f;
        t.canInterrupt = true;
        def.transitions.push_back(t);
        m_selectedTransition = static_cast<int>(def.transitions.size()) - 1;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(m_selectedTransition < 0 || m_selectedTransition >= static_cast<int>(def.transitions.size()));
    if (ImGui::Button(ICON_FA_TRASH "  Remove transition"))
    {
        def.transitions.erase(def.transitions.begin() + m_selectedTransition);
        m_selectedTransition = -1;
        ag.graph.clearPath();
    }
    ImGui::EndDisabled();

    if (ImGui::BeginTable("transitions", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("From");
        ImGui::TableSetupColumn("To");
        ImGui::TableSetupColumn("Blend");
        ImGui::TableSetupColumn("Interrupt");
        ImGui::TableSetupColumn("On end");
        ImGui::TableHeadersRow();
        for (uint32_t i = 0; i < def.transitions.size(); ++i)
        {
            AnimTransitionDef& t = def.transitions[i];
            ImGui::PushID(static_cast<int>(i) + 4000);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool sel = (m_selectedTransition == static_cast<int>(i));
            const char* fromName = (t.from == kAnyState) ? "*" : (t.from < def.states.size() ? def.states[t.from].name.c_str() : "?");
            char        rowLabel[96];
            std::snprintf(rowLabel, sizeof(rowLabel), "%s##row%u", fromName, i);
            if (ImGui::Selectable(rowLabel, sel, ImGuiSelectableFlags_SpanAllColumns))
                m_selectedTransition = static_cast<int>(i);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(t.to < def.states.size() ? def.states[t.to].name.c_str() : "?");
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", t.blendSec);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(t.canInterrupt ? "yes" : "no");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(t.onClipEnd ? "yes" : "no");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (m_selectedTransition < 0 || m_selectedTransition >= static_cast<int>(def.transitions.size()))
        return;

    AnimTransitionDef& t = def.transitions[static_cast<uint32_t>(m_selectedTransition)];
    ImGui::SeparatorText("Selected transition");
    comboState("From", def, t.from, true);
    comboState("To", def, t.to, false);
    ImGui::DragFloat("Blend (sec)", &t.blendSec, 0.005f, 0.0f, 2.0f, "%.3f");
    ImGui::Checkbox("Can interrupt", &t.canInterrupt);
    ImGui::Checkbox("On clip end", &t.onClipEnd);

    ImGui::TextUnformatted("When (all must pass; empty = always)");
    if (ImGui::Button("Add condition") && !def.params.empty())
    {
        AnimCondition c;
        c.paramIndex = 0;
        t.when.push_back(c);
    }
    for (uint32_t ci = 0; ci < t.when.size(); ++ci)
    {
        AnimCondition& c = t.when[ci];
        ImGui::PushID(static_cast<int>(ci) + 6000);
        if (c.paramIndex >= def.params.size())
            c.paramIndex = 0;
        if (ImGui::BeginCombo("Param", def.params.empty() ? "(none)" : def.params[c.paramIndex].name.c_str()))
        {
            for (uint32_t pi = 0; pi < def.params.size(); ++pi)
            {
                const bool sel = (pi == c.paramIndex);
                if (ImGui::Selectable(def.params[pi].name.c_str(), sel))
                    c.paramIndex = pi;
            }
            ImGui::EndCombo();
        }
        int opi = indexFromOp(c.op);
        const char* ops[] = { ">", "<", ">=", "<=", "==", "!=" };
        if (ImGui::Combo("Op", &opi, ops, 6))
            c.op = opFromIndex(opi);
        if (!def.params.empty() && def.params[c.paramIndex].type == AnimParamType::Float)
            ImGui::DragFloat("Value", &c.floatValue, 0.05f);
        else
            ImGui::Checkbox("Value", &c.boolValue);
        if (ImGui::Button("Remove condition"))
        {
            t.when.erase(t.when.begin() + static_cast<int>(ci));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("Parameters list"))
    {
        if (ImGui::Button("Add float param"))
        {
            AnimParamDef p;
            p.name = "param" + std::to_string(def.params.size() + 1);
            def.params.push_back(std::move(p));
            ag.graph.refreshParams();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add trigger"))
        {
            AnimParamDef p;
            p.name = "trigger" + std::to_string(def.params.size() + 1);
            p.type = AnimParamType::Trigger;
            def.params.push_back(std::move(p));
            ag.graph.refreshParams();
        }
        for (uint32_t i = 0; i < def.params.size(); ++i)
        {
            AnimParamDef& p = def.params[i];
            ImGui::PushID(static_cast<int>(i) + 8000);
            char nameBuf[64]{};
#if defined(_MSC_VER)
            strncpy_s(nameBuf, p.name.c_str(), _TRUNCATE);
#else
            std::strncpy(nameBuf, p.name.c_str(), sizeof(nameBuf) - 1);
#endif
            if (ImGui::InputText("Param name", nameBuf, sizeof(nameBuf)))
                p.name = nameBuf;
            ImGui::SameLine();
            if (ImGui::Button("Remove"))
            {
                remapConditionsAfterParamRemoved(def, i);
                def.params.erase(def.params.begin() + static_cast<int>(i));
                ag.graph.rebindKeepingState();
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }
}

void AnimEditorPanel::drawNotifyEditor(AnimGraphDef& def)
{
    const AnimationSet* set = def.animSet.get();
    if (!set)
        return;
    if (ImGui::Button(ICON_FA_CIRCLE_PLUS "  Add notify"))
    {
        AnimMarker m;
        m.name = "notify";
        def.overlayMarkers.push_back(m);
        def.overlayClipIndex.push_back(0);
        m_selectedNotify = static_cast<int>(def.overlayMarkers.size()) - 1;
    }
    const uint32_t count = static_cast<uint32_t>(
        def.overlayMarkers.size() < def.overlayClipIndex.size() ? def.overlayMarkers.size() : def.overlayClipIndex.size());
    for (uint32_t i = 0; i < count; ++i)
    {
        ImGui::PushID(static_cast<int>(i) + 10000);
        comboClip("Clip", *set, def.overlayClipIndex[i]);
        char nameBuf[64]{};
#if defined(_MSC_VER)
        strncpy_s(nameBuf, def.overlayMarkers[i].name.c_str(), _TRUNCATE);
#else
        std::strncpy(nameBuf, def.overlayMarkers[i].name.c_str(), sizeof(nameBuf) - 1);
#endif
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
            def.overlayMarkers[i].name = nameBuf;
        ImGui::DragFloat("Time", &def.overlayMarkers[i].time, 0.01f, 0.0f, 30.0f);
        if (ImGui::Button("Remove"))
        {
            def.overlayMarkers.erase(def.overlayMarkers.begin() + static_cast<int>(i));
            def.overlayClipIndex.erase(def.overlayClipIndex.begin() + static_cast<int>(i));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
}

bool AnimEditorPanel::saveGraph(AnimGraphComponent& ag, AssetManager& assets)
{
    if (!ag.graphDef)
        return false;
    std::filesystem::path path = ag.model ? sidecarPathForModel(*ag.model) : std::filesystem::path{};
    if (path.empty() && !ag.graphDef->modelPath.empty())
    {
        std::filesystem::path modelVirt(ag.graphDef->modelPath);
        modelVirt.replace_extension(".anim.json");
        path = assets.resolve(modelVirt.generic_string());
        if (path.empty())
            path = modelVirt;
    }
    if (path.empty())
    {
        std::snprintf(m_status, sizeof(m_status), "No sidecar path — load a glTF from content/ first.");
        return false;
    }
    if (ag.graphDef->modelPath.empty() && ag.model)
    {
        std::string virt = assets.virtualPathFromAbsolute(ag.model->sourcePath());
        if (!virt.empty())
            ag.graphDef->modelPath = virt;
    }
    if (!saveAnimGraphJsonFile(*ag.graphDef, path))
    {
        std::snprintf(m_status, sizeof(m_status), "Save failed: %s", path.string().c_str());
        return false;
    }
    const std::string key = ImageCache::normalizePath(path);
    if (ag.graphDef->id == NULL_ASSET)
        assets.registerAsset(ag.graphDef, key);
    std::snprintf(m_status, sizeof(m_status), "Saved %s", path.filename().string().c_str());
    return true;
}

bool AnimEditorPanel::reloadGraph(AnimGraphComponent& ag, AssetManager& assets)
{
    (void)assets;
    if (!ag.graphDef || !ag.graphDef->animSet || !ag.model)
        return false;
    const std::filesystem::path path = sidecarPathForModel(*ag.model);
    if (path.empty())
        return false;
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::snprintf(m_status, sizeof(m_status), "Could not open %s", path.string().c_str());
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();

    AnimGraphDef parsed;
    if (!parseAnimGraphJson(text.c_str(), *ag.graphDef->animSet, parsed))
    {
        std::snprintf(m_status, sizeof(m_status), "Reload parse failed");
        return false;
    }
    AnimGraphDef& dst = *ag.graphDef;
    dst.defaultState      = parsed.defaultState;
    dst.modelPath         = std::move(parsed.modelPath);
    dst.params            = std::move(parsed.params);
    dst.states            = std::move(parsed.states);
    dst.transitions       = std::move(parsed.transitions);
    dst.overlayMarkers    = std::move(parsed.overlayMarkers);
    dst.overlayClipIndex  = std::move(parsed.overlayClipIndex);
    ag.graph.rebindKeepingState();
    m_selectedTransition = -1;
    std::snprintf(m_status, sizeof(m_status), "Reloaded %s", path.filename().string().c_str());
    return true;
}

bool AnimEditorPanel::createGraphFromClips(AnimGraphComponent& ag, AssetManager& assets)
{
    if (!ag.model || !ag.model->skeleton())
        return false;
    if (!ag.animSet)
        ag.animSet = ag.model->animationSet();
    if (!ag.animSet || ag.animSet->clipCount() == 0)
        return false;

    auto graph = std::make_shared<AnimGraphDef>();
    std::string modelVirt = assets.virtualPathFromAbsolute(ag.model->sourcePath());
    if (modelVirt.empty() && !ag.model->sourcePath().empty())
        modelVirt = ag.model->sourcePath().filename().generic_string();
    if (!initAnimGraphFromSet(*graph, *ag.animSet, modelVirt))
        return false;
    graph->animSet = ag.animSet;

    const std::filesystem::path sidecar = sidecarPathForModel(*ag.model);
    if (!sidecar.empty())
        assets.registerAsset(graph, ImageCache::normalizePath(sidecar));
    else
        assets.registerAsset(graph);

    ag.graphDef = graph;
    ag.graph.bind(graph.get(), ag.model->skeleton());
    std::snprintf(m_status, sizeof(m_status), "Created graph with %u states — Save to write anim.json", static_cast<unsigned>(graph->states.size()));
    return true;
}
