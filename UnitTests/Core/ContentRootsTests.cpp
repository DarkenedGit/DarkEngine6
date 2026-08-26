#include <gtest/gtest.h>

#include "Core/ContentRoots.h"
#include "Core/Version.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace Dark;

namespace
{

std::filesystem::path weaklyNorm(const std::filesystem::path& p)
{
    std::error_code       ec;
    std::filesystem::path n = std::filesystem::weakly_canonical(p, ec);
    return ec ? p.lexically_normal() : n;
}

struct ScopedTempDir
{
    std::filesystem::path path;

    explicit ScopedTempDir(std::filesystem::path p)
        : path(std::move(p))
    {
    }

    ~ScopedTempDir()
    {
        std::error_code ec;
        if (!path.empty())
            std::filesystem::remove_all(path, ec);
    }

    ScopedTempDir(const ScopedTempDir&)            = delete;
    ScopedTempDir& operator=(const ScopedTempDir&) = delete;
};

} // namespace

TEST(Version, EngineMacrosMatchCMakeProject)
{
    EXPECT_EQ(kEngineVersionMajor, 0);
    EXPECT_EQ(kEngineVersionMinor, 1);
    EXPECT_EQ(kEngineVersionPatch, 0);
    EXPECT_STREQ(kEngineVersion, "0.1.0");
    EXPECT_STREQ(DE_ENGINE_VERSION_STRING, "0.1.0");
    EXPECT_EQ(kEngineHasGit, kEngineGit[0] != '\0');
}

TEST(ContentRoots, EmptyBasesYieldEmptyList)
{
    EXPECT_TRUE(contentRootCandidates({}, {}).empty());
}

TEST(ContentRoots, CandidateJoinOrderVsTempDir)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const auto      stamp   = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path  tmpBase = fs::temp_directory_path(ec);
    ASSERT_FALSE(ec);
    ScopedTempDir tmp(tmpBase / ("darkengine6_content_roots_ut_" + std::to_string(stamp)));

    const fs::path exeDir = tmp.path / "build" / "bin" / "Debug";
    const fs::path cwd    = tmp.path / "cwd_other";

    fs::create_directories(exeDir, ec);
    ASSERT_FALSE(ec);
    fs::create_directories(cwd, ec);
    ASSERT_FALSE(ec);

    const std::vector<fs::path> got = contentRootCandidates(exeDir, cwd);
    ASSERT_EQ(got.size(), 6u);
    EXPECT_EQ(got[0], weaklyNorm(exeDir / "content"));
    EXPECT_EQ(got[1], weaklyNorm(cwd / "content"));
    EXPECT_EQ(got[2], weaklyNorm(exeDir / ".." / ".." / ".." / "content"));
    EXPECT_EQ(got[3], weaklyNorm(cwd / ".." / ".." / ".." / "content"));
    EXPECT_EQ(got[4], weaklyNorm(exeDir / ".." / ".." / "content"));
    EXPECT_EQ(got[5], weaklyNorm(cwd / ".." / ".." / "content"));

    EXPECT_EQ(got[2], weaklyNorm(tmp.path / "content"));
    EXPECT_EQ(got[4], weaklyNorm(tmp.path / "build" / "content"));

    for (const fs::path& p : got)
        EXPECT_EQ(p.filename(), "content");
}

TEST(ContentRoots, DuplicateExeAndCwdAreUnique)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const auto      stamp   = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path  tmpBase = fs::temp_directory_path(ec);
    ASSERT_FALSE(ec);
    ScopedTempDir tmp(tmpBase / ("darkengine6_content_roots_dup_ut_" + std::to_string(stamp)));

    const fs::path exeDir = tmp.path / "build" / "bin" / "Debug";
    fs::create_directories(exeDir, ec);
    ASSERT_FALSE(ec);

    const std::vector<fs::path> got = contentRootCandidates(exeDir, exeDir);
    ASSERT_EQ(got.size(), 3u);
    EXPECT_EQ(got[0], weaklyNorm(exeDir / "content"));
    EXPECT_EQ(got[1], weaklyNorm(exeDir / ".." / ".." / ".." / "content"));
    EXPECT_EQ(got[2], weaklyNorm(exeDir / ".." / ".." / "content"));
}

TEST(ContentRoots, DefaultCandidatesUseContentSuffix)
{
    const std::vector<std::filesystem::path> got = contentRootCandidates();
    EXPECT_FALSE(got.empty());
    for (const auto& p : got)
    {
        EXPECT_FALSE(p.empty());
        EXPECT_EQ(p.filename(), "content");
    }
}
