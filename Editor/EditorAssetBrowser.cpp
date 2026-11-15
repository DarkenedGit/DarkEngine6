#include "EditorApp.h"

#include "Content/ContentDb.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Editor/EditorInternals.h"
#include "Math/Ray3f.h"
#include "Math/Vector2f.h"
#include "Scene/SceneTypes.h"
#include "Editor/EditorTypeIcon.h"
#include "Ui/Icons.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using namespace Dark;
using namespace Dark::EditorDetail;
using namespace Math;

namespace
{
constexpr const char* kAssetDragType = "DE_ASSET";

struct AssetDragPayload
{
    char archetype[96]{};
    char path[192]{};
};

bool containsInsensitive(std::string_view hay, std::string_view needle)
{
    if (needle.empty())
        return true;
    if (needle.size() > hay.size())
        return false;
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i)
    {
        bool ok = true;
        for (size_t j = 0; j < needle.size(); ++j)
        {
            if (lower(static_cast<unsigned char>(hay[i + j])) != lower(static_cast<unsigned char>(needle[j])))
            {
                ok = false;
                break;
            }
        }
        if (ok)
            return true;
    }
    return false;
}

void beginAssetDrag(std::string_view archetypeId, const char* path, const char* preview, uint64_t iconId)
{
    if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        return;
    AssetDragPayload payload{};
    if (!archetypeId.empty())
    {
        const size_t n = archetypeId.size() < sizeof(payload.archetype) - 1 ? archetypeId.size() : sizeof(payload.archetype) - 1;
        std::memcpy(payload.archetype, archetypeId.data(), n);
        payload.archetype[n] = '\0';
    }
    if (path && path[0])
        strncpy_s(payload.path, path, _TRUNCATE);
    ImGui::SetDragDropPayload(kAssetDragType, &payload, sizeof(payload));

    if (iconId != 0)
    {
        ImGui::Image(static_cast<ImTextureID>(iconId), ImVec2(48.0f, 48.0f));
        ImGui::SameLine();
    }

    ImGui::TextUnformatted(preview && preview[0] ? preview : payload.archetype);
    ImGui::EndDragDropSource();
}
} // namespace

std::string EditorApp::archetypeForModelPath(std::string_view virt)
{
    const size_t slash = virt.find_last_of("/\\");
    const std::string_view file = (slash == std::string_view::npos) ? virt : virt.substr(slash + 1);
    const size_t dot = file.find_last_of('.');
    const std::string_view stem = (dot == std::string_view::npos) ? file : file.substr(0, dot);
    const Archetype* match = archetypeMatchingModelStem(stem);
    if (!match || match->name.empty())
        return "core:model";
    bool character = false;
    bool solid = false;
    for (const ArchetypeBehavior& behavior : match->behaviors)
    {
        if (behavior.name == "player.controller" || behavior.name.starts_with("ai."))
            character = true;
        if (behavior.name == "prop.solid")
            solid = true;
    }
    if (character || !solid)
        return match->name;
    return "core:model";
}

void EditorApp::queueAssetPlace(std::string_view archetypeId, const char* modelPath, bool useMouse)
{
    m_queueAssetPlace = true;
    m_queueAssetArchetype = archetypeId.empty() ? std::string("core:cube") : std::string(archetypeId);
    m_queueAssetModel[0] = 0;
    if (modelPath && modelPath[0])
        strncpy_s(m_queueAssetModel, modelPath, _TRUNCATE);
    m_queueAssetUseMouse = useMouse;
    if (useMouse)
    {
        m_queueAssetMouseX = input().mouseX();
        m_queueAssetMouseY = input().mouseY();
    }
}

