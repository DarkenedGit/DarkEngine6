#include "EditorApp.h"

#include "Editor/EditorInternals.h"
#include "Editor/EditorObject.h"
#include "Scene/SceneFile.h"
#include "ECS/Components.h"
#include "Network/Replication.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Core/UiPalette.h"
#include "Ui/Icons.h"
#include "Ui/ImGuiTheme.h"
#include "Input/InputCodes.h"
#include "Collision/StaticCollision.h"
#include "Math/AABox3f.h"
#include "Math/AABox2f.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Math/Sphere3f.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Math/Ray3f.h"
#include "Render/LineMesh.h"
#include "Render/MeshGen.h"
#include "Render/TaaJitter.h"
#include "Render/ModelDraw.h"
#include "Assets/Model.h"
#include "Animation/AnimGraphTick.h"
#include "Animation/AnimGraphComponent.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace Dark;
using namespace Dark::EditorDetail;
using namespace Math;

bool EditorApp::netClientLocked()
{
    const NetRole role = network().role();
    return role == NetRole::Joining || role == NetRole::Client;
}

bool EditorApp::netSceneLocked()
{
    return network().role() != NetRole::Idle;
}

bool EditorApp::canHostSession()
{
    return network().role() == NetRole::Idle;
}

bool EditorApp::canJoinSession()
{
    return network().role() == NetRole::Idle && editorObjectCount() == 0;
}

void EditorApp::registerReplicatedProps()
{
    uint32_t n = 0;
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (!isReplicatedProp(so.type))
            return;
        if (network().registerEntity(world(), e, prefabFromType(so.type), ClientId::Host, packRgba8(so.color)))
            ++n;
    });
    DE_LOG_INFO(LogCategory::Networking, "Editor: registered {}/{} props for host", n, kNetMaxReplicated);
}

void EditorApp::hostNetworkSession()
{
    if (!canHostSession())
    {
        DE_LOG_WARN(LogCategory::Networking, "Editor: host requires Idle (disconnect first)");
        return;
    }
    network().setWantsPawn(false);
    network().setSceneMode(m_sceneMode == SceneMode::Scene2D ? 1u : 0u);
    network().setPlayerName("Editor");
    registerReplicatedProps();
    if (!network().host(kNetDefaultPort))
        DE_LOG_ERROR(LogCategory::Networking, "Editor: host session failed");
}

void EditorApp::discardLocalSceneForJoin()
{
    std::vector<Entity> ents;
    collectEditorEntities(ents);
    for (Entity e : ents)
    {
        if (world().has<NetworkedComponent>(e))
            network().unregisterEntity(world(), e);
        else if (world().alive(e))
            world().destroyEntity(e);
    }
    m_emitters.clear();
    m_selected = {};
    m_dragging = false;
}

void EditorApp::joinNetworkSession()
{
    if (!canJoinSession())
    {
        DE_LOG_WARN(LogCategory::Networking, "Editor: join requires an empty {} scene",
                    m_sceneMode == SceneMode::Scene2D ? "2D" : "3D");
        return;
    }
    Address addr{};
    addr.port = kNetDefaultPort;
    if (!parseIPv4(m_joinAddress, addr))
    {
        DE_LOG_ERROR(LogCategory::Networking, "Editor: invalid join IP '{}'", m_joinAddress);
        return;
    }
    if (addr.port == 0)
        addr.port = kNetDefaultPort;
    network().setWantsPawn(false);
    network().setSceneMode(m_sceneMode == SceneMode::Scene2D ? 1u : 0u);
    if (!network().join(addr))
        DE_LOG_ERROR(LogCategory::Networking, "Editor: join failed");
}

