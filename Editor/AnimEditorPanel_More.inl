
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

        if (set && Dark::EditorImGuiHelpers::comboClip("Clip", *set, st.clipIndex) && i == current)
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
    Dark::EditorImGuiHelpers::comboState("From", def, t.from, true);
    Dark::EditorImGuiHelpers::comboState("To", def, t.to, false);
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
        Dark::EditorImGuiHelpers::comboClip("Clip", *set, def.overlayClipIndex[i]);
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
