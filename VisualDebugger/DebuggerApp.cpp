#include "DebuggerApp.h"

#include "Core/Log.h"
#include "Network/NetTypes.h"

#include <imgui.h>

#include "Input/InputCodes.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace Dark;

namespace
{
    const char* slotName(uint32_t i)
    {
        switch (static_cast<PerfSlot>(i))
        {
        case PerfSlot::Frame:
            return "Frame";
        case PerfSlot::NetPoll:
            return "Net poll";
        case PerfSlot::Update:
            return "Update";
        case PerfSlot::Audio:
            return "Audio";
        case PerfSlot::NetFlush:
            return "Net flush";
        case PerfSlot::DebugFlush:
            return "Debug";
        case PerfSlot::Render:
            return "Render";
        case PerfSlot::Present:
            return "Present";
        default:
            return "?";
        }
    }

    ImVec4 levelColor(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace:
            return ImVec4(0.55f, 0.55f, 0.60f, 1.0f);
        case LogLevel::Info:
            return ImVec4(0.80f, 0.85f, 0.90f, 1.0f);
        case LogLevel::Warn:
            return ImVec4(1.00f, 0.80f, 0.30f, 1.0f);
        case LogLevel::Error:
            return ImVec4(1.00f, 0.40f, 0.35f, 1.0f);
        case LogLevel::Fatal:
            return ImVec4(1.00f, 0.20f, 0.20f, 1.0f);
        default:
            return ImVec4(1, 1, 1, 1);
        }
    }

    void formatBytes(char* buf, uint32_t cap, uint64_t bytes)
    {
        if (bytes >= 1024ull * 1024ull * 1024ull)
            std::snprintf(buf, cap, "%.2f GiB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        else if (bytes >= 1024ull * 1024ull)
            std::snprintf(buf, cap, "%.2f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        else if (bytes >= 1024ull)
            std::snprintf(buf, cap, "%.1f KiB", static_cast<double>(bytes) / 1024.0);
        else
            std::snprintf(buf, cap, "%llu B", static_cast<unsigned long long>(bytes));
    }
} // namespace

DebuggerApp::DebuggerApp(const AppConfig& cfg, Address autoJoin)
    : Application(cfg)
    , m_autoJoin(autoJoin)
{
    for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i)
        m_catEnabled[i] = true;
}

void DebuggerApp::onInit()
{
    renderer().setClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    if (!m_imgui.init(window(), renderer(), "debugger_imgui.ini", true))
        DE_LOG_ERROR(LogCategory::Debug, "VisualDebugger: ImGui init failed");

    if (m_autoJoin.ipv4 != 0)
    {
        std::snprintf(m_joinAddress, sizeof(m_joinAddress), "%u.%u.%u.%u:%u",
                      (m_autoJoin.ipv4 >> 24) & 255u, (m_autoJoin.ipv4 >> 16) & 255u, (m_autoJoin.ipv4 >> 8) & 255u,
                      m_autoJoin.ipv4 & 255u, m_autoJoin.port != 0 ? m_autoJoin.port : kDebugDefaultPort);
        tryConnect();
    }
}

void DebuggerApp::onUpdate(float dt)
{
    m_client.poll(dt);
    if (m_imgui.wantCaptureKeyboard())
        return;
    if (input().keyPressed(Key::Escape))
        requestQuit();
}

void DebuggerApp::tryConnect()
{
    Address addr{};
    addr.port = kDebugDefaultPort;
    if (!parseIPv4(m_joinAddress, addr))
    {
        DE_LOG_ERROR(LogCategory::Debug, "VisualDebugger: invalid address '{}'", m_joinAddress);
        return;
    }
    if (addr.port == 0)
        addr.port = kDebugDefaultPort;
    applySubscribeFromPanels();
    if (!m_client.connect(addr))
        DE_LOG_ERROR(LogCategory::Debug, "VisualDebugger: connect failed");
}

void DebuggerApp::disconnect()
{
    m_client.disconnect();
}

void DebuggerApp::applySubscribeFromPanels()
{
    uint32_t mask = 0;
    if (m_showLog)
        mask |= DebugChannelLog;
    if (m_showMemory)
        mask |= DebugChannelMemory;
    if (m_showPerf)
        mask |= DebugChannelPerf;
    m_client.setSubscribeMask(mask);

    uint32_t catMask = 0;
    const uint32_t catCount = static_cast<uint32_t>(LogCategory::Count);
    for (uint32_t i = 0; i < catCount && i < 32; ++i)
    {
        if (m_catEnabled[i])
            catMask |= (1u << i);
    }
    const LogLevel levels[] = {LogLevel::Trace, LogLevel::Info, LogLevel::Warn, LogLevel::Error, LogLevel::Fatal};
    const int      idx      = (m_minLevelIdx >= 0 && m_minLevelIdx < 5) ? m_minLevelIdx : 0;
    m_client.setLogFilter(levels[idx], catMask);
    m_filterDirty = false;
}

