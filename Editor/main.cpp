#include "EditorApp.h"
#include "Core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    Dark::AppConfig cfg{};
    cfg.title  = "DarkEngine6 Editor";
    cfg.width  = 1600;
    cfg.height = 900;
    cfg.vsync  = true;

    EditorApp app{cfg};
    app.run();
    return 0;
}
