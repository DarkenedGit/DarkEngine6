#include "SandboxApp.h"

#include "Core/Log.h"
#include "Debug/DebugTypes.h"
#include "Network/NetTypes.h"
#include "Render/DebugRenderState.h"
#include "Render/ScenePath.h"
#include "Sky/Environment.h"

#include <imgui.h>

#include <cstdio>

using namespace Dark;
using namespace Math;

namespace
{

const char* roleLabel(NetRole role)
{
    switch (role)
    {
    case NetRole::Idle:
        return "Idle";
    case NetRole::Joining:
        return "Joining";
    case NetRole::Host:
        return "Host";
    case NetRole::Client:
        return "Client";
    default:
        return "?";
    }
}

void formatIPv4(char* out, size_t cap, const Address& addr)
{
    const uint32_t ip = addr.ipv4;
    std::snprintf(out, cap, "%u.%u.%u.%u:%u", (ip >> 24) & 255u, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u, addr.port);
}

} // namespace

void SandboxApp::drawPauseOverlay()
{
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 16.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    if (ImGui::Begin("##pausedBanner", nullptr, flags))
        ImGui::TextUnformatted("PAUSED  fly cam  WASD move  Q/Space up  Z/Ctrl down  Shift sprint  mouse look  O step  P resume");
    ImGui::End();
}

