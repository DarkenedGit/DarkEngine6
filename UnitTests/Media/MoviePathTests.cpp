#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Core/Log.h"
#include "Media/MoviePath.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Dark;

namespace
{

    std::vector<std::string>  g_logs;
    std::vector<LogCategory>  g_cats;

    void captureLog(LogLevel, LogCategory category, const char* message)
    {
        g_cats.push_back(category);
        g_logs.emplace_back(message ? message : "");
    }

    struct LogCapture
    {
        LogCapture()
        {
            g_logs.clear();
            g_cats.clear();
            Log::resetFilters();
            Log::setCapture(&captureLog);
        }

        ~LogCapture()
        {
            Log::setCapture(nullptr);
        }
    };

    std::string lowerAscii(std::string s)
    {
        for (char& c : s)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        return s;
    }

} // namespace

TEST(MoviePath, ExtensionAllowList)
{
    EXPECT_TRUE(movieExtensionAllowed("movies/sample.mp4"));
    EXPECT_TRUE(movieExtensionAllowed("movies/sample.MP4"));
    EXPECT_TRUE(movieExtensionAllowed("movies/a.m4v"));
    EXPECT_TRUE(movieExtensionAllowed("movies/a.mov"));
    EXPECT_TRUE(movieExtensionAllowed("movies/a.mpg"));
    EXPECT_TRUE(movieExtensionAllowed("movies/a.mpeg"));
    EXPECT_TRUE(movieExtensionAllowed("movies/a.vob"));
    EXPECT_TRUE(movieExtensionAllowed("movies/a.ts"));
    EXPECT_TRUE(movieExtensionAllowed("movies/a.m2ts"));
    EXPECT_TRUE(movieExtensionAllowed("movies/a.mts"));

    EXPECT_FALSE(movieExtensionAllowed("movies/a.mkv"));
    EXPECT_FALSE(movieExtensionAllowed("movies/a.avi"));
    EXPECT_FALSE(movieExtensionAllowed("movies/a.wmv"));
    EXPECT_FALSE(movieExtensionAllowed("movies/a.webm"));
    EXPECT_FALSE(movieExtensionAllowed("movies/a"));
    EXPECT_FALSE(movieExtensionAllowed(""));
    EXPECT_FALSE(movieExtensionAllowed(nullptr));

    EXPECT_EQ(movieVirtualPathPrecheck("movies/clip.mkv"), MovieStatus::UnsupportedContainer);
    EXPECT_EQ(movieVirtualPathPrecheck("movies/clip.avi"), MovieStatus::UnsupportedContainer);
    EXPECT_EQ(movieVirtualPathPrecheck("http://example.com/a.mp4"), MovieStatus::UnsupportedContainer);
}

TEST(MoviePath, CodedSizeAndSoftwareFallback)
{
    EXPECT_TRUE(movieCodedSizeAllowed(3840, 2160));
    EXPECT_TRUE(movieCodedSizeAllowed(1920, 1080));
    EXPECT_TRUE(movieCodedSizeAllowed(1280, 720));
    EXPECT_FALSE(movieCodedSizeAllowed(4096, 2160));
    EXPECT_FALSE(movieCodedSizeAllowed(7680, 4320));
    EXPECT_FALSE(movieCodedSizeAllowed(0, 0));

    EXPECT_TRUE(movieSoftwareFallbackAllowed(1920, 1080));
    EXPECT_TRUE(movieSoftwareFallbackAllowed(1280, 720));
    EXPECT_FALSE(movieSoftwareFallbackAllowed(3840, 2160));
    EXPECT_FALSE(movieSoftwareFallbackAllowed(1920, 1081));
    EXPECT_FALSE(movieSoftwareFallbackAllowed(1921, 1080));
    EXPECT_FALSE(movieSoftwareFallbackAllowed(0, 0));
}

TEST(MoviePath, PrecheckBeforeMountJoin)
{
    EXPECT_EQ(movieVirtualPathPrecheck(nullptr), MovieStatus::FileMissing);
    EXPECT_EQ(movieVirtualPathPrecheck(""), MovieStatus::FileMissing);
    EXPECT_EQ(movieVirtualPathPrecheck("movies/../../outside.mp4"), MovieStatus::FileMissing);
    EXPECT_EQ(movieVirtualPathPrecheck("movies/../outside.mp4"), MovieStatus::FileMissing);
    EXPECT_EQ(movieVirtualPathPrecheck("C:/outside/movie.mp4"), MovieStatus::FileMissing);
    EXPECT_EQ(movieVirtualPathPrecheck("/movies/sample.mp4"), MovieStatus::FileMissing);
    EXPECT_EQ(movieVirtualPathPrecheck("http://cdn/movie.mp4"), MovieStatus::UnsupportedContainer);
    EXPECT_EQ(movieVirtualPathPrecheck("movies/sample.mp4"), MovieStatus::Ok);
    EXPECT_EQ(movieVirtualPathPrecheck("Movies/Sample.MP4"), MovieStatus::Ok);
}