void DebuggerApp::drawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Quit", "Esc"))
            requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Connection", nullptr, &m_showConnection);
        if (ImGui::MenuItem("Log", nullptr, &m_showLog))
            applySubscribeFromPanels();
        if (ImGui::MenuItem("Memory", nullptr, &m_showMemory))
            applySubscribeFromPanels();
        if (ImGui::MenuItem("Performance", nullptr, &m_showPerf))
            applySubscribeFromPanels();
        ImGui::EndMenu();
    }
    if (m_client.isConnected())
        ImGui::TextDisabled("  |  connected to %s", m_client.hostTitle()[0] ? m_client.hostTitle() : "host");
    else if (m_client.isConnecting())
        ImGui::TextDisabled("  |  connecting...");
    else
        ImGui::TextDisabled("  |  disconnected");
    ImGui::EndMainMenuBar();
}

void DebuggerApp::drawConnectionPanel()
{
    if (!m_showConnection)
        return;
    if (!ImGui::Begin("Connection", &m_showConnection))
    {
        ImGui::End();
        return;
    }
    ImGui::InputText("Host", m_joinAddress, sizeof(m_joinAddress));
    const bool connected = m_client.isConnected();
    if (!connected)
    {
        if (ImGui::Button("Connect"))
            tryConnect();
    }
    else if (ImGui::Button("Disconnect"))
        disconnect();

    ImGui::SameLine();
    if (connected)
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Connected");
    else if (m_client.isConnecting())
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "Connecting...");
    else
        ImGui::TextDisabled("Disconnected");

    ImGui::TextUnformatted("LAN only — no authentication");
    ImGui::TextDisabled("Game listens with -debug or F10 (Editor: Debug menu). Port 26162.");
    ImGui::End();
}

void DebuggerApp::drawLogPanel()
{
    if (!m_showLog)
        return;
    if (!ImGui::Begin("Log", &m_showLog))
    {
        ImGui::End();
        applySubscribeFromPanels();
        return;
    }

    const char* levelNames[] = {"Trace", "Info", "Warn", "Error", "Fatal"};
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::Combo("Min level", &m_minLevelIdx, levelNames, 5))
        m_filterDirty = true;

    ImGui::SameLine();
    if (ImGui::Button("All cats"))
    {
        for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i)
            m_catEnabled[i] = true;
        m_filterDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("None"))
    {
        for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i)
            m_catEnabled[i] = false;
        m_filterDirty = true;
    }

    for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i)
    {
        ImGui::SameLine();
        if (ImGui::Checkbox(Log::categoryName(static_cast<LogCategory>(i)), &m_catEnabled[i]))
            m_filterDirty = true;
    }

    ImGui::InputText("Search", m_search, sizeof(m_search));
    ImGui::SameLine();
    ImGui::Checkbox("Autoscroll", &m_autoScroll);
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        m_client.clearLogs();

    if (m_filterDirty)
        applySubscribeFromPanels();

    ImGui::Separator();
    if (ImGui::BeginChild("log_lines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
    {
        const LogLevel levels[] = {LogLevel::Trace, LogLevel::Info, LogLevel::Warn, LogLevel::Error, LogLevel::Fatal};
        const LogLevel minLv    = levels[(m_minLevelIdx >= 0 && m_minLevelIdx < 5) ? m_minLevelIdx : 0];

        for (uint32_t i = 0; i < m_client.logCount(); ++i)
        {
            DebugLogEntry e{};
            if (!m_client.logAt(i, e))
                continue;
            if (static_cast<int>(e.level) < static_cast<int>(minLv) && e.level != LogLevel::Error && e.level != LogLevel::Fatal)
                continue;
            const uint32_t ci = static_cast<uint32_t>(e.category);
            if (ci < static_cast<uint32_t>(LogCategory::Count) && !m_catEnabled[ci])
            {
                if (e.level != LogLevel::Error && e.level != LogLevel::Fatal)
                    continue;
            }
            if (m_search[0] && !std::strstr(e.text, m_search))
                continue;

            ImGui::PushStyleColor(ImGuiCol_Text, levelColor(e.level));
            ImGui::TextUnformatted(Log::levelName(e.level));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", Log::categoryName(e.category));
            ImGui::SameLine();
            ImGui::TextUnformatted(e.text);
            if (e.dropped)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(dropped %u)", e.dropped);
            }
        }
        if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8.0f)
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
    if (!m_showLog)
        applySubscribeFromPanels();
}

