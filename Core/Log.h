#pragma once
#include <string_view>
#include <format>
#include <filesystem>

namespace Dark
{

    enum class LogLevel
    {
        Trace,
        Info,
        Warn,
        Error,
        Fatal
    };

    enum class LogCategory
    {
        Core = 0,
        Render,
        Audio,
        Collision,
        AI,
        Input,
        Networking,
        Count
    };

    class Log
    {
    public:
        using CaptureFn = void (*)(LogLevel level, LogCategory category, const char* message);

        // Opens (truncates) the log file. Default path is DarkEngine6.log next to the executable.
        static void init();
        static void init(const std::filesystem::path& filePath);
        static void shutdown();

        static std::filesystem::path filePath();

        static void setCategoryEnabled(LogCategory category, bool enabled);
        static bool isCategoryEnabled(LogCategory category);
        static void setMinLevel(LogLevel level);
        static LogLevel minLevel();
        static void resetFilters();

        static const char* levelName(LogLevel level);
        static const char* categoryName(LogCategory category);

        // Optional test hook. Invoked with the formatted message (no prefix) after filters pass.
        static void setCapture(CaptureFn fn);

        static bool shouldLog(LogLevel level, LogCategory category);

        template <typename... Args>
        static void log(LogLevel level, LogCategory category, std::string_view fmt, Args&&... args)
        {
            if (!shouldLog(level, category))
                return;
            const auto msg = std::vformat(fmt, std::make_format_args(args...));
            write(level, category, msg);
        }

        template <typename... Args>
        static void log(LogLevel level, std::string_view fmt, Args&&... args)
        {
            log(level, LogCategory::Core, fmt, std::forward<Args>(args)...);
        }

    private:
        static void write(LogLevel level, LogCategory category, std::string_view message);
    };

    // Opens the log file on construction and closes it on destruction.
    struct LogSession
    {
        LogSession() { Log::init(); }
        ~LogSession() { Log::shutdown(); }
        LogSession(const LogSession&)            = delete;
        LogSession& operator=(const LogSession&) = delete;
    };

} // namespace Dark

// ── Convenience macros ────────────────────────────────────────────────────────
// Pass LogCategory as the first argument to tag a subsystem, e.g.
//   DE_LOG_INFO(LogCategory::Render, "PSO ready");
// Omit it to log under LogCategory::Core.
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
