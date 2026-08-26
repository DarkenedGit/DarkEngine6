#include <gtest/gtest.h>

#include "Core/ContentRoots.h"
#include "Core/Version.h"

#include <chrono>
#include <filesystem>
#include <string>

using namespace Dark;

namespace
{

std::filesystem::path weaklyNorm(const std::filesystem::path& p)
{
    std::error_code       ec;
    std::filesystem::path n = std::filesystem::weakly_canonical(p, ec);
    return ec ? p.lexically_normal() : n;
}

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
    const auto      stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path  tmpRoot = fs::temp_directory_path(ec) / ("darkengine6_content_roots_ut_" + std::to_string(stamp));
    ASSERT_FALSE(ec);

    const fs::path exeDir = tmpRoot / "build" / "bin" / "Debug";
    const fs::path cwd    = tmpRoot / "cwd_other";

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

    EXPECT_EQ(got[2], weaklyNorm(tmpRoot / "content"));
    EXPECT_EQ(got[4], weaklyNorm(tmpRoot / "build" / "content"));

    for (const fs::path& p : got)
        EXPECT_EQ(p.filename(), "content");

    fs::remove_all(tmpRoot, ec);
}

TEST(ContentRoots, DuplicateExeAndCwdAreUnique)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const auto      stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path  tmpRoot = fs::temp_directory_path(ec) / ("darkengine6_content_roots_dup_ut_" + std::to_string(stamp));
    ASSERT_FALSE(ec);

    const fs::path exeDir = tmpRoot / "build" / "bin" / "Debug";
    fs::create_directories(exeDir, ec);
    ASSERT_FALSE(ec);

    const std::vector<fs::path> got = contentRootCandidates(exeDir, exeDir);
    ASSERT_EQ(got.size(), 3u);
    EXPECT_EQ(got[0], weaklyNorm(exeDir / "content"));
    EXPECT_EQ(got[1], weaklyNorm(exeDir / ".." / ".." / ".." / "content"));
    EXPECT_EQ(got[2], weaklyNorm(exeDir / ".." / ".." / "content"));

    fs::remove_all(tmpRoot, ec);
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