void DebuggerApp::drawMemoryPanel()
{
    if (!m_showMemory)
        return;
    if (!ImGui::Begin("Memory", &m_showMemory))
    {
        ImGui::End();
        applySubscribeFromPanels();
        return;
    }

    if (ImGui::BeginTable("pools", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Pool");
        ImGui::TableSetupColumn("Count");
        ImGui::TableSetupColumn("Used");
        ImGui::TableSetupColumn("Capacity");
        ImGui::TableSetupColumn("Fill");
        ImGui::TableHeadersRow();

        DebugMemoryPool pools[kDebugMaxPools]{};
        uint32_t        n = m_client.poolCount();
        for (uint32_t i = 0; i < n && i < kDebugMaxPools; ++i)
            m_client.poolAt(i, pools[i]);

        std::sort(pools, pools + n, [](const DebugMemoryPool& a, const DebugMemoryPool& b) {
            if (a.used != b.used)
                return a.used > b.used;
            return std::strcmp(a.name, b.name) < 0;
        });

        for (uint32_t i = 0; i < n; ++i)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(pools[i].name);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", pools[i].count);
            char used[32]{};
            char cap[32]{};
            formatBytes(used, sizeof(used), pools[i].used);
            formatBytes(cap, sizeof(cap), pools[i].capacity);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(used);
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(cap);
            ImGui::TableSetColumnIndex(4);
            const float frac = (pools[i].capacity > 0) ? static_cast<float>(static_cast<double>(pools[i].used) / static_cast<double>(pools[i].capacity)) : 0.0f;
            ImGui::ProgressBar(frac, ImVec2(-1, 0));
        }
        ImGui::EndTable();
    }
    ImGui::End();
    if (!m_showMemory)
        applySubscribeFromPanels();
}

void DebuggerApp::drawPerfPanel()
{
    if (!m_showPerf)
        return;
    if (!ImGui::Begin("Performance", &m_showPerf))
    {
        ImGui::End();
        applySubscribeFromPanels();
        return;
    }

    const DebugPerfSnapshot& p = m_client.lastPerf();
    ImGui::Text("FPS  %.1f", p.fps);
    ImGui::Text("Frame %.2f ms", p.dtMs);
    ImGui::Text("Draws %u   Tris %u", p.drawCalls, p.triangles);
    ImGui::Text("Net in/out %llu / %llu   RTT %.1f ms", static_cast<unsigned long long>(p.packetsIn), static_cast<unsigned long long>(p.packetsOut), p.rttMs);

    float hist[240]{};
    const uint32_t hn = m_client.frameHistoryCount();
    uint32_t       plotN = hn < 240 ? hn : 240;
    for (uint32_t i = 0; i < plotN; ++i)
        m_client.frameHistoryAt(i, hist[i]);
    if (plotN > 0)
        ImGui::PlotLines("Frame ms", hist, static_cast<int>(plotN), 0, nullptr, 0.0f, 50.0f, ImVec2(-1, 80));

    if (ImGui::BeginTable("phases", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
    {
        ImGui::TableSetupColumn("Phase");
        ImGui::TableSetupColumn("ms");
        ImGui::TableHeadersRow();
        for (uint32_t i = 0; i < kDebugPerfSlotCount; ++i)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(slotName(i));
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", p.phaseMs[i]);
        }
        ImGui::EndTable();
    }
    ImGui::End();
    if (!m_showPerf)
        applySubscribeFromPanels();
}

void DebuggerApp::drawUi()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockSpaceOverViewport(0, vp);

    drawMenuBar();
    drawConnectionPanel();
    drawLogPanel();
    drawMemoryPanel();
    drawPerfPanel();
}

void DebuggerApp::onRender()
{
    renderer().beginFrame();
    if (m_imgui.isReady())
    {
        m_imgui.beginFrame();
        drawUi();
        m_imgui.render(renderer());
    }
    renderer().endFrame();
}

void DebuggerApp::onShutdown()
{
    m_client.disconnect();
    if (m_imgui.isReady())
        m_imgui.shutdown(renderer());
}
