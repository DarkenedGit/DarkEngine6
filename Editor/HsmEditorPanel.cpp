#include "Editor/HsmEditorPanel.h"
#include "Editor/EditorImGui.h"
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
    if (Dark::EditorImGuiHelpers::comboString("Root", m_def.root, names, false))
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

#include "Editor/HsmEditorPanel_More.inl"
