#include "Editor/EditorFileDialog.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <commdlg.h>

namespace Dark
{
    bool pickEditorFile(void* ownerHwnd, bool save, const wchar_t* title, const wchar_t* filter, const wchar_t* defaultExt,
                        const std::filesystem::path& suggested, std::filesystem::path& out)
    {
        wchar_t file[MAX_PATH];
        file[0] = 0;
        if (!suggested.empty())
        {
            const std::wstring wide = suggested.wstring();
            if (wide.size() < MAX_PATH)
                wcsncpy_s(file, wide.c_str(), _TRUNCATE);
            else
                wcsncpy_s(file, suggested.filename().wstring().c_str(), _TRUNCATE);
        }

        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = static_cast<HWND>(ownerHwnd);
        ofn.lpstrFilter = filter;
        ofn.lpstrFile   = file;
        ofn.nMaxFile    = MAX_PATH;
        ofn.lpstrDefExt = defaultExt;
        ofn.Flags       = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_HIDEREADONLY
            | (save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
        ofn.lpstrTitle = title;

        const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
        if (!ok)
            return false;
        out = std::filesystem::path(file);
        return true;
    }
}
