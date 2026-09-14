#include "Editor/HsmEditorPanel.h"
#include "AI/HsmGraphJson.h"
#include "Editor/HsmStatechart.h"
#include "Assets/ImageCache.h"
#include "Core/Log.h"
#include "Ui/Icons.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

using namespace Dark;

namespace
{
std::string uniqueName(const std::vector<std::string>& used, const char* base)
{
    std::string name = base;
    int         n    = 2;
    auto        taken = [&](const std::string& s) {
        for (const std::string& u : used)
        {
            if (u == s)
                return true;
        }
        return false;
    };
    while (taken(name))
        name = std::string(base) + std::to_string(n++);
    return name;
}

std::vector<std::string> stateNames(const HsmGraphDef& def)
{
    std::vector<std::string> names;
    names.reserve(def.states.size());
    for (const HsmStateDef& st : def.states)
        names.push_back(st.name);
    return names;
}

bool comboString(const char* label, std::string& value, const std::vector<std::string>& items, bool allowEmpty)
{
    const char* preview = value.empty() ? (allowEmpty ? "(none)" : "(invalid)") : value.c_str();
    bool        changed = false;
    if (!ImGui::BeginCombo(label, preview))
        return false;
    if (allowEmpty)
    {
        const bool sel = value.empty();
        if (ImGui::Selectable("(none)", sel))
        {
            value   = {};
            changed = true;
        }
    }
    for (const std::string& item : items)
    {
        const bool sel = (item == value);
        if (ImGui::Selectable(item.c_str(), sel))
        {
            value   = item;
            changed = true;
        }
        if (sel)
            ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return changed;
}

} // namespace

void HsmEditorPanel::rebuildPreview()
{
    m_preview.reset();
    if (m_def.states.empty())
        return;
    if (!m_preview.build(m_def))
    {
        std::snprintf(m_status, sizeof(m_status), "Preview build failed — check parents/events");
        return;
    }
    m_preview.start();
    m_previewDirty = false;
}

void HsmEditorPanel::loadTemplate(const HsmGraphDef& templ, const char* virtualPath)
{
    m_def          = templ;
    m_virtualPath  = virtualPath ? virtualPath : "";
    m_selectedState      = 0;
    m_selectedTransition = -1;
    m_selectedEvent      = 0;
    m_previewDirty       = true;
    m_chartFitNext       = true;
    rebuildPreview();
    std::snprintf(m_status, sizeof(m_status), "Loaded template %s", m_def.name.c_str());
}

void HsmEditorPanel::loadVirtual(AssetManager& assets, const std::string& virtualPath)
{
    AssetRef<HsmGraphDef> graph = assets.tryLoadHsmGraph(virtualPath);
    if (!graph)
    {
        std::snprintf(m_status, sizeof(m_status), "Failed to load %s", virtualPath.c_str());
        return;
    }
    m_def         = *graph;
    m_virtualPath = virtualPath;
    m_def.sourcePath = virtualPath;
    m_selectedState      = 0;
    m_selectedTransition = -1;
    m_selectedEvent      = 0;
    m_previewDirty       = true;
    m_chartFitNext       = true;
    rebuildPreview();
    std::snprintf(m_status, sizeof(m_status), "Loaded %s", virtualPath.c_str());
}

bool HsmEditorPanel::save(AssetManager& assets)
{
    std::filesystem::path path;
    if (!m_virtualPath.empty())
        path = assets.resolve(m_virtualPath);
    if (path.empty() && !m_def.sourcePath.empty())
        path = std::filesystem::path(m_def.sourcePath);
    if (path.empty() && !m_virtualPath.empty())
    {
        const std::filesystem::path existing = assets.resolve("ai/hunter.hsm.json");
        if (!existing.empty())
            path = existing.parent_path() / std::filesystem::path(m_virtualPath).filename();
        else
            path = std::filesystem::path(m_virtualPath);
    }
    if (path.empty())
    {
        std::snprintf(m_status, sizeof(m_status), "No path to save — load hunter or player first");
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    m_def.sourcePath = m_virtualPath.empty() ? path.generic_string() : m_virtualPath;
    if (!saveHsmGraphJsonFile(m_def, path))
    {
        std::snprintf(m_status, sizeof(m_status), "Save failed: %s", path.string().c_str());
        return false;
    }
    assets.registerAsset(std::make_shared<HsmGraphDef>(m_def), ImageCache::normalizePath(path));
    std::snprintf(m_status, sizeof(m_status), "Saved %s", path.filename().string().c_str());
    return true;
}

void HsmEditorPanel::draw(AssetManager& assets, bool* open)
{
    if (open && !*open)
        return;
    if (!ImGui::Begin("HSM", open))
    {
        ImGui::End();
        return;
    }

    if (m_def.states.empty())
    {
        loadVirtual(assets, "ai/hunter.hsm.json");
        if (m_def.states.empty())
            loadTemplate(makeHunterHsmGraph(), "ai/hunter.hsm.json");
    }

    if (ImGui::Button("Hunter"))
        loadVirtual(assets, "ai/hunter.hsm.json");
    ImGui::SameLine();
    if (ImGui::Button("Player"))
        loadVirtual(assets, "ai/player.hsm.json");
    ImGui::SameLine();
    if (ImGui::Button("New hunter"))
        loadTemplate(makeHunterHsmGraph(), "ai/hunter.hsm.json");
    ImGui::SameLine();
    if (ImGui::Button("New player"))
        loadTemplate(makePlayerHsmGraph(), "ai/player.hsm.json");

    ImGui::TextUnformatted(m_virtualPath.empty() ? "(unsaved)" : m_virtualPath.c_str());
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save"))
        save(assets);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Reload") && !m_virtualPath.empty())
        loadVirtual(assets, m_virtualPath);

    char nameBuf[64]{};
#if defined(_MSC_VER)
    strncpy_s(nameBuf, m_def.name.c_str(), _TRUNCATE);
#else
    std::strncpy(nameBuf, m_def.name.c_str(), sizeof(nameBuf) - 1);
#endif
    if (ImGui::InputText("Graph name", nameBuf, sizeof(nameBuf)))
        m_def.name = nameBuf;

    const auto names = stateNames(m_def);
    if (comboString("Root", m_def.root, names, false))
        m_previewDirty = true;

    if (m_previewDirty)
        rebuildPreview();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool   wide  = avail.x >= 720.0f;
    if (m_splitFrac < 0.28f)
        m_splitFrac = 0.28f;
    if (m_splitFrac > 0.78f)
        m_splitFrac = 0.78f;

    if (wide)
    {
        const float splitPx = 6.0f;
        const float leftW   = (avail.x - splitPx) * m_splitFrac;
        ImGui::BeginChild("hsm_chart_pane", ImVec2(leftW, 0.0f), ImGuiChildFlags_Borders);
        ImGui::SeparatorText("Statechart");
        drawPreview();
        ImGui::Separator();
        drawChart();
        ImGui::EndChild();

        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton("hsm_split", ImVec2(splitPx, avail.y > 8.0f ? avail.y : 8.0f));
        if (ImGui::IsItemActive())
            m_splitFrac += ImGui::GetIO().MouseDelta.x / avail.x;
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        ImGui::SameLine(0.0f, 0.0f);
        ImGui::BeginChild("hsm_edit_pane", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        ImGui::SeparatorText("Edit lists");
        drawStates();
        ImGui::Separator();
        drawTransitions();
        ImGui::Separator();
        drawEvents();
        ImGui::Separator();
        drawParams();
        ImGui::EndChild();
    }
    else
    {
        const float topH = avail.y * 0.48f;
        ImGui::BeginChild("hsm_chart_pane", ImVec2(0.0f, topH > 180.0f ? topH : 180.0f), ImGuiChildFlags_Borders);
        ImGui::SeparatorText("Statechart");
        drawPreview();
        drawChart();
        ImGui::EndChild();
        ImGui::BeginChild("hsm_edit_pane", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        ImGui::SeparatorText("Edit lists");
        drawStates();
        ImGui::Separator();
        drawTransitions();
        ImGui::Separator();
        drawEvents();
        ImGui::Separator();
        drawParams();
        ImGui::EndChild();
    }

    if (m_status[0])
    {
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_status);
    }
    ImGui::End();
}

void HsmEditorPanel::drawChart()
{
    if (ImGui::Button("Fit view"))
        m_chartFitNext = true;
    ImGui::SameLine();
    ImGui::TextDisabled("zoom %.2f", m_chartZoom);
    const bool fit = m_chartFitNext;
    m_chartFitNext = false;
    if (drawHsmStatechart(m_def, &m_preview, m_selectedState, m_chartPan, m_chartZoom, fit))
    {
        m_selectedTransition = -1;
    }
}

void HsmEditorPanel::drawPreview()
{
    ImGui::Text("Leaf: %s", m_preview.leafName());
    std::string path;
    for (const std::string& n : m_preview.activePathNames())
    {
        if (!path.empty())
            path += " / ";
        path += n;
    }
    ImGui::TextWrapped("Path: %s", path.empty() ? "(stopped)" : path.c_str());
    if (ImGui::Button(ICON_FA_ROTATE_RIGHT "  Restart"))
    {
        m_preview.stop();
        m_preview.start();
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Fire event");
    for (uint32_t i = 0; i < m_def.events.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i) + 30000);
        if (ImGui::Button(m_def.events[i].name.c_str()))
            m_preview.processEventNamed(m_def.events[i].name);
        if (i + 1 < m_def.events.size())
            ImGui::SameLine();
        ImGui::PopID();
    }
}

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
    if (comboString("Parent", st.parent, names, true))
        m_previewDirty = true;
    if (comboString("Initial child", st.initial, names, true))
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
    if (comboString("From", from, fromItems, false))
    {
        t.from         = { from };
        m_previewDirty = true;
    }
    if (comboString("To", t.to, names, false))
        m_previewDirty = true;
    std::vector<std::string> eventNames;
    for (const HsmEventDef& e : m_def.events)
        eventNames.push_back(e.name);
    if (comboString("Event", t.event, eventNames, false))
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
