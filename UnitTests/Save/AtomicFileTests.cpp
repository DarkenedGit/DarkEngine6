#include <gtest/gtest.h>

#include "Save/AtomicFile.h"

#include <filesystem>
#include <string>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

using namespace Dark;

namespace
{
    std::filesystem::path tempFile(const char* name)
    {
        std::error_code ec;
        const auto dir = std::filesystem::temp_directory_path(ec);
        EXPECT_FALSE(ec);
        return dir / name;
    }

    void removePath(const std::filesystem::path& path)
    {
        if (path.empty())
            return;
        const std::wstring text  = path.wstring();
        const DWORD        attrs = GetFileAttributesW(text.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
            return;
        if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
                RemoveDirectoryW(text.c_str());
            else
                DeleteFileW(text.c_str());
            if (GetFileAttributesW(text.c_str()) == INVALID_FILE_ATTRIBUTES)
                return;
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    void removeReparseFixture(const std::filesystem::path& dir, const std::filesystem::path& reparse)
    {
        // Drop the reparse point before the directory it names.
        removePath(reparse);
        removePath(dir / "index.json.tmp");
        removePath(dir / "index.json");
        removePath(dir / "payload" / "sentinel.txt");
        removePath(dir / "payload");
        removePath(dir);
    }

    bool createJunction(const std::filesystem::path& link, const std::filesystem::path& target, std::error_code& ec)
    {
        ec.clear();
        wchar_t systemDir[MAX_PATH]{};
        const UINT got = GetSystemDirectoryW(systemDir, MAX_PATH);
        if (got == 0 || got >= MAX_PATH)
        {
            ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
            return false;
        }

        std::wstring command = std::wstring(systemDir) + L"\\cmd.exe /d /c mklink /J \"" + link.wstring() + L"\" \"" + target.wstring() + L"\"";

        SECURITY_ATTRIBUTES inherit{};
        inherit.nLength              = static_cast<DWORD>(sizeof(inherit));
        inherit.bInheritHandle       = TRUE;
        const HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        STARTUPINFOW startup{};
        startup.cb          = static_cast<DWORD>(sizeof(startup));
        BOOL inheritHandles = FALSE;
        if (nul != INVALID_HANDLE_VALUE)
        {
            startup.dwFlags    = STARTF_USESTDHANDLES;
            startup.hStdInput  = nul;
            startup.hStdOutput = nul;
            startup.hStdError  = nul;
            inheritHandles     = TRUE;
        }

        PROCESS_INFORMATION process{};
        const BOOL started = CreateProcessW(nullptr, &command[0], nullptr, nullptr, inheritHandles, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
        if (nul != INVALID_HANDLE_VALUE)
            CloseHandle(nul);
        if (!started)
        {
            ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
            return false;
        }

        const DWORD wait = WaitForSingleObject(process.hProcess, 15000);
        DWORD       code = 1;
        if (wait == WAIT_OBJECT_0)
        {
            if (!GetExitCodeProcess(process.hProcess, &code))
                code = 1;
        }
        else
        {
            TerminateProcess(process.hProcess, 1);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        if (wait != WAIT_OBJECT_0 || code != 0)
        {
            const int value = code != 0 ? static_cast<int>(code) : static_cast<int>(wait);
            ec              = std::error_code(value, std::system_category());
            return false;
        }
        return true;
    }

    struct ReparseCleanup
    {
        std::filesystem::path dir;
        std::filesystem::path reparse;

        ~ReparseCleanup()
        {
            removeReparseFixture(dir, reparse);
        }
    };
}

TEST(AtomicFile, RoundTrip)
{
    const auto path = tempFile("darkengine6-atomic-roundtrip.txt");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ASSERT_EQ(Save::writeAtomic(path, "progress"), Save::SaveResult::Ok);
    std::string text;
    EXPECT_EQ(Save::readCapped(path, text, 64), Save::SaveResult::Ok);
    EXPECT_EQ(text, "progress");
    std::filesystem::remove(path, ec);
}

TEST(AtomicFile, RejectsOversizeBeforeAlloc)
{
    const auto path = tempFile("darkengine6-atomic-oversize.txt");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ASSERT_EQ(Save::writeAtomic(path, "too-big"), Save::SaveResult::Ok);
    std::string text = "unchanged";
    EXPECT_EQ(Save::readCapped(path, text, 4), Save::SaveResult::TooLarge);
    EXPECT_TRUE(text.empty());
    std::filesystem::remove(path, ec);
}

TEST(AtomicFile, RejectsReparseWithoutTruncating)
{
    const auto dir      = tempFile("darkengine6-atomic-reparse");
    const auto payload  = dir / "payload";
    const auto sentinel = payload / "sentinel.txt";
    const auto dest     = dir / "index.json";
    const auto tmp      = std::filesystem::path(dest.wstring() + L".tmp");

    ReparseCleanup cleanup{dir, tmp};
    removeReparseFixture(dir, tmp);

    std::error_code ec;
    std::filesystem::create_directories(payload, ec);
    ASSERT_FALSE(ec);
    ASSERT_EQ(Save::writeAtomic(sentinel, "SENTINEL"), Save::SaveResult::Ok);

    const DWORD sentinelAttrs = GetFileAttributesW(sentinel.wstring().c_str());
    ASSERT_NE(sentinelAttrs, INVALID_FILE_ATTRIBUTES);
    ASSERT_TRUE((sentinelAttrs & FILE_ATTRIBUTE_REPARSE_POINT) == 0);

    DWORD linkFlags = 0;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
    linkFlags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif
    // Symlink creation needs admin or Developer Mode. A directory junction does not.
    if (!CreateSymbolicLinkW(tmp.wstring().c_str(), sentinel.wstring().c_str(), linkFlags))
    {
        std::error_code linkEc;
        if (!createJunction(tmp, payload, linkEc))
        {
            GTEST_SKIP() << "cannot create a symlink or directory junction: " << linkEc.message();
        }
    }

    const DWORD linkAttrs = GetFileAttributesW(tmp.wstring().c_str());
    ASSERT_NE(linkAttrs, INVALID_FILE_ATTRIBUTES);
    ASSERT_TRUE((linkAttrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0);

    EXPECT_EQ(Save::writeAtomic(dest, "truncated"), Save::SaveResult::PathInvalid);

    std::string text;
    EXPECT_EQ(Save::readCapped(sentinel, text, 64), Save::SaveResult::Ok);
    EXPECT_EQ(text, "SENTINEL");
    EXPECT_FALSE(std::filesystem::exists(dest, ec));

    removeReparseFixture(dir, tmp);
    EXPECT_EQ(GetFileAttributesW(tmp.wstring().c_str()), INVALID_FILE_ATTRIBUTES);
}
