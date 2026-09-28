#pragma once

#include <filesystem>

namespace Dark
{
    // Win32 open/save dialog. `filter` is a double-null terminated wide string.
    // Cancel returns false and leaves `out` unchanged.
    bool pickEditorFile(void* ownerHwnd, bool save, const wchar_t* title, const wchar_t* filter, const wchar_t* defaultExt,
                        const std::filesystem::path& suggested, std::filesystem::path& out);
}
