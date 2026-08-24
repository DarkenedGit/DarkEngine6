#include "SandboxApp.h"
#include "Core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int)
{
    Dark::AppConfig cfg{};
    cfg.title  = "DarkEngine6 Sandbox";
    cfg.width  = 2560;
    cfg.height = 1600;
    cfg.vsync  = true;
    Dark::parseNetCommandLine(lpCmdLine, cfg);

    SandboxApp app{cfg};
    app.run();
    return 0;
}