void EditorApp::drawNetworkMenu()
{
    if (!ImGui::BeginMenu("Network"))
        return;

    if (network().role() == NetRole::Idle)
        network().browse();

    const NetRole  role     = network().role();
    const bool     hostOk   = canHostSession();
    const bool     joinOk   = canJoinSession();
    const uint32_t peerN    = network().peerCount();
    uint32_t       propN    = 0;
    world().each<EditorObjectComponent>([&](Entity, EditorObjectComponent& so) {
        if (isReplicatedProp(so.type))
            ++propN;
    });

    if (ImGui::MenuItem(ICON_FA_SERVER "  Host Session", "26160", false, hostOk))
        hostNetworkSession();
    if (!hostOk && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Disconnect first");

    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("Join IP", m_joinAddress, sizeof(m_joinAddress));
    if (ImGui::MenuItem(ICON_FA_PLUG "  Join", nullptr, false, joinOk))
        joinNetworkSession();
    if (!joinOk && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Join requires an empty scene of the current mode");

    ImGui::Separator();
    ImGui::TextUnformatted("LAN sessions");
    ImGui::TextDisabled("Same-PC :26161 bind is unreliable — use typed IP / CLI");
    const uint32_t discovered = network().sessionCount();
    if (discovered == 0)
        ImGui::TextDisabled("(none)");
    for (uint32_t i = 0; i < discovered; ++i)
    {
        NetSessionInfo s{};
        if (!network().sessionAt(i, s))
            continue;
        const uint32_t ip = s.address.ipv4;
        char           line[96];
        std::snprintf(line, sizeof(line), "%s  %u.%u.%u.%u:%u  peers %u  %s",
                      s.name[0] ? s.name : "(unnamed)",
                      (ip >> 24) & 255u, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u,
                      s.address.port, s.peerCount, s.sceneMode ? "2D" : "3D");
        const uint8_t wantMode = m_sceneMode == SceneMode::Scene2D ? 1u : 0u;
        const bool    canClick = joinOk && s.sceneMode == wantMode;
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::MenuItem(line, nullptr, false, canClick))
        {
            std::snprintf(m_joinAddress, sizeof(m_joinAddress), "%u.%u.%u.%u:%u",
                          (ip >> 24) & 255u, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u, s.address.port);
            joinNetworkSession();
        }
        if (!canClick && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            if (s.sceneMode != wantMode)
                ImGui::SetTooltip("Host scene mode does not match (switch to an empty %s scene)", s.sceneMode ? "2D" : "3D");
            else
                ImGui::SetTooltip("Join requires an empty scene of the current mode");
        }
        ImGui::PopID();
    }

    if (ImGui::MenuItem(ICON_FA_RIGHT_FROM_BRACKET "  Disconnect", nullptr, false, role != NetRole::Idle))
        network().disconnect();

    ImGui::Separator();
    if (role == NetRole::Host)
        ImGui::Text("Role: Host (%u peers)", peerN);
    else
        ImGui::Text("Role: %s", netRoleLabel(role));
    ImGui::Text("Peers: %u", peerN);
    ImGui::Text("Packets in/out: %llu / %llu",
                static_cast<unsigned long long>(network().packetsIn()),
                static_cast<unsigned long long>(network().packetsOut()));
    ImGui::Text("RTT: %.1f ms", network().rttMs(ClientId::Host));
    ImGui::Text("Replicated: %u/%u", propN, kNetMaxReplicated);
    ImGui::TextUnformatted("LAN only — no authentication");
    ImGui::TextDisabled("Scale gizmo is local-only (not replicated)");
    ImGui::EndMenu();
}

void EditorApp::drawDebugMenu()
{
    if (!ImGui::BeginMenu("Debug"))
        return;

    const bool listening = debug().isListening();
    if (ImGui::MenuItem(listening ? ICON_FA_BUG "  Stop Visual Debugger Listen" : ICON_FA_BUG "  Listen for Visual Debugger", "26162"))
    {
        if (listening)
        {
            debug().shutdown();
            DE_LOG_INFO(LogCategory::Debug, "Editor: Visual Debugger listen stopped");
        }
        else if (debug().listen(kDebugDefaultPort))
            DE_LOG_INFO(LogCategory::Debug, "Editor: Visual Debugger listening TCP {}", debug().boundAddress().port);
        else
            DE_LOG_ERROR(LogCategory::Debug, "Editor: Visual Debugger listen failed");
    }
    if (listening)
        ImGui::Text("Listening TCP %u%s", debug().boundAddress().port, debug().hasClient() ? "  (debugger connected)" : "");
    else
        ImGui::TextUnformatted("Not listening");
    ImGui::TextUnformatted("LAN only — no authentication");
    ImGui::EndMenu();
}

bool EditorApp::onNetSpawn(World& world, Entity e, NetPrefab prefab, const TransformComponent& xf, uint32_t colorRgba8, void* user)
{
    (void)xf;
    auto* self = static_cast<EditorApp*>(user);
    if (!self || !e.valid())
        return false;

    // Same contract as Sandbox: inbound replicas are drawable from ECS.
    const SceneObjectType type = typeFromPrefab(prefab);
    if (!isLocalLightType(type) && !world.has<MeshComponent>(e))
    {
        auto& mc       = world.emplace<MeshComponent>(e);
        mc.matAssetID  = self->m_propMaterial ? self->m_propMaterial->id : NULL_ASSET;
        mc.meshAssetID = NULL_ASSET;
    }
    if (!world.has<EditorObjectComponent>(e))
    {
        EditorObjectComponent so{};
        so.type         = type;
        so.emitterIndex = -1;
        unpackRgba8(colorRgba8, so.color);
        world.emplace<EditorObjectComponent>(e, so);
    }
    return world.has<EditorObjectComponent>(e);
}

void EditorApp::onNetDespawn(World& world, Entity e, NetId id, void* user)
{
    (void)world;
    (void)id;
    auto* self = static_cast<EditorApp*>(user);
    if (!self)
        return;
    if (self->m_selected.valid() && self->m_selected.id() == e.id())
    {
        self->m_selected = {};
        self->m_dragging = false;
    }
}

void EditorApp::onNetPeer(const NetPeerInfo& info, NetPeerEvent event, void* user)
{
    (void)user;
    DE_LOG_INFO(LogCategory::Networking, "Editor: peer {} {} (wantsPawn={})",
                static_cast<unsigned>(info.id),
                event == NetPeerEvent::Joined ? "joined" : "left",
                info.wantsPawn ? 1 : 0);
}