void EditorApp::flushQueuedAssetPlace()
{
    SceneObjectData authored{};
    std::string id = m_queueAssetArchetype.empty() ? std::string("core:cube") : m_queueAssetArchetype;
    if (m_queueAssetModel[0])
    {
        authored.modelPath = m_queueAssetModel;
        id = archetypeForModelPath(m_queueAssetModel);
    }

    if (m_sceneMode == SceneMode::Scene2D)
    {
        SceneObjectData probe{};
        probe.archetype = id;
        if (!editorShownInMode(probe, "2d"))
        {
            DE_LOG_WARN("Editor: cannot place '{}' in a 2D scene", id);
            return;
        }
        Vector2f p{};
        if (m_queueAssetUseMouse)
        {
            p = m_camera2D.ScreenToWorld(
                Vector2f(static_cast<float>(m_queueAssetMouseX), static_cast<float>(m_queueAssetMouseY)),
                static_cast<float>(renderer().width()),
                static_cast<float>(renderer().height()));
        }
        else
            worldFromMouse2D(p);
        if (m_gridSnap > 0.0f)
        {
            p.x = snap(p.x, m_gridSnap);
            p.y = snap(p.y, m_gridSnap);
        }
        float col[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        editorDefaultColor(id, col);
        Vector3f scale{ 1.0f, 1.0f, 1.0f };
        editorDefaultScale(id, scale);
        const Entity e = spawnObject(id, Vector3f(p.x, p.y, 0.0f), scale, Quaternion::IDENTITY, col, nullptr);
        if (e.valid())
            audio().play3D(m_sfxPlace, Vector3f(p.x, p.y, 0.0f), 0.65f);
        return;
    }

    Vector3f hit{};
    bool     ok = false;
    if (m_queueAssetUseMouse)
    {
        const Ray3f ray = m_camera.ScreenPointToRay(
            static_cast<float>(m_queueAssetMouseX),
            static_cast<float>(m_queueAssetMouseY),
            static_cast<float>(renderer().width()),
            static_cast<float>(renderer().height()));
        ok = groundHitFromRay(ray, hit);
    }
    if (!ok)
    {
        const Ray3f ray(m_camera.GetPosition(), m_camera.GetLook());
        ok = groundHitFromRay(ray, hit);
    }
    if (!ok)
    {
        DE_LOG_WARN("Editor: asset place failed (no ground hit)");
        return;
    }
    const SceneObjectData* authoredPtr = authored.modelPath.empty() ? nullptr : &authored;
    placeAtWorld(id, hit, authoredPtr);
}

void EditorApp::refreshAssetListing()
{
    m_assetEntries.clear();
    m_assetListDirty = false;

    if (m_assetRoot.empty())
    {
        for (const auto& candidate : contentRootCandidates())
        {
            std::error_code ec;
            if (std::filesystem::is_directory(candidate, ec) && !ec)
            {
                m_assetRoot = candidate;
                break;
            }
        }
    }
    if (m_assetRoot.empty())
        return;

    std::filesystem::path dir = m_assetRoot;
    if (!m_assetRelPath.empty())
        dir /= m_assetRelPath;

    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec) || ec)
        return;

    for (const auto& it : std::filesystem::directory_iterator(dir, ec))
    {
        if (ec)
            break;
        const std::string name = it.path().filename().string();
        if (name.empty() || name[0] == '.')
            continue;
        std::error_code typeEc;
        AssetEntry      entry{};
        entry.name = name;
        if (it.is_directory(typeEc) && !typeEc)
        {
            entry.folder = true;
            m_assetEntries.push_back(std::move(entry));
            continue;
        }
        std::string ext = it.path().extension().string();
        for (char& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".gltf" && ext != ".glb")
            continue;
        entry.placeable = true;
        if (m_assetRelPath.empty())
            entry.virt = name;
        else
        {
            entry.virt = m_assetRelPath;
            entry.virt.push_back('/');
            entry.virt += name;
        }
        m_assetEntries.push_back(std::move(entry));
    }

    std::sort(m_assetEntries.begin(), m_assetEntries.end(), [](const AssetEntry& a, const AssetEntry& b) {
        if (a.folder != b.folder)
            return a.folder && !b.folder;
        return a.name < b.name;
    });
}

void EditorApp::drawAssetDropTarget()
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload || !payload->IsDataType(kAssetDragType) || payload->DataSize != sizeof(AssetDragPayload))
        return;
    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        return;

    // A fullscreen overlay becomes a black OS viewport (ViewportsEnable). Accept the drop
    // when the cursor is over the passthrough 3D view, not an editor panel.
    const ImGuiWindow* hovered = ImGui::GetCurrentContext() ? ImGui::GetCurrentContext()->HoveredWindow : nullptr;
    if (hovered)
    {
        const ImGuiWindowFlags skip = ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground;
        if ((hovered->Flags & skip) == 0)
            return;
    }

    const auto* drag = static_cast<const AssetDragPayload*>(payload->Data);
    queueAssetPlace(drag->archetype, drag->path[0] ? drag->path : nullptr, true);
}

