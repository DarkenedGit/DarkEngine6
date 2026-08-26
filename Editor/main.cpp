#include "EditorApp.h"
#include "Core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int)
{
    Dark::AppConfig cfg{};
    cfg.title  = "DarkEngine6 Editor";
    cfg.width  = 2560;
    cfg.height = 1600;
    cfg.vsync  = true;
    Dark::parseNetCommandLine(lpCmdLine, cfg);

    EditorApp app{cfg};
    if (!app.initOk())
    {
        DE_LOG_FATAL("Failed to start");
        return 1;
    }
    app.run();
    return 0;
}