void SandboxApp::drawDevTools()
{
    ImGui::SetNextWindowSize(ImVec2(440.0f, 620.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(28.0f, 28.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Dev Tools", &m_showDevTools))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Checkbox("Pause gameplay (fly cam)", &m_gameplayPaused))
    {
        m_stepGameplay = false;
        DE_LOG_INFO("Sandbox: gameplay paused = {}", m_gameplayPaused);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("P");
    ImGui::BeginDisabled(!m_gameplayPaused);
    if (ImGui::Button("Step"))
        m_stepGameplay = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("O");
    ImGui::Separator();

    DebugRenderState& dbg = renderer().debugState();
    const float elevDeg   = m_env.sunElevation() * 57.2957795f;

    ImGui::TextUnformatted(renderer().scenePath() == ScenePath::HybridDeferred ? "Path: hybrid deferred" : "Path: forward");
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::Text("%.0f fps", static_cast<double>(ImGui::GetIO().Framerate));
    ImGui::Text("Draws %u  tris %u  sun %.1f deg", renderer().stats().drawCalls, renderer().stats().triangles, static_cast<double>(elevDeg));
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int fill = static_cast<int>(dbg.fill);
        if (ImGui::RadioButton("Solid", fill == 0))
        {
            dbg.fill = DebugFill::Solid;
            DE_LOG_INFO("Sandbox: fill = {}", toString(dbg.fill));
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Wireframe", fill == 1))
        {
            dbg.fill = DebugFill::Wireframe;
            DE_LOG_INFO("Sandbox: fill = {}", toString(dbg.fill));
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Points", fill == 2))
        {
            dbg.fill = DebugFill::Points;
            DE_LOG_INFO("Sandbox: fill = {}", toString(dbg.fill));
        }

        if (ImGui::Checkbox("Lighting", &dbg.lighting))
            DE_LOG_INFO("Sandbox: lighting = {}", dbg.lighting);

        bool shadows = dbg.shadows;
        if (ImGui::Checkbox("Shadows", &shadows))
        {
            dbg.shadows = shadows;
            m_shadows.setDebugEnabled(shadows);
            DE_LOG_INFO("Sandbox: shadows = {}", shadows);
        }
        if (ImGui::Checkbox("Shadow map tiles", &m_showShadowMaps))
            DE_LOG_INFO("Sandbox: shadow map overlay = {}", m_showShadowMaps);
        if (ImGui::Checkbox("Depth tile", &m_showDepth))
            DE_LOG_INFO("Sandbox: depth overlay = {}", m_showDepth);
        if (renderer().hasGBuffer())
        {
            if (ImGui::Checkbox("G-buffer tiles", &m_showGBuffer))
                DE_LOG_INFO("Sandbox: G-buffer overlay = {}", m_showGBuffer);
            if (ImGui::Checkbox("Velocity tile", &m_showVelocity))
                DE_LOG_INFO("Sandbox: velocity overlay = {}", m_showVelocity);
        }
        if (renderer().hasSceneBuffers())
        {
            if (ImGui::Checkbox("ACES tonemap", &dbg.aces))
                DE_LOG_INFO("Sandbox: ACES = {}", dbg.aces);
        }
        if (renderer().hasGBuffer())
        {
            if (ImGui::Checkbox("TAA", &dbg.taa))
                DE_LOG_INFO("Sandbox: TAA = {}", dbg.taa);
            if (ImGui::Checkbox("Motion blur", &dbg.motionBlur))
                DE_LOG_INFO("Sandbox: motion blur = {}", dbg.motionBlur);
        }
    }

    if (ImGui::CollapsingHeader("Sky", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const Vector3f lc = m_env.lightColor();
        ImGui::ColorButton("##light", ImVec4(lc.x, lc.y, lc.z, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(18.0f, 18.0f));
        ImGui::SameLine();
        ImGui::Text("Light  ambient (%.2f, %.2f, %.2f)", static_cast<double>(m_env.ambientColor().x), static_cast<double>(m_env.ambientColor().y),
                    static_cast<double>(m_env.ambientColor().z));

        float tod = m_env.timeOfDay;
        if (ImGui::SliderFloat("Time of day", &tod, 0.0f, 23.99f, "%.2f h"))
        {
            m_env.timeOfDay = tod;
            m_env.evaluate();
        }
        bool animate = m_env.timeScale > 0.0f;
        if (ImGui::Checkbox("Animate time", &animate))
        {
            m_env.timeScale = animate ? 0.35f : 0.0f;
            DE_LOG_INFO("Sky: time scale = {:.2f} h/s", m_env.timeScale);
        }

        auto weatherBtn = [&](const char* label, const Sky::WeatherState& w, const ImVec4& col) {
            ImGui::PushStyleColor(ImGuiCol_Button, col);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x + 0.12f, col.y + 0.12f, col.z + 0.12f, 1.0f));
            if (ImGui::Button(label, ImVec2(92.0f, 28.0f)))
            {
                m_env.weather = w;
                m_env.evaluate();
                DE_LOG_INFO("Sky: weather {}", label);
            }
            ImGui::PopStyleColor(2);
        };
        weatherBtn("Clear", Sky::WeatherState::Clear(), ImVec4(0.20f, 0.45f, 0.72f, 1.0f));
        ImGui::SameLine();
        weatherBtn("Partly", Sky::WeatherState::PartlyCloudy(), ImVec4(0.32f, 0.42f, 0.55f, 1.0f));
        ImGui::SameLine();
        weatherBtn("Overcast", Sky::WeatherState::Overcast(), ImVec4(0.38f, 0.40f, 0.42f, 1.0f));
        ImGui::SameLine();
        weatherBtn("Storm", Sky::WeatherState::Storm(), ImVec4(0.28f, 0.22f, 0.40f, 1.0f));
        ImGui::Text("Cover %.0f%%  fog %.3f  exposure %.2f", static_cast<double>(m_env.weather.cloudCoverage * 100.0f), static_cast<double>(m_env.fogDensity()),
                    static_cast<double>(m_env.exposure()));
    }

    if (ImGui::CollapsingHeader("Network", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const NetRole role = network().role();
        ImGui::Text("Role: %s   peers %u   rtt %.0f ms", roleLabel(role), network().peerCount(), static_cast<double>(network().rttMs(network().localClientId())));
        if (ImGui::Button("Host", ImVec2(88.0f, 0.0f)))
            devNetHost();
        ImGui::SameLine();
        if (ImGui::Button("Disconnect", ImVec2(100.0f, 0.0f)))
            devNetDisconnect();
        ImGui::SameLine();
        if (ImGui::Button("Browse LAN", ImVec2(100.0f, 0.0f)))
            devNetBrowse();

        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputText("##joinHost", m_joinHost, sizeof(m_joinHost));
        ImGui::SameLine();
        if (ImGui::Button("Join"))
        {
            Address addr{};
            addr.port = kNetDefaultPort;
            if (parseIPv4(m_joinHost, addr))
                devNetJoin(addr);
            else
                DE_LOG_WARN(LogCategory::Networking, "Sandbox: bad join address '{}'", m_joinHost);
        }

        const uint32_t n = network().sessionCount();
        if (m_netBrowsing)
            ImGui::Text("LAN sessions: %u", n);
        for (uint32_t i = 0; i < n; ++i)
        {
            NetSessionInfo s{};
            if (!network().sessionAt(i, s))
                continue;
            char ip[48];
            formatIPv4(ip, sizeof(ip), s.address);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(s.name[0] ? s.name : "(unnamed)"))
                devNetJoin(s.address);
            ImGui::SameLine();
            ImGui::TextDisabled("%s  peers %u", ip, s.peerCount);
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Debugger", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const bool listening = debug().isListening();
        ImGui::Text("Visual Debugger: %s", listening ? "listening" : "off");
        if (listening)
            ImGui::Text("TCP %u   client %s", debug().boundAddress().port, debug().hasClient() ? "connected" : "waiting");
        if (ImGui::Button(listening ? "Stop listen" : "Listen"))
            devToggleListen();
        ImGui::SameLine();
        ImGui::TextDisabled("port %u", kDebugDefaultPort);
    }

    ImGui::Separator();
    ImGui::TextDisabled("M hides this panel and recaptures the mouse.");
    ImGui::End();
}
