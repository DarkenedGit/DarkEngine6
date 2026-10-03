#include "Core/Paths.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace Dark
{

    std::filesystem::path executableDirectory()
    {
        // Grow until the path fits. n == buffer size means truncation, including the MAX_PATH case.
        constexpr DWORD kCap = 32768;
        std::wstring    buf(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (n == 0)
                return {};
            if (n < buf.size())
            {
                std::filesystem::path exe(buf.c_str());
                return exe.parent_path();
            }
            if (buf.size() >= kCap)
                return {};
            const size_t next = buf.size() > kCap / 2 ? static_cast<size_t>(kCap) : buf.size() * 2;
            buf.assign(next, L'\0');
        }
    }

    std::filesystem::path absolutePath(const std::filesystem::path& path)
    {
        if (path.empty())
            return {};

        std::error_code ec;
        std::filesystem::path abs = std::filesystem::absolute(path, ec);
        if (ec)
            return {};
        return abs;
}

} // namespace Dark
