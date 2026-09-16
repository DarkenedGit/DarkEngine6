void HsmEditorPanel::drawEvents()
{
    if (ImGui::Button(ICON_FA_CIRCLE_PLUS "  Add event"))
    {
        std::vector<std::string> used;
        for (const HsmEventDef& e : m_def.events)
            used.push_back(e.name);
        HsmEventDef e;
        e.name = uniqueName(used, "Event");
        e.id   = 10;
        for (const HsmEventDef& existing : m_def.events)
        {
            if (existing.id >= e.id)
                e.id = existing.id + 1;
        }
        m_def.events.push_back(std::move(e));
        m_selectedEvent = static_cast<int>(m_def.events.size()) - 1;
        m_previewDirty  = true;
    }
    for (uint32_t i = 0; i < m_def.events.size(); ++i)
    {
        HsmEventDef& e = m_def.events[i];
        ImGui::PushID(static_cast<int>(i) + 1000);
        char buf[64]{};
#if defined(_MSC_VER)
        strncpy_s(buf, e.name.c_str(), _TRUNCATE);
#else
        std::strncpy(buf, e.name.c_str(), sizeof(buf) - 1);
#endif
        if (ImGui::InputText("Name", buf, sizeof(buf)))
        {
            const std::string old = e.name;
            e.name                = buf;
            for (HsmTransitionDef& t : m_def.transitions)
            {
                if (t.event == old)
                    t.event = e.name;
            }
            m_previewDirty = true;
        }
        int id = static_cast<int>(e.id);
        if (ImGui::InputInt("Id", &id))
        {
            if (id < 2)
                id = 2;
            e.id           = static_cast<AI::HsmEventId>(id);
            m_previewDirty = true;
        }
        if (ImGui::Button("Remove"))
        {
            const std::string gone = e.name;
            m_def.events.erase(m_def.events.begin() + static_cast<int>(i));
            for (HsmTransitionDef& t : m_def.transitions)
            {
                if (t.event == gone)
                    t.event.clear();
            }
            m_previewDirty = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
        ImGui::Separator();
    }
}

void HsmEditorPanel::drawStates()
{
    if (ImGui::Button(ICON_FA_CIRCLE_PLUS "  Add state"))
    {
        HsmStateDef st;
        st.name   = uniqueName(stateNames(m_def), "State");
        st.parent = m_def.root;
        m_def.states.push_back(std::move(st));
        m_selectedState = static_cast<int>(m_def.states.size()) - 1;
        m_previewDirty  = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(m_def.states.size() <= 1 || m_selectedState < 0);
    if (ImGui::Button(ICON_FA_TRASH "  Remove state") && m_selectedState >= 0 && m_selectedState < static_cast<int>(m_def.states.size()))
    {
        const std::string gone = m_def.states[static_cast<uint32_t>(m_selectedState)].name;
        m_def.states.erase(m_def.states.begin() + m_selectedState);
        if (m_def.root == gone && !m_def.states.empty())
            m_def.root = m_def.states[0].name;
        for (HsmStateDef& st : m_def.states)
        {
            if (st.parent == gone)
                st.parent.clear();
            if (st.initial == gone)
                st.initial.clear();
            if (st.base == gone)
                st.base.clear();
        }
        std::vector<HsmTransitionDef> kept;
        for (HsmTransitionDef t : m_def.transitions)
        {
            if (t.to == gone)
                continue;
            std::vector<std::string> from;
            for (const std::string& f : t.from)
            {
                if (f != gone)
                    from.push_back(f);
            }
            if (from.empty())
                continue;
            t.from = std::move(from);
            kept.push_back(std::move(t));
        }
        m_def.transitions    = std::move(kept);
        m_selectedState      = 0;
        m_selectedTransition = -1;
        m_previewDirty       = true;
    }
    ImGui::EndDisabled();

    for (uint32_t i = 0; i < m_def.states.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i) + 2000);
        const bool sel = (m_selectedState == static_cast<int>(i));
        if (ImGui::Selectable(m_def.states[i].name.c_str(), sel))
            m_selectedState = static_cast<int>(i);
        ImGui::PopID();
    }

    if (m_selectedState < 0 || m_selectedState >= static_cast<int>(m_def.states.size()))
        return;

    HsmStateDef& st = m_def.states[static_cast<uint32_t>(m_selectedState)];
    ImGui::SeparatorText("Selected state");
    char buf[64]{};
#if defined(_MSC_VER)
    strncpy_s(buf, st.name.c_str(), _TRUNCATE);
#else
    std::strncpy(buf, st.name.c_str(), sizeof(buf) - 1);
#endif
    if (ImGui::InputText("Name", buf, sizeof(buf)) && buf[0])
    {
        const std::string old = st.name;
        st.name               = buf;
        if (m_def.root == old)
            m_def.root = st.name;
        for (HsmStateDef& other : m_def.states)
        {
            if (other.parent == old)
                other.parent = st.name;
            if (other.initial == old)
                other.initial = st.name;
            if (other.base == old)
                other.base = st.name;
        }
        for (HsmTransitionDef& t : m_def.transitions)
        {
            if (t.to == old)
                t.to = st.name;
            for (std::string& f : t.from)
            {
                if (f == old)
                    f = st.name;
            }
        }
        m_previewDirty = true;
    }
    auto names = stateNames(m_def);
    if (Dark::EditorImGuiHelpers::comboString("Parent", st.parent, names, true))
        m_previewDirty = true;
    if (Dark::EditorImGuiHelpers::comboString("Initial child", st.initial, names, true))
        m_previewDirty = true;
    int hist = static_cast<int>(st.history);
    const char* histItems[] = { "none", "shallow", "deep" };
    if (ImGui::Combo("History", &hist, histItems, 3))
    {
        st.history     = static_cast<AI::HsmHistory>(hist);
        m_previewDirty = true;
    }
    if (ImGui::Checkbox("Inherit entry/exit", &st.inheritEntryExit))
        m_previewDirty = true;
}