TEST(MoviePath, NormalizedPrefixMatchesVirtualPathRule)
{
    // generic separators, ASCII lower, a trailing '/', file strictly longer than the mount.
    EXPECT_TRUE(movieNormalizedFileUnderMount("c:/game/content/movies/sample.mp4", "c:/game/content"));
    EXPECT_TRUE(movieNormalizedFileUnderMount("c:/game/content/movies/sample.mp4", "c:/game/content/"));
    EXPECT_FALSE(movieNormalizedFileUnderMount("c:/game/content", "c:/game/content"));
    EXPECT_FALSE(movieNormalizedFileUnderMount("c:/game/content/", "c:/game/content"));
    EXPECT_FALSE(movieNormalizedFileUnderMount("c:/game/content2/movies/sample.mp4", "c:/game/content"));
    EXPECT_FALSE(movieNormalizedFileUnderMount("c:/game/other/movies/sample.mp4", "c:/game/content"));
    EXPECT_FALSE(movieNormalizedFileUnderMount("", "c:/game/content"));
}

TEST(MoviePath, ContainedFileUsesVirtualPathFromAbsolute)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto      stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path  root  = fs::temp_directory_path(ec) / ("dark-movie-path-" + std::to_string(stamp));
    ASSERT_FALSE(ec);
    const fs::path movies = root / "movies";
    fs::create_directories(movies, ec);
    ASSERT_FALSE(ec);

    const fs::path outsideDir = fs::temp_directory_path(ec) / ("dark-movie-outside-" + std::to_string(stamp));
    fs::create_directories(outsideDir, ec);
    ASSERT_FALSE(ec);
    const fs::path outside = outsideDir / "outside.mp4";
    {
        std::ofstream out(outside, std::ios::binary);
        ASSERT_TRUE(out.good());
        out << "outside";
    }

    struct Cleanup
    {
        fs::path a;
        fs::path b;
        ~Cleanup()
        {
            std::error_code ignore;
            fs::remove_all(a, ignore);
            fs::remove_all(b, ignore);
        }
    } cleanup{root, outsideDir};

    {
        std::ofstream out(movies / "sample.mp4", std::ios::binary);
        ASSERT_TRUE(out.good());
        out << "mp4";
    }
    {
        std::ofstream empty(movies / "empty.mp4", std::ios::binary);
        ASSERT_TRUE(empty.good());
    }
    fs::create_directories(movies / "dir.mp4", ec);
    ASSERT_FALSE(ec);

    AssetManager assets;
    ASSERT_TRUE(assets.mountDirectory(root));

    EXPECT_EQ(moviePathStatus(assets, "movies/sample.mp4"), MovieStatus::Ok);
    EXPECT_EQ(moviePathStatus(assets, "movies/empty.mp4"), MovieStatus::FileMissing);
    EXPECT_EQ(moviePathStatus(assets, "movies/dir.mp4"), MovieStatus::FileMissing);
    EXPECT_EQ(moviePathStatus(assets, "movies/missing.mp4"), MovieStatus::FileMissing);

    LogCapture capture;
    EXPECT_EQ(moviePathStatus(assets, "movies/../../outside.mp4"), MovieStatus::FileMissing);
    ASSERT_FALSE(g_logs.empty());
    EXPECT_EQ(g_cats.back(), LogCategory::Media);
    const std::string logged = lowerAscii(g_logs.back());
    EXPECT_NE(logged.find("movies/../../outside.mp4"), std::string::npos);
    const std::string outsideText = lowerAscii(outside.string());
    EXPECT_EQ(logged.find(outsideText), std::string::npos);

    std::error_code linkEc;
    fs::create_symlink(outside, movies / "link.mp4", linkEc);
    if (!linkEc)
        EXPECT_EQ(moviePathStatus(assets, "movies/link.mp4"), MovieStatus::FileMissing);
}

TEST(MoviePath, HeadersDoNotIncludePlatformApis)
{
    const std::filesystem::path media = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Media";
    std::error_code             ec;
    ASSERT_TRUE(std::filesystem::is_directory(media, ec));

    bool sawHeader = false;
    std::filesystem::directory_iterator it(media, ec);
    const std::filesystem::directory_iterator end;
    ASSERT_FALSE(ec);
    while (it != end)
    {
        if (it->path().extension() == ".h")
        {
            sawHeader = true;
            std::ifstream in(it->path());
            ASSERT_TRUE(in.good()) << it->path().string();
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const std::string lower = lowerAscii(text);
            EXPECT_EQ(lower.find("windows.h"), std::string::npos) << it->path().string();
            EXPECT_EQ(lower.find("d3d12"), std::string::npos) << it->path().string();
            EXPECT_EQ(lower.find("xaudio"), std::string::npos) << it->path().string();
        }
        it.increment(ec);
        ASSERT_FALSE(ec);
    }
    EXPECT_TRUE(sawHeader);
}
