#include "Sandbox2DApp.h"
#include "Core/Log.h"
#include "Core/Version.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int)
{
    Dark::AppConfig cfg{};
    cfg.title        = "DarkEngine6 Sandbox2D";
    cfg.width        = 1280;
    cfg.height       = 720;
    cfg.vsync        = true;
    cfg.netSceneMode = 1;
    cfg.hostId       = "sandbox2d";
    cfg.hostName     = "Sandbox2D";
    cfg.hostVersion  = Dark::kEngineVersion;
    cfg.showSplash   = true;
    Dark::parseNetCommandLine(lpCmdLine, cfg);
    Dark::parseAppCommandLine(lpCmdLine, cfg);

    Sandbox2DApp app{ cfg };
    if (!app.initOk())
    {
        DE_LOG_FATAL("Failed to start");
        return 1;
    }
    app.run();
    return 0;
}