void HsmEditorPanel::drawTransitions()
{
    if (ImGui::Button(ICON_FA_CIRCLE_PLUS "  Add transition") && !m_def.states.empty() && !m_def.events.empty())
    {
        HsmTransitionDef t;
        t.from  = { m_def.states[0].name };
        t.to    = m_def.states.back().name;
        t.event = m_def.events[0].name;
        m_def.transitions.push_back(std::move(t));
        m_selectedTransition = static_cast<int>(m_def.transitions.size()) - 1;
        m_previewDirty       = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(m_selectedTransition < 0 || m_selectedTransition >= static_cast<int>(m_def.transitions.size()));
    if (ImGui::Button(ICON_FA_TRASH "  Remove transition"))
    {
        m_def.transitions.erase(m_def.transitions.begin() + m_selectedTransition);
        m_selectedTransition = -1;
        m_previewDirty       = true;
    }
    ImGui::EndDisabled();

    if (ImGui::BeginTable("hsm_tr", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("From");
        ImGui::TableSetupColumn("Event");
        ImGui::TableSetupColumn("To");
        ImGui::TableSetupColumn("Action");
        ImGui::TableHeadersRow();
        for (uint32_t i = 0; i < m_def.transitions.size(); ++i)
        {
            const HsmTransitionDef& t = m_def.transitions[i];
            ImGui::PushID(static_cast<int>(i) + 4000);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool sel = (m_selectedTransition == static_cast<int>(i));
            const char* from = t.from.empty() ? "?" : t.from[0].c_str();
            char        label[96];
            std::snprintf(label, sizeof(label), "%s##tr%u", from, i);
            if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_SpanAllColumns))
                m_selectedTransition = static_cast<int>(i);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(t.event.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(t.to.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(t.action.empty() ? "-" : t.action.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (m_selectedTransition < 0 || m_selectedTransition >= static_cast<int>(m_def.transitions.size()))
        return;

    HsmTransitionDef& t = m_def.transitions[static_cast<uint32_t>(m_selectedTransition)];
    ImGui::SeparatorText("Selected transition");
    auto names = stateNames(m_def);
    std::string from = t.from.empty() ? std::string() : t.from[0];
    std::vector<std::string> fromItems = names;
    fromItems.insert(fromItems.begin(), "*");
    if (Dark::EditorImGuiHelpers::comboString("From", from, fromItems, false))
    {
        t.from         = { from };
        m_previewDirty = true;
    }
    if (Dark::EditorImGuiHelpers::comboString("To", t.to, names, false))
        m_previewDirty = true;
    std::vector<std::string> eventNames;
    for (const HsmEventDef& e : m_def.events)
        eventNames.push_back(e.name);
    if (Dark::EditorImGuiHelpers::comboString("Event", t.event, eventNames, false))
        m_previewDirty = true;
    char act[64]{};
#if defined(_MSC_VER)
    strncpy_s(act, t.action.c_str(), _TRUNCATE);
#else
    std::strncpy(act, t.action.c_str(), sizeof(act) - 1);
#endif
    if (ImGui::InputText("Action", act, sizeof(act)))
    {
        t.action       = act;
        m_previewDirty = true;
    }
    int kind = static_cast<int>(t.kind);
    const char* kinds[] = { "external", "local", "internal" };
    if (ImGui::Combo("Kind", &kind, kinds, 3))
    {
        t.kind         = static_cast<AI::HsmTransitionKind>(kind);
        m_previewDirty = true;
    }
}

void HsmEditorPanel::drawParams()
{
    if (ImGui::Button(ICON_FA_CIRCLE_PLUS "  Add parameter"))
    {
        HsmParamDef p;
        p.name         = "param" + std::to_string(m_def.params.size() + 1);
        m_def.params.push_back(std::move(p));
        m_previewDirty = true;
    }
    for (uint32_t i = 0; i < m_def.params.size(); ++i)
    {
        HsmParamDef& p = m_def.params[i];
        ImGui::PushID(static_cast<int>(i) + 5000);
        char buf[64]{};
#if defined(_MSC_VER)
        strncpy_s(buf, p.name.c_str(), _TRUNCATE);
#else
        std::strncpy(buf, p.name.c_str(), sizeof(buf) - 1);
#endif
        if (ImGui::InputText("Name", buf, sizeof(buf)))
        {
            p.name         = buf;
            m_previewDirty = true;
        }
        if (ImGui::DragFloat("Default", &p.defaultFloat, 0.05f, 0.0f, 60.0f))
            m_previewDirty = true;
        if (ImGui::Button("Remove"))
        {
            m_def.params.erase(m_def.params.begin() + static_cast<int>(i));
            m_previewDirty = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
}
