#pragma once
#include "Core/Window.h"
#include "ECS/World.h"
#include "Assets/AssetManager.h"
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
    };

    class Application
    {
    public:
        explicit Application(const AppConfig& cfg = {});
        virtual ~Application();

        void run();
        void requestQuit() { m_running = false; }

        virtual void onInit() {}
        virtual void onUpdate(float dt) {}
        virtual void onRender() {}
        virtual void onShutdown() {}

        World&        world()    { return m_world; }
        AssetManager& assets()   { return m_assets; }
        Renderer&     renderer() { return m_renderer; }
        Input&        input()    { return m_input; }
        const Input&  input() const { return m_input; }
        Window&       window()   { return m_window; }

    protected:
        Window       m_window;
        Input        m_input;
        World        m_world;
        AssetManager m_assets;
        Renderer     m_renderer;

    private:
        bool m_running = true;
    };

} // namespace Dark
