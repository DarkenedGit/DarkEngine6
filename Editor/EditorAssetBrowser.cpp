#include "EditorApp.h"

#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Editor/EditorInternals.h"
#include "Math/Ray3f.h"
#include "Math/Vector2f.h"
#include "Scene/SceneTypes.h"
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
    SceneObjectType type = SceneObjectType::Cube;
    char            path[192]{};
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

void beginAssetDrag(SceneObjectType type, const char* path, const char* preview)
{
    if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        return;
    AssetDragPayload payload{};
    payload.type = type;
    if (path && path[0])
        strncpy_s(payload.path, path, _TRUNCATE);
    ImGui::SetDragDropPayload(kAssetDragType, &payload, sizeof(payload));
    ImGui::TextUnformatted(preview ? preview : toString(type));
    ImGui::EndDragDropSource();
}

const char* iconForType(SceneObjectType type)
{
    switch (type)
    {
    case SceneObjectType::Sphere:
    case SceneObjectType::Coin:
        return ICON_FA_CIRCLE;
    case SceneObjectType::ParticleEmitter:
        return ICON_FA_BOLT;
    case SceneObjectType::PointLight:
    case SceneObjectType::SpotLight:
        return ICON_FA_LIGHTBULB;
    case SceneObjectType::Player:
    case SceneObjectType::Spawn:
        return ICON_FA_CIRCLE_PLUS;
    case SceneObjectType::Hunter:
    case SceneObjectType::Wolf:
        return ICON_FA_BUG;
    case SceneObjectType::Platform:
        return ICON_FA_LAYER_GROUP;
    default:
        return ICON_FA_CUBE;
    }
}
} // namespace

bool EditorApp::tryPawnTypeForModelPath(std::string_view virt, SceneObjectType& out)
{
    const size_t slash = virt.find_last_of("/\\");
    const std::string_view file = (slash == std::string_view::npos) ? virt : virt.substr(slash + 1);
    const size_t dot = file.find_last_of('.');
    const std::string_view stem = (dot == std::string_view::npos) ? file : file.substr(0, dot);
    if (stem == "wolf")
    {
        out = SceneObjectType::Wolf;
        return true;
    }
    if (stem == "human")
    {
        out = SceneObjectType::Player;
        return true;
    }
    if (stem == "skeleton")
    {
        out = SceneObjectType::Hunter;
        return true;
    }
    return false;
}

void EditorApp::queueAssetPlace(SceneObjectType type, const char* modelPath, bool useMouse)
{
    m_queueAssetPlace = true;
    m_queueAssetType  = type;
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
    SceneObjectType type = m_queueAssetType;
    if (m_queueAssetModel[0])
    {
        authored.modelPath = m_queueAssetModel;
        SceneObjectType pawn{};
        if (tryPawnTypeForModelPath(m_queueAssetModel, pawn))
            type = pawn;
        else
            type = SceneObjectType::Model;
    }

    if (m_sceneMode == SceneMode::Scene2D)
    {
        if (!isScene2DType(type))
        {
            DE_LOG_WARN("Editor: cannot place '{}' in a 2D scene", toString(type));
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
        float col[4]{};
        defaultColor2D(type, col);
        const Entity e = spawnObject(type, Vector3f(p.x, p.y, 0.0f), defaultScale2D(type), Quaternion::IDENTITY, col, nullptr);
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
    placeAtWorld(type, hit, authoredPtr);
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
    queueAssetPlace(drag->type, drag->path[0] ? drag->path : nullptr, true);
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

    auto drawPrimitive = [&](SceneObjectType type, const char* label) {
        const char* icon = iconForType(type);
        char        row[96];
        std::snprintf(row, sizeof(row), "%s  %s", icon, label);
        ImGui::BeginDisabled(!createOk);
        ImGui::Selectable(row);
        if (createOk)
            beginAssetDrag(type, nullptr, label);
        if (createOk && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            queueAssetPlace(type, nullptr, false);
        ImGui::EndDisabled();
    };

    if (ImGui::CollapsingHeader("Place", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (m_sceneMode == SceneMode::Scene2D)
        {
            drawPrimitive(SceneObjectType::Platform, "Platform");
            drawPrimitive(SceneObjectType::Coin, "Coin");
            drawPrimitive(SceneObjectType::Spawn, "Player Spawn");
        }
        else
        {
            drawPrimitive(SceneObjectType::Cube, "Cube");
            drawPrimitive(SceneObjectType::Sphere, "Sphere");
            drawPrimitive(SceneObjectType::ParticleEmitter, "Particle Emitter");
            drawPrimitive(SceneObjectType::PointLight, "Point Light");
            drawPrimitive(SceneObjectType::SpotLight, "Spot Light");
            drawPrimitive(SceneObjectType::Player, "Player");
            drawPrimitive(SceneObjectType::Hunter, "Hunter");
            drawPrimitive(SceneObjectType::Wolf, "Wolf");
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
                std::snprintf(row, sizeof(row), "%s  %s", ICON_FA_CUBE, entry.name.c_str());
                ImGui::BeginDisabled(!createOk);
                ImGui::Selectable(row);
                if (createOk)
                    beginAssetDrag(SceneObjectType::Model, entry.virt.c_str(), entry.name.c_str());
                if (createOk && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    queueAssetPlace(SceneObjectType::Model, entry.virt.c_str(), false);
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
    }

    ImGui::End();
}
