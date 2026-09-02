#include "Core/Application.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Core/Version.h"
#include "Input/InputCodes.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <stdlib.h>

namespace Dark
{

    namespace
    {
        bool isAsciiSpace(unsigned char c)
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
        }

        bool tokenEq(const char* a, const char* b)
        {
            if (!a || !b)
                return false;
            while (*a && *b && *a == *b)
            {
                ++a;
                ++b;
            }
            return *a == 0 && *b == 0;
        }

        bool tokenStartsWith(const char* s, const char* prefix)
        {
            if (!s || !prefix)
                return false;
            while (*prefix)
            {
                if (*s != *prefix)
                    return false;
                ++s;
                ++prefix;
            }
            return true;
        }

        bool parsePortDigits(const char* s, uint16_t& out)
        {
            if (!s || *s == 0)
                return false;

            uint32_t v = 0;
            const char* p = s;
            while (*p)
            {
                const unsigned char c = static_cast<unsigned char>(*p);
                if (c < '0' || c > '9')
                    return false;
                const uint32_t d = static_cast<uint32_t>(c - '0');
                if (v > (65535u - d) / 10u)
                    return false;
                v = v * 10u + d;
                ++p;
            }
            out = static_cast<uint16_t>(v);
            return true;
        }

        bool nextToken(const char*& p, char* buf, uint32_t cap)
        {
            if (!p || !buf || cap < 2)
                return false;

            while (*p && isAsciiSpace(static_cast<unsigned char>(*p)))
                ++p;
            if (*p == 0)
                return false;

            uint32_t n = 0;
            bool overflow = false;
            while (*p && !isAsciiSpace(static_cast<unsigned char>(*p)))
            {
                if (n + 1 >= cap)
                    overflow = true;
                else
                    buf[n++] = *p;
                ++p;
            }
            if (overflow)
            {
                buf[0] = 0;
                return true;
            }
            buf[n] = 0;
            return true;
        }
    } // namespace

    bool parseNetCommandLine(const char* lpCmdLine, AppConfig& cfg)
    {
        bool     sawHost  = false;
        bool     sawJoin  = false;
        uint16_t hostPort = kNetDefaultPort;
        Address  joinAddr{};
        joinAddr.port = kNetDefaultPort;

        const char* p = lpCmdLine ? lpCmdLine : "";
        char        tok[96];

        while (nextToken(p, tok, sizeof(tok)))
        {
            if (tok[0] == 0)
                continue;

            if (tokenEq(tok, "-host"))
            {
                sawHost = true;
                const char* peek = p;
                char        next[96];
                if (nextToken(peek, next, sizeof(next)) && next[0] != 0 && parsePortDigits(next, hostPort))
                    p = peek;
                continue;
            }

            if (tokenStartsWith(tok, "-host:"))
            {
                uint16_t port = 0;
                if (!parsePortDigits(tok + 6, port))
                {
                    DE_LOG_ERROR(LogCategory::Networking, "parseNetCommandLine: invalid -host port");
                    continue;
                }
                sawHost  = true;
                hostPort = port;
                continue;
            }

            if (tokenEq(tok, "-join"))
            {
                const char* peek = p;
                char        next[96];
                if (!nextToken(peek, next, sizeof(next)) || next[0] == 0)
                {
                    DE_LOG_ERROR(LogCategory::Networking, "parseNetCommandLine: -join missing address");
                    continue;
                }
                Address addr{};
                addr.port = kNetDefaultPort;
                if (!parseIPv4(next, addr))
                {
                    DE_LOG_ERROR(LogCategory::Networking, "parseNetCommandLine: invalid -join address");
                    continue;
                }
                p        = peek;
                sawJoin  = true;
                joinAddr = addr;
                continue;
            }

            if (tokenStartsWith(tok, "-join:"))
            {
                Address addr{};
                addr.port = kNetDefaultPort;
                if (!parseIPv4(tok + 6, addr))
                {
                    DE_LOG_ERROR(LogCategory::Networking, "parseNetCommandLine: invalid -join address");
                    continue;
                }
                sawJoin  = true;
                joinAddr = addr;
                continue;
            }

            if (tokenEq(tok, "-debug"))
            {
                cfg.debugListen     = true;
                cfg.debugListenPort = kDebugDefaultPort;
                const char* peek = p;
                char        next[96];
                if (nextToken(peek, next, sizeof(next)) && next[0] != 0 && parsePortDigits(next, cfg.debugListenPort))
                    p = peek;
                continue;
            }

            if (tokenStartsWith(tok, "-debug:"))
            {
                uint16_t port = 0;
                if (!parsePortDigits(tok + 7, port))
                {
                    DE_LOG_ERROR(LogCategory::Debug, "parseNetCommandLine: invalid -debug port");
                    continue;
                }
                cfg.debugListen     = true;
                cfg.debugListenPort = port;
                continue;
            }
        }

        if (sawHost && sawJoin)
        {
            DE_LOG_ERROR(LogCategory::Networking, "parseNetCommandLine: cannot use -host and -join together");
            cfg.netHost     = false;
            cfg.netHostPort = kNetDefaultPort;
            cfg.netJoin     = Address{};
            return false;
        }

        if (sawHost)
        {
            cfg.netHost     = true;
            cfg.netHostPort = hostPort;
        }
        if (sawJoin)
            cfg.netJoin = joinAddr;
        return true;
    }

    bool parseAppCommandLine(const char* lpCmdLine, AppConfig& cfg)
    {
        const char* p = lpCmdLine ? lpCmdLine : "";
        char        tok[96];

        while (nextToken(p, tok, sizeof(tok)))
        {
            if (tok[0] == 0)
                continue;
            if (tokenEq(tok, "-no-splash"))
                cfg.cliNoSplash = true;
            else if (tokenEq(tok, "-splash"))
                cfg.cliSplash = true;
            else if (tokenEq(tok, "-forward"))
                cfg.cliForward = true;
        }
        return true;
    }

    void applyDeferredScenePath(AppConfig& cfg, ScenePath whenEnabled)
    {
        cfg.scenePath = cfg.cliForward ? ScenePath::SwapChainForward : whenEnabled;
    }

    bool shouldShowSplash(const AppConfig& cfg, bool jsonEnabled)
    {
        char*  env = nullptr;
        size_t len = 0;
        if (_dupenv_s(&env, &len, "DE_NO_SPLASH") == 0 && env && env[0] != '\0')
        {
            free(env);
            return false;
        }
        free(env);

        if (cfg.cliNoSplash)
            return false;
        if (cfg.cliSplash)
            return true;
        if (!cfg.showSplash)
            return false;
        if (!jsonEnabled)
            return false;
        return true;
    }

    Application::Application(const AppConfig& cfg)
        : m_logSession()
        , m_window(cfg.title, cfg.width, cfg.height)
        , m_input()
        , m_renderer(m_window, cfg.vsync)
        , m_config(cfg)
    {
        m_window.setInput(&m_input);
        if (!m_audio.create())
            DE_LOG_WARN(LogCategory::Audio, "Audio: disabled (no device or XAudio2 init failed)");
        if constexpr (kEngineHasGit)
            DE_LOG_INFO("DarkEngine6 {} ({}) — starting up (D3D12)", kEngineVersion, kEngineGit);
        else
            DE_LOG_INFO("DarkEngine6 {} — starting up (D3D12)", kEngineVersion);
        if (!initOk())
            DE_LOG_FATAL(LogCategory::Render, "Renderer init failed");
    }

    Application::~Application()
    {
        if (m_loading.isReady())
            m_loading.shutdown(m_renderer);
        m_debug.shutdown();
        m_network.shutdown();
        m_window.setInput(nullptr);
        DE_LOG_INFO("DarkEngine6 — shutting down");
    }

    void Application::applyNetConfig()
    {
        m_network.setSceneMode(m_config.netSceneMode);
        if (m_config.netHost)
        {
            m_network.host(m_config.netHostPort);
            return;
        }
        if (m_config.netJoin.ipv4 != 0 || m_config.netJoin.port != 0)
            m_network.join(m_config.netJoin);
    }

    void Application::applyDebugConfig()
    {
        m_debug.setAppTitle(m_config.title);
        if (m_config.debugListen)
            m_debug.listen(m_config.debugListenPort);
    }

    void Application::mountDefaultContentRoots()
    {
        namespace fs = std::filesystem;
        for (const fs::path& c : contentRootCandidates())
        {
            std::error_code ec;
            if (!c.empty() && fs::exists(c, ec) && !ec && fs::is_directory(c, ec) && !ec)
                m_assets.mountDirectory(c);
        }
    }

    bool Application::shouldShowSplash() const
    {
        return Dark::shouldShowSplash(m_config, m_loading.config().enabled);
    }

    bool Application::skipPressed() const
    {
        if (m_input.keyPressed(Key::Escape))
            return true;
        if (m_input.mousePressed(MouseButton::Left))
            return true;
        for (int i = 0; i < kMaxGamepads; ++i)
        {
            if (m_input.buttonPressed(GamepadButton::Start, i))
                return true;
        }
        return false;
    }

    LoadingDrawState Application::makeDrawState() const
    {
        LoadingDrawState state;
        state.phase         = m_loading.phase();
        state.timeSec       = m_window.getTime();
        state.fade          = m_bootFade;
        state.reducedMotion = m_loading.config().reducedMotion;
        return state;
    }

    void Application::presentClearOnly()
    {
        const float savedClear[4] = {
            m_renderer.clearColor()[0], m_renderer.clearColor()[1],
            m_renderer.clearColor()[2], m_renderer.clearColor()[3]
        };
        const float* bg = m_loading.config().background;
        m_renderer.setClearColor(bg[0], bg[1], bg[2], bg[3]);

        DE_LOG_INFO("LoadingScreen: first present (clear)");
        bool ok = false;
        if (m_renderer.beginFrame())
            ok = m_renderer.endFrame() && m_renderer.present();

        m_renderer.setClearColor(savedClear[0], savedClear[1], savedClear[2], savedClear[3]);
        if (!ok)
        {
            DE_LOG_ERROR(LogCategory::Render, "Present failed; stopping");
            m_running = false;
        }
    }

    bool Application::pumpSplashFrame()
    {
        if (m_bootPresenting)
            return m_running && !m_window.shouldClose();

        m_bootPresenting = true;

        m_input.beginFrame();
        m_window.pollEvents();
        if (m_window.takeSizeChanged())
            m_renderer.resize(m_window.width(), m_window.height());
        m_input.updateDevices();
        m_audio.tick();

        if (m_window.shouldClose())
        {
            m_running        = false;
            m_bootPresenting = false;
            return false;
        }

        if (m_window.isFocused() && skipPressed())
        {
#if defined(_DEBUG)
            m_loading.skipCurrentPhaseDwell();
#else
            if (m_loading.phase() != LoadingPhase::Engine && m_loading.config().skipOnKey)
                m_loading.skipCurrentPhaseDwell();
#endif
        }

        if (m_window.isMinimized())
        {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 16, QS_ALLINPUT);
            m_bootPresenting = false;
            return m_running;
        }

        const float savedClear[4] = {
            m_renderer.clearColor()[0], m_renderer.clearColor()[1],
            m_renderer.clearColor()[2], m_renderer.clearColor()[3]
        };
        const float* bg = m_loading.config().background;
        m_renderer.setClearColor(bg[0], bg[1], bg[2], bg[3]);

        if (!m_renderer.beginFrame())
        {
            DE_LOG_ERROR(LogCategory::Render, "beginFrame failed; stopping");
            m_running        = false;
            m_bootPresenting = false;
            m_renderer.setClearColor(savedClear[0], savedClear[1], savedClear[2], savedClear[3]);
            return false;
        }
        m_loading.draw(m_renderer, makeDrawState());
        if (!m_renderer.endFrame() || !m_renderer.present())
        {
            DE_LOG_ERROR(LogCategory::Render, "Present failed; stopping");
            m_running        = false;
            m_bootPresenting = false;
            m_renderer.setClearColor(savedClear[0], savedClear[1], savedClear[2], savedClear[3]);
            return false;
        }
        m_renderer.setClearColor(savedClear[0], savedClear[1], savedClear[2], savedClear[3]);

        m_bootPresenting = false;
        return true;
    }

    bool Application::pumpBootFrame()
    {
        if (!splashActive())
            return m_running && !m_window.shouldClose();
        return pumpSplashFrame();
    }

    void Application::runFadeLoop()
    {
        if (m_loading.config().reducedMotion)
            return;

        m_loading.setPhase(LoadingPhase::FadeOut);
        constexpr float kFadeSeconds = 0.2f;
        const float     fadeStart    = m_window.getTime();
        while (m_running && !m_window.shouldClose())
        {
            const float t = m_window.getTime() - fadeStart;
            if (t >= kFadeSeconds)
                break;
            m_bootFade = 1.0f - (t / kFadeSeconds);
            if (m_bootFade < 0.0f)
                m_bootFade = 0.0f;
            if (!pumpSplashFrame())
                break;
        }
        m_bootFade = 1.0f;
    }

    void Application::run()
    {
        if (!initOk())
            return;

        mountDefaultContentRoots();
        m_loading.tryLoadConfig(m_config);
        const bool splash  = shouldShowSplash();
        bool       onInitRan     = false;
        bool       splashCreated = false;

        if (splash)
        {
            m_splashActive = true;
            presentClearOnly();
            if (!m_running || m_window.shouldClose())
            {
                m_splashActive = false;
                return;
            }
            splashCreated = m_loading.create(m_renderer);

            DE_LOG_INFO("LoadingScreen: engine phase ({:.2f}s min)", m_loading.config().engine.minSeconds);
            m_loading.setPhase(LoadingPhase::Engine);
            while (m_running && !m_window.shouldClose() && m_loading.remainingDwell() > 0.0f)
            {
                if (!pumpSplashFrame())
                    break;
                m_loading.tryLoadAssets(m_renderer);
            }

            if (m_running && !m_window.shouldClose())
                m_loading.tryLoadAssets(m_renderer);

            if (!m_running || m_window.shouldClose())
            {
                if (splashCreated)
                    m_loading.shutdown(m_renderer);
                m_splashActive = false;
                return;
            }

            DE_LOG_INFO("LoadingScreen: host phase '{}'", m_config.hostId ? m_config.hostId : "app");
            m_loading.setPhase(LoadingPhase::Host);
            onInitRan = true;
            onInit();

            while (m_running && !m_window.shouldClose() && m_loading.remainingDwell() > 0.0f)
            {
                if (!pumpSplashFrame())
                    break;
            }

            if (m_running && !m_window.shouldClose())
                runFadeLoop();

            DE_LOG_INFO("LoadingScreen: teardown");
            if (splashCreated)
                m_loading.shutdown(m_renderer);
            m_splashActive = false;
        }
        else
        {
            onInitRan = true;
            onInit();
        }

        if (!m_running || m_window.shouldClose())
        {
            if (onInitRan)
                onShutdown();
            return;
        }

        onSplashFinished();

        applyNetConfig();
        applyDebugConfig();

        float lastTime = m_window.getTime();

        while (m_running && !m_window.shouldClose())
        {
            // Edges cleared first, then OS messages fill keyboard pressed/released.
            m_input.beginFrame();
            m_window.pollEvents();
            if (m_window.takeSizeChanged())
            {
                if (!m_renderer.resize(m_window.width(), m_window.height()))
                {
                    DE_LOG_ERROR(LogCategory::Render, "resize failed; stopping");
                    break;
                }
            }
            m_input.updateDevices();

            const float now = m_window.getTime();
            float dt = now - lastTime;
            lastTime = now;

            if (dt < 0.0f || dt > 0.25f)
                dt = 1.0f / 60.0f;

            m_debug.perf().beginFrame();

            float t0 = m_window.getTime();
            m_network.poll(m_world, dt);
            m_debug.perf().add(PerfSlot::NetPoll, m_window.getTime() - t0);

            t0 = m_window.getTime();
            m_debug.poll(dt);
            m_debug.perf().add(PerfSlot::DebugFlush, m_window.getTime() - t0);

            t0 = m_window.getTime();
            onUpdate(dt);
            m_debug.perf().add(PerfSlot::Update, m_window.getTime() - t0);

            t0 = m_window.getTime();
            m_audio.tick();
            m_debug.perf().add(PerfSlot::Audio, m_window.getTime() - t0);

            t0 = m_window.getTime();
            m_network.flush(m_world, dt);
            m_debug.perf().add(PerfSlot::NetFlush, m_window.getTime() - t0);

            t0 = m_window.getTime();
            onRender();
            m_debug.perf().add(PerfSlot::Render, m_window.getTime() - t0);

            if (!m_running)
                break;

            t0 = m_window.getTime();
            if (!m_renderer.present())
            {
                DE_LOG_ERROR(LogCategory::Render, "Present failed; stopping");
                break;
            }
            m_debug.perf().add(PerfSlot::Present, m_window.getTime() - t0);

            const DebugFrameStats fs{m_renderer.stats().drawCalls, m_renderer.stats().triangles};
            const float rtt = (m_network.role() == NetRole::Idle) ? 0.0f : m_network.rttMs(m_network.localClientId());
            m_debug.perf().endFrame(dt, fs, m_network.packetsIn(), m_network.packetsOut(), rtt);
            m_debug.flush(&m_world, fs, m_network.packetsIn(), m_network.packetsOut(), rtt, dt);
        }

        onShutdown();
    }

} // namespace Dark
