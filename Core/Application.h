#pragma once
#include "Core/Log.h"
#include "Core/Window.h"
#include "ECS/World.h"
#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
#include "Network/NetworkSystem.h"
#include "Debug/DebugServer.h"
#include "Render/Renderer.h"
#include "Render/LoadingScreen.h"
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
        bool        cliNoSplash   = false; // -no-splash
        bool        cliSplash     = false; // -splash
        bool        cliForward    = false; // -forward (keep swap-chain forward 3D)
        ScenePath   scenePath     = ScenePath::SwapChainForward; // request; live path is Renderer::scenePath()
    };

    bool parseNetCommandLine(const char* lpCmdLine, AppConfig& cfg);
    bool parseAppCommandLine(const char* lpCmdLine, AppConfig& cfg);
    bool shouldShowSplash(const AppConfig& cfg, bool jsonEnabled);
    void applyDeferredScenePath(AppConfig& cfg, ScenePath whenEnabled);

    class Application
    {
    public:
        explicit Application(const AppConfig& cfg = {});
        virtual ~Application();

        bool initOk() const { return m_window.nativeHandle() != nullptr && m_renderer.isValid(); }
        void run();
        void requestQuit() { m_running = false; }

        bool pumpBootFrame();
        bool splashActive() const { return m_splashActive; }
        const AppConfig& config() const { return m_config; }

        virtual void onInit() {}
        virtual void onUpdate(float dt) {}
        virtual void onRender() {}
        virtual void onShutdown() {}
        virtual void onSplashFinished() {}

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
        void mountDefaultContentRoots();
        bool shouldShowSplash() const;
        bool pumpSplashFrame();
        void presentClearOnly();
        void runFadeLoop();
        bool skipPressed() const;
        LoadingDrawState makeDrawState() const;

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
        LoadingScreen m_loading;
        AppConfig     m_config;
        bool          m_running        = true;
        bool          m_bootPresenting = false;
        bool          m_splashActive   = false;
        float         m_bootFade       = 1.0f;
    };

} // namespace Dark