void EditorApp::drawAssetBrowser()
{
    if (!m_showAssetBrowser)
        return;
    if (m_assetListDirty)
        refreshAssetListing();

    if (!ImGui::Begin("Assets", &m_showAssetBrowser))
    {
        ImGui::End();
        return;
    }

    const bool createOk = !netClientLocked() && !m_playMode;
    ImGui::TextDisabled("Drag onto the viewport to place. Double-click places at the camera look.");
    if (ImGui::Button(ICON_FA_ROTATE_RIGHT "  Refresh"))
    {
        m_assetRoot.clear();
        m_assetListDirty = true;
        refreshAssetListing();
    }
    ImGui::SameLine();
    if (ImGui::Button("..") && !m_assetRelPath.empty())
    {
        const size_t slash = m_assetRelPath.find_last_of('/');
        m_assetRelPath     = (slash == std::string::npos) ? std::string() : m_assetRelPath.substr(0, slash);
        m_assetListDirty   = true;
        refreshAssetListing();
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##asset_filter", "Filter", m_assetFilter, sizeof(m_assetFilter));
    ImGui::Text("content/%s", m_assetRelPath.empty() ? "" : m_assetRelPath.c_str());
    ImGui::Separator();

    const std::string_view filter = m_assetFilter;

    if (ImGui::CollapsingHeader("Place", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const char* modeName = (m_sceneMode == SceneMode::Scene2D) ? "2d" : "3d";
        std::vector<const Archetype*> placeable;
        for (const Archetype& archetype : archetypes())
        {
            if (archetypeVisibleInMode(archetype, modeName))
                placeable.push_back(&archetype);
        }
        std::sort(placeable.begin(), placeable.end(), [](const Archetype* a, const Archetype* b) {
            if (a->editor.hotkey != b->editor.hotkey)
                return a->editor.hotkey < b->editor.hotkey;
            return a->name < b->name;
        });
        for (const Archetype* archetype : placeable)
        {
            const std::string label = archetype->editor.label.empty() ? archetype->name : archetype->editor.label;
            const uint64_t icon = archetypeIconId(archetype->name);
            char row[192];
            ImGui::BeginDisabled(!createOk);
            ImGui::PushID(archetype->name.c_str());
            if (drawTypeIconImage(icon))
                std::snprintf(row, sizeof(row), "%s", label.c_str());
            else
                std::snprintf(row, sizeof(row), "%s  %s", iconForContentName(archetype->editor.icon), label.c_str());
            ImGui::Selectable(row);
            if (createOk)
                beginAssetDrag(archetype->name, nullptr, label.c_str(), icon);
            if (createOk && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                queueAssetPlace(archetype->name, nullptr, false);
            ImGui::PopID();
            ImGui::EndDisabled();
        }
    }

    ImGui::Separator();
    if (m_assetRoot.empty())
        ImGui::TextWrapped("No content/ folder found.");
    else
    {
        for (const AssetEntry& entry : m_assetEntries)
        {
            if (!containsInsensitive(entry.name, filter))
                continue;
            ImGui::PushID(entry.name.c_str());
            if (entry.folder)
            {
                char row[160];
                if (drawTypeIconImage(m_folderIconId))
                    std::snprintf(row, sizeof(row), "%s", entry.name.c_str());
                else
                    std::snprintf(row, sizeof(row), "%s  %s", ICON_FA_FOLDER, entry.name.c_str());
                if (ImGui::Selectable(row))
                {
                    if (!m_assetRelPath.empty())
                        m_assetRelPath.push_back('/');
                    m_assetRelPath += entry.name;
                    m_assetListDirty = true;
                    refreshAssetListing();
                    ImGui::PopID();
                    break;
                }
            }
            else
            {
                char row[160];
                const uint64_t modelIcon = archetypeIconId("core:model");
                const uint64_t shown = modelIcon != 0 ? modelIcon : m_fileIconId;
                if (drawTypeIconImage(shown))
                    std::snprintf(row, sizeof(row), "%s", entry.name.c_str());
                else
                    std::snprintf(row, sizeof(row), "%s  %s", ICON_FA_CUBE, entry.name.c_str());
                ImGui::BeginDisabled(!createOk);
                ImGui::Selectable(row);
                if (createOk)
                    beginAssetDrag("core:model", entry.virt.c_str(), entry.name.c_str(), modelIcon);
                if (createOk && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    queueAssetPlace("core:model", entry.virt.c_str(), false);
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
    }

    ImGui::End();
}
