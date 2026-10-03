#include "Save/SavePaths.h"

#include "Core/Log.h"
#include "Core/Paths.h"
#include "Core/UUID.h"
#include "Save/AtomicFile.h"

#include <algorithm>
#include <chrono>
#include <format>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <ShlObj.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace Dark::Save
{
    namespace
    {
        bool probeDirectory(const std::filesystem::path& dir)
        {
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (ec)
                return false;
            const auto probe = dir / L".probe";
            if (writeAtomic(probe, "ok") != SaveResult::Ok)
                return false;
            std::filesystem::remove(probe, ec);
            return true;
        }

        std::filesystem::path localAppDataSaves()
        {
            PWSTR                   known = nullptr;
            std::filesystem::path   dir;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known)) && known)
            {
                dir = std::filesystem::path(known) / "DarkEngine6" / "Saves";
                CoTaskMemFree(known);
            }
            if (dir.empty())
            {
                wchar_t buf[MAX_PATH]{};
                const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
                if (n > 0 && n < MAX_PATH)
                    dir = std::filesystem::path(buf) / "DarkEngine6" / "Saves";
            }
            return dir;
        }

        bool digit(char c)
        {
            return c >= '0' && c <= '9';
        }

        bool hexLower(char c)
        {
            return digit(c) || (c >= 'a' && c <= 'f');
        }

        void deleteStaleTempsIn(const std::filesystem::path& dir)
        {
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec))
                return;
            const auto now = std::filesystem::file_time_type::clock::now();
            for (std::filesystem::directory_iterator it(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
            {
                std::error_code itemEc;
                if (!it->is_regular_file(itemEc))
                    continue;
                const auto name = it->path().filename().string();
                if (name.size() < 5 || name.rfind(".tmp") != name.size() - 4)
                    continue;
                const auto stamp = it->last_write_time(itemEc);
                if (itemEc)
                    continue;
                if (now - stamp < std::chrono::minutes(1))
                    continue;
                std::filesystem::remove(it->path(), itemEc);
            }
        }

        std::filesystem::path& chosenDirectory()
        {
            static std::filesystem::path dir;
            static bool                  ready = false;
            if (ready)
                return dir;

            const auto exe = executableDirectory();
            if (!exe.empty() && probeDirectory(exe / "Saves"))
            {
                dir = exe / "Saves";
            }
            else
            {
                const auto fallback = localAppDataSaves();
                if (!fallback.empty() && probeDirectory(fallback))
                {
                    DE_LOG_WARN("Save: using {}", fallback.string());
                    dir = fallback;
                }
                else
                {
                    DE_LOG_ERROR("Save: no writable Saves directory");
                    dir = exe.empty() ? std::filesystem::path{} : exe / "Saves";
                }
            }
            if (!dir.empty())
                deleteStaleTempsIn(dir);
            ready = true;
            return dir;
        }
    } // namespace

    std::filesystem::path savesDirectory()
    {
        return chosenDirectory();
    }

    bool validSaveFileName(std::string_view name)
    {
        if (name.empty() || name.size() > 80)
            return false;
        for (unsigned char c : name)
        {
            if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
                return false;
        }
        if (name == "." || name == "..")
            return false;

        std::string_view rest;
        if (name.starts_with("save_"))
            rest = name.substr(5);
        else if (name.starts_with("quick_"))
            rest = name.substr(6);
        else if (name.starts_with("auto_"))
            rest = name.substr(5);
        else
            return false;
        // YYYY-MM-DD_HH-MM-SS_hhhh.json
        if (rest.size() != 29)
            return false;
        const int digits[] = { 0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18 };
        for (int index : digits)
        {
            if (!digit(rest[static_cast<size_t>(index)]))
                return false;
        }
        if (rest[4] != '-' || rest[7] != '-' || rest[10] != '_' || rest[13] != '-' || rest[16] != '-' || rest[19] != '_')
            return false;
        for (int i = 20; i < 24; ++i)
        {
            if (!hexLower(rest[static_cast<size_t>(i)]))
                return false;
        }
        return rest.substr(24) == ".json";
    }

    SaveResult makeSaveName(SaveKind kind, std::string& outName)
    {
        const char* prefix = "save";
        if (kind == SaveKind::Quick)
            prefix = "quick";
        else if (kind == SaveKind::Auto)
            prefix = "auto";

        SYSTEMTIME st{};
        GetSystemTime(&st);
        const unsigned suffix = static_cast<unsigned>(static_cast<uint64_t>(UUID{}) & 0xFFFFu);
        outName = std::format("{}_{:04}-{:02}-{:02}_{:02}-{:02}-{:02}_{:04x}.json", prefix, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, suffix);
        if (!validSaveFileName(outName))
            return SaveResult::PathInvalid;
        return SaveResult::Ok;
    }

    SaveResult listSaveFiles(std::vector<SaveFileInfo>& out)
    {
        out.clear();
        const auto dir = savesDirectory();
        if (dir.empty())
            return SaveResult::NoSavesDir;
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec) || ec)
            return SaveResult::NoSavesDir;

        for (std::filesystem::directory_iterator it(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
        {
            std::error_code itemEc;
            if (!it->is_regular_file(itemEc))
                continue;
            const auto name = it->path().filename().string();
            if (!validSaveFileName(name))
                continue;
            const auto size = it->file_size(itemEc);
            if (itemEc || size > kMaxFileBytes)
                continue;
            SaveFileInfo info;
            info.name = name;
            info.path = it->path();
            info.size = size;
            out.push_back(std::move(info));
        }
        std::sort(out.begin(), out.end(), [](const SaveFileInfo& a, const SaveFileInfo& b) { return a.name < b.name; });
        return SaveResult::Ok;
    }

    void deleteStaleTemps()
    {
        const auto dir = savesDirectory();
        if (!dir.empty())
            deleteStaleTempsIn(dir);
    }

} // namespace Dark::Save
