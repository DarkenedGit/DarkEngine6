#include "Core/Paths.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace Dark
{

    std::filesystem::path executableDirectory()
    {
        wchar_t buf[MAX_PATH]{};
        const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return {};

        std::filesystem::path exe(buf);
        return exe.parent_path();
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
