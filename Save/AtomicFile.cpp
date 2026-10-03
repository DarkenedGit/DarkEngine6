#include "Save/AtomicFile.h"

#include "Core/Log.h"

#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace Dark::Save
{
    namespace
    {
        std::wstring widePath(const std::filesystem::path& path)
        {
            return path.wstring();
        }

        // CreateFileW follows symlinks and junctions; CREATE_ALWAYS would truncate the target.
        bool isReparsePoint(const std::wstring& text)
        {
            if (text.empty())
                return false;
            const DWORD attrs = GetFileAttributesW(text.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES)
                return false;
            return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        }

        SaveResult rejectReparsePaths(const std::filesystem::path& path, const std::wstring& dstText, const std::wstring& tmpText)
        {
            if (!isReparsePoint(path.parent_path().wstring()) && !isReparsePoint(dstText) && !isReparsePoint(tmpText))
                return SaveResult::Ok;
            DE_LOG_ERROR("Save: refusing reparse point");
            return SaveResult::PathInvalid;
        }
    } // namespace

    SaveResult writeAtomic(const std::filesystem::path& path, std::string_view bytes)
    {
        if (path.empty())
            return SaveResult::IoOpenFailed;

        std::error_code ec;
        const auto      parent = path.parent_path();
        if (!parent.empty())
        {
            std::filesystem::create_directories(parent, ec);
            if (ec)
            {
                DE_LOG_ERROR("Save: cannot create {}", parent.string());
                return SaveResult::IoOpenFailed;
            }
        }

        const std::wstring tmpText = widePath(path) + L".tmp";
        const std::wstring dstText = widePath(path);
        const SaveResult   reparse = rejectReparsePaths(path, dstText, tmpText);
        if (reparse != SaveResult::Ok)
            return reparse;

        HANDLE file = CreateFileW(tmpText.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            DE_LOG_ERROR("Save: CreateFile failed ({})", GetLastError());
            return SaveResult::IoOpenFailed;
        }

        size_t written = 0;
        while (written < bytes.size())
        {
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - written, 1u << 20));
            DWORD       got   = 0;
            if (!WriteFile(file, bytes.data() + written, chunk, &got, nullptr) || got != chunk)
            {
                DE_LOG_ERROR("Save: WriteFile failed ({})", GetLastError());
                CloseHandle(file);
                DeleteFileW(tmpText.c_str());
                return SaveResult::IoWriteFailed;
            }
            written += got;
        }

        if (!FlushFileBuffers(file))
        {
            DE_LOG_ERROR("Save: FlushFileBuffers failed ({})", GetLastError());
            CloseHandle(file);
            DeleteFileW(tmpText.c_str());
            return SaveResult::IoFlushFailed;
        }
        CloseHandle(file);

        if (!MoveFileExW(tmpText.c_str(), dstText.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DE_LOG_ERROR("Save: MoveFileEx failed ({})", GetLastError());
            DeleteFileW(tmpText.c_str());
            return SaveResult::IoRenameFailed;
        }
        return SaveResult::Ok;
    }

    SaveResult readCapped(const std::filesystem::path& path, std::string& out, uint64_t cap)
    {
        out.clear();
        if (path.empty())
            return SaveResult::IoReadFailed;

        const std::wstring text    = widePath(path);
        const std::wstring tmpText = text + L".tmp";
        const SaveResult   reparse = rejectReparsePaths(path, text, tmpText);
        if (reparse != SaveResult::Ok)
            return reparse;

        HANDLE file = CreateFileW(text.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            DE_LOG_ERROR("Save: open for read failed ({})", GetLastError());
            return SaveResult::IoReadFailed;
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart < 0)
        {
            CloseHandle(file);
            return SaveResult::IoReadFailed;
        }
        if (static_cast<uint64_t>(size.QuadPart) > cap)
        {
            CloseHandle(file);
            return SaveResult::TooLarge;
        }

        out.resize(static_cast<size_t>(size.QuadPart));
        size_t filled = 0;
        while (filled < out.size())
        {
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(out.size() - filled, 1u << 20));
            DWORD       got   = 0;
            if (!ReadFile(file, out.data() + filled, chunk, &got, nullptr))
            {
                CloseHandle(file);
                out.clear();
                return SaveResult::IoReadFailed;
            }
            if (got == 0)
                break;
            filled += got;
        }
        CloseHandle(file);
        if (filled != out.size())
        {
            out.clear();
            return SaveResult::IoReadFailed;
        }
        return SaveResult::Ok;
    }

} // namespace Dark::Save
