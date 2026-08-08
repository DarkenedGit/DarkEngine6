#include "Core/Log.h"

namespace Dark
{

    void Log::init() 
    {
        // Future: hook into file sink or ImGui console
    }

    const char* Log::levelTag(LogLevel l) 
    {
        switch (l) {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Info:  return "INFO";
            case LogLevel::Warn:  return "WARN";
            case LogLevel::Error: return "ERROR";
            case LogLevel::Fatal: return "FATAL";
            default:              return "?";
        }
    }

} // namespace Dark
