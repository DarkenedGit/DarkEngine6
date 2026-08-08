#pragma once
#include <string_view>
#include <format>
#include <cstdio>

namespace Dark
{

    enum class LogLevel { Trace, Info, Warn, Error, Fatal };

    class Log 
    {
    public:
        static void init();

        template<typename... Args>
        static void log(LogLevel level, std::string_view fmt, Args&&... args) 
        {
            const char* tag = levelTag(level);
            const auto  msg = std::vformat(fmt, std::make_format_args(args...));
            std::fprintf(stdout, "[DE/%s] %s\n", tag, msg.c_str());
        }

    private:
        static const char* levelTag(LogLevel l);
    };

} // namespace Dark

// ── Convenience macros ────────────────────────────────────────────────────────
#define DE_LOG_TRACE(...) ::Dark::Log::log(::Dark::LogLevel::Trace, __VA_ARGS__)
#define DE_LOG_INFO(...) ::Dark::Log::log(::Dark::LogLevel::Info, __VA_ARGS__)
#define DE_LOG_WARN(...) ::Dark::Log::log(::Dark::LogLevel::Warn, __VA_ARGS__)
#define DE_LOG_ERROR(...) ::Dark::Log::log(::Dark::LogLevel::Error, __VA_ARGS__)
#define DE_LOG_FATAL(...) ::Dark::Log::log(::Dark::LogLevel::Fatal, __VA_ARGS__)

#if DE_ENABLE_ASSERTS
    #define DE_ASSERT(cond, ...) \
        do { if(!(cond)) { DE_LOG_FATAL("Assertion failed: " #cond); __debugbreak(); } } while(0)
#else
    #define DE_ASSERT(cond, ...) ((void)(cond))
#endif
