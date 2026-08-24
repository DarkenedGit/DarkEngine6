#include "Core/Log.h"
#include "Core/Paths.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace Dark
{
    namespace
    {
        constexpr uint32_t kCategoryCount     = static_cast<uint32_t>(LogCategory::Count);
        constexpr uint32_t kAllCategoriesMask = (1u << kCategoryCount) - 1u;

        static_assert(kCategoryCount <= 32u, "LogCategory bitmask fits in uint32_t");

        struct LogState
        {
            std::mutex            mutex;
            FILE*                 file = nullptr;
            std::filesystem::path path;
            std::atomic<uint32_t> enabledMask{kAllCategoriesMask};
            std::atomic<int>      minLevel{static_cast<int>(LogLevel::Trace)};
            Log::CaptureFn        capture = nullptr;

            ~LogState()
            {
                if (file)
                {
                    std::fclose(file);
                    file = nullptr;
                }
            }
        };

        LogState& state()
        {
            static LogState s;
            return s;
        }

        void closeFileUnlocked(LogState& s)
        {
            if (s.file)
            {
                std::fclose(s.file);
                s.file = nullptr;
            }
        }

        uint32_t categoryBit(LogCategory category)
        {
            const auto index = static_cast<uint32_t>(category);
            if (index >= kCategoryCount)
                return 0;
            return 1u << index;
        }

        std::filesystem::path defaultLogPath()
        {
            const auto dir = executableDirectory();
            if (dir.empty())
                return std::filesystem::path("DarkEngine6.log");
            return dir / "DarkEngine6.log";
        }
    } // namespace

    void Log::init()
    {
        init(defaultLogPath());
    }

    void Log::init(const std::filesystem::path& filePath)
    {
        auto& s = state();
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            closeFileUnlocked(s);
            s.path.clear();

            if (filePath.has_parent_path())
            {
                std::error_code ec;
                std::filesystem::create_directories(filePath.parent_path(), ec);
            }

            FILE* f = nullptr;
#if defined(_MSC_VER)
            if (_wfopen_s(&f, filePath.wstring().c_str(), L"w") != 0)
                f = nullptr;
#else
            f = std::fopen(filePath.string().c_str(), "w");
#endif
            s.file = f;
            if (s.file)
                s.path = filePath;
        }

        if (state().file)
            log(LogLevel::Info, LogCategory::Core, "Log: writing to '{}'", filePath.string());
        else
            log(LogLevel::Warn, LogCategory::Core, "Log: could not open '{}' for writing", filePath.string());
    }

    void Log::shutdown()
    {
        auto& s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        closeFileUnlocked(s);
        s.path.clear();
    }

    std::filesystem::path Log::filePath()
    {
        auto& s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        return s.path;
    }

    void Log::setCategoryEnabled(LogCategory category, bool enabled)
    {
        const uint32_t bit = categoryBit(category);
        if (bit == 0)
            return;

        auto& s = state();
        if (enabled)
            s.enabledMask.fetch_or(bit, std::memory_order_relaxed);
        else
            s.enabledMask.fetch_and(~bit, std::memory_order_relaxed);
    }

    bool Log::isCategoryEnabled(LogCategory category)
    {
        const uint32_t bit = categoryBit(category);
        if (bit == 0)
            return true;
        return (state().enabledMask.load(std::memory_order_relaxed) & bit) != 0;
    }

    void Log::setMinLevel(LogLevel level)
    {
        state().minLevel.store(static_cast<int>(level), std::memory_order_relaxed);
    }

    LogLevel Log::minLevel()
    {
        return static_cast<LogLevel>(state().minLevel.load(std::memory_order_relaxed));
    }

    void Log::resetFilters()
    {
        auto& s = state();
        s.enabledMask.store(kAllCategoriesMask, std::memory_order_relaxed);
        s.minLevel.store(static_cast<int>(LogLevel::Trace), std::memory_order_relaxed);
    }

    const char* Log::levelName(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
        default:
            return "?";
        }
    }

    const char* Log::categoryName(LogCategory category)
    {
        switch (category)
        {
        case LogCategory::Core:
            return "Core";
        case LogCategory::Render:
            return "Render";
        case LogCategory::Audio:
            return "Audio";
        case LogCategory::Collision:
            return "Collision";
        case LogCategory::AI:
            return "AI";
        case LogCategory::Input:
            return "Input";
        case LogCategory::Networking:
            return "Networking";
        default:
            return "?";
        }
    }

    void Log::setCapture(CaptureFn fn)
    {
        auto& s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        s.capture = fn;
    }

    bool Log::shouldLog(LogLevel level, LogCategory category)
    {
        const int lv = static_cast<int>(level);
        if (lv < state().minLevel.load(std::memory_order_relaxed))
            return false;

        // Errors and fatals always record, even when the category is filtered.
        if (level == LogLevel::Error || level == LogLevel::Fatal)
            return true;

        return isCategoryEnabled(category);
    }

    void Log::write(LogLevel level, LogCategory category, std::string_view message)
    {
        const std::string line = std::format("[DE/{}][{}] {}\n", levelName(level), categoryName(category), message);

        CaptureFn   cap = nullptr;
        std::string captured;
        {
            auto& s = state();
            std::lock_guard<std::mutex> lock(s.mutex);
            cap = s.capture;
            if (cap)
                captured.assign(message);

            if (s.file)
            {
                std::fputs(line.c_str(), s.file);
                std::fflush(s.file);
            }
            std::fputs(line.c_str(), stdout);
            std::fflush(stdout);
        }

        OutputDebugStringA(line.c_str());

        if (cap)
            cap(level, category, captured.c_str());
    }

} // namespace Dark
