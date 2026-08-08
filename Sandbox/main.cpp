#include "SandboxApp.h"
#include "Core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <exception>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) 
{
    Dark::AppConfig cfg{};
    cfg.title  = "DarkEngine6 Sandbox";
    cfg.width  = 1280;
    cfg.height = 720;
    cfg.vsync  = true;

    SandboxApp app{cfg};
    app.run();
    return 0;
}
