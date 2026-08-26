#pragma once
#include "Core/Log.h"
#include "Core/Window.h"
#include "ECS/World.h"
#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
#include "Network/NetworkSystem.h"
#include "Debug/DebugServer.h"
#include "Render/Renderer.h"
#include "Input/Input.h"

namespace Dark
{
    struct AppConfig
    {
        const char* title  = "DarkEngine6";
        uint32_t    width  = 2560;
        uint32_t    height = 1600;
        bool        vsync  = true;

        bool     netHost      = false;
        uint16_t netHostPort  = kNetDefaultPort;
        Address  netJoin{};          // ipv4==0 && port==0 → do not join
        uint8_t  netSceneMode = 0;   // 0 = 3D, 1 = 2D; apps may override

        bool     debugListen     = false;
        uint16_t debugListenPort = kDebugDefaultPort;

        bool        showSplash    = false;
        const char* hostId        = "app";
        const char* hostName      = nullptr;
        const char* hostVersion   = nullptr;
        const char* loadingConfig = nullptr;
    };

    bool parseNetCommandLine(const char* lpCmdLine, AppConfig& cfg);

    class Application
    {
    public:
        explicit Application(const AppConfig& cfg = {});
        virtual ~Application();

        bool initOk() const { return m_window.nativeHandle() != nullptr && m_renderer.isValid(); }
        void run();
        void requestQuit() { m_running = false; }

        virtual void onInit() {}
        virtual void onUpdate(float dt) {}
        virtual void onRender() {}
        virtual void onShutdown() {}

        World&         world()    { return m_world; }
        AssetManager&  assets()   { return m_assets; }
        AudioSystem&   audio()    { return m_audio; }
        NetworkSystem& network()  { return m_network; }
        DebugServer&   debug()    { return m_debug; }
        Renderer&      renderer() { return m_renderer; }
        Input&         input()    { return m_input; }
        const Input&   input() const { return m_input; }
        Window&        window()   { return m_window; }

    private:
        void applyNetConfig();
        void applyDebugConfig();

        LogSession m_logSession;

    protected:
        Window        m_window;
        Input         m_input;
        World         m_world;
        AssetManager  m_assets;
        Renderer      m_renderer;
        AudioSystem   m_audio;
        NetworkSystem m_network; // after audio: sockets destroyed before HWND
        DebugServer   m_debug;   // after network: debug TCP dies before game sockets / HWND

    private:
        AppConfig m_config;
        bool      m_running = true;
    };

} // namespace Dark
