#include "Core/Application.h"
#include "Core/Log.h"

namespace Dark
{

Application::Application(const AppConfig& cfg)
    : m_window(cfg.title, cfg.width, cfg.height)
    , m_input()
    , m_renderer(m_window)
{
    Log::init();
    m_window.setInput(&m_input);
    if (!m_audio.create())
        DE_LOG_WARN("Audio: disabled (no device or XAudio2 init failed)");
    DE_LOG_INFO("DarkEngine6 v0.1 — starting up (D3D12)");
}

Application::~Application()
{
    m_window.setInput(nullptr);
    DE_LOG_INFO("DarkEngine6 — shutting down");
}

void Application::run()
{
    onInit();

    float lastTime = 0.0f;

    while (m_running && !m_window.shouldClose())
    {
        // Edges cleared first, then OS messages fill keyboard pressed/released.
        m_input.beginFrame();
        m_window.pollEvents();
        if (m_window.takeSizeChanged())
            m_renderer.resize(m_window.width(), m_window.height());
        m_input.updateDevices();

        const float now = m_window.getTime();
        float dt = now - lastTime;
        lastTime = now;

        if (dt < 0.0f || dt > 0.25f)
            dt = 1.0f / 60.0f;

        onUpdate(dt);
        m_audio.tick();
        onRender();
        m_renderer.present();
    }

    onShutdown();
}

} // namespace Dark
