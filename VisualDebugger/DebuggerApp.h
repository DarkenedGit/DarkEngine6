#pragma once

#include "Core/Application.h"
#include "Debug/DebugClient.h"
#include "Ui/ImGuiHost.h"

class DebuggerApp : public Dark::Application
{
public:
    explicit DebuggerApp(const Dark::AppConfig& cfg, Dark::Address autoJoin = {});

    void onInit() override;
    void onUpdate(float dt) override;
    void onRender() override;
    void onShutdown() override;

private:
    void drawUi();
    void drawMenuBar();
    void drawConnectionPanel();
    void drawLogPanel();
    void drawMemoryPanel();
    void drawPerfPanel();
    void applySubscribeFromPanels();
    void tryConnect();
    void disconnect();

    ImGuiHost         m_imgui;
    Dark::DebugClient m_client;
    Dark::Address     m_autoJoin{};

    char m_joinAddress[64] = "127.0.0.1:26162";
    bool m_showLog         = true;
    bool m_showMemory      = true;
    bool m_showPerf        = true;
    bool m_showConnection  = true;
    bool m_autoScroll      = true;
    bool m_filterDirty     = true;

    int      m_minLevelIdx = 0;
    bool     m_catEnabled[static_cast<int>(Dark::LogCategory::Count)]{};
    char     m_search[64]{};
};

