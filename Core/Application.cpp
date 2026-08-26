#include "Core/Application.h"
#include "Core/Log.h"

#include <cstdint>

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
        DE_LOG_INFO("DarkEngine6 v0.1 — starting up (D3D12)");
        if (!initOk())
            DE_LOG_FATAL(LogCategory::Render, "Renderer init failed");
    }

    Application::~Application()
    {
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

    void Application::run()
    {
        if (!initOk())
            return;

        onInit();
        applyNetConfig();
        applyDebugConfig();

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
