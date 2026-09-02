#include "SandboxApp.h"
#include "Core/Log.h"
#include "Core/Version.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int)
{
    Dark::AppConfig cfg{};
    cfg.title       = "DarkEngine6 Sandbox";
    cfg.width       = 2560;
    cfg.height      = 1600;
    cfg.vsync       = true;
    cfg.hostId      = "sandbox";
    cfg.hostName    = "Sandbox";
    cfg.hostVersion = Dark::kEngineVersion;
    cfg.showSplash  = true;
    Dark::parseNetCommandLine(lpCmdLine, cfg);
    Dark::parseAppCommandLine(lpCmdLine, cfg);
    Dark::applyDeferredScenePath(cfg, Dark::ScenePath::HdrForward);

    SandboxApp app{cfg};
    if (!app.initOk())
    {
        DE_LOG_FATAL("Failed to start");
        return 1;
    }
    app.run();
    return 0;
}
