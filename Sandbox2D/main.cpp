#include "Sandbox2DApp.h"
#include "Core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int)
{
    Dark::AppConfig cfg{};
    cfg.title  = "DarkEngine6 Sandbox2D";
    cfg.width  = 1280;
    cfg.height = 720;
    cfg.vsync  = true;
    Dark::parseNetCommandLine(lpCmdLine, cfg);

    Sandbox2DApp app{ cfg };
    app.run();
    return 0;
}
