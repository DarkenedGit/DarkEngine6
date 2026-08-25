#include <gtest/gtest.h>

#include "Core/Log.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Dark;

namespace
{

struct CapturedLog
{
    LogLevel     level    = LogLevel::Info;
    LogCategory  category = LogCategory::Core;
    std::string  message;
};

std::vector<CapturedLog> g_captured;

void captureLog(LogLevel level, LogCategory category, const char* message)
{
    g_captured.push_back({level, category, message ? message : ""});
}

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

class LogTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Log::resetFilters();
        Log::setCapture(nullptr);
        g_captured.clear();
        Log::setCapture(&captureLog);
    }

    void TearDown() override
    {
        Log::setCapture(nullptr);
        Log::resetFilters();
        Log::shutdown();
        g_captured.clear();
    }
};

TEST_F(LogTest, CategoryNames)
{
    EXPECT_STREQ(Log::categoryName(LogCategory::Core), "Core");
    EXPECT_STREQ(Log::categoryName(LogCategory::Render), "Render");
    EXPECT_STREQ(Log::categoryName(LogCategory::Audio), "Audio");
    EXPECT_STREQ(Log::categoryName(LogCategory::Collision), "Collision");
    EXPECT_STREQ(Log::categoryName(LogCategory::AI), "AI");
    EXPECT_STREQ(Log::categoryName(LogCategory::Input), "Input");
    EXPECT_STREQ(Log::categoryName(LogCategory::Networking), "Networking");
    EXPECT_STREQ(Log::categoryName(LogCategory::Debug), "Debug");
}

TEST_F(LogTest, CategoriesEnabledByDefault)
{
    EXPECT_TRUE(Log::isCategoryEnabled(LogCategory::Render));
    EXPECT_TRUE(Log::isCategoryEnabled(LogCategory::Audio));
    EXPECT_TRUE(Log::isCategoryEnabled(LogCategory::Collision));
    EXPECT_TRUE(Log::isCategoryEnabled(LogCategory::AI));
    EXPECT_TRUE(Log::isCategoryEnabled(LogCategory::Input));
    EXPECT_TRUE(Log::isCategoryEnabled(LogCategory::Networking));
    EXPECT_TRUE(Log::isCategoryEnabled(LogCategory::Debug));
    EXPECT_TRUE(Log::isCategoryEnabled(LogCategory::Core));
}

TEST_F(LogTest, UncategorizedLogsUseCore)
{
    DE_LOG_INFO("plain message");
    ASSERT_EQ(g_captured.size(), 1u);
    EXPECT_EQ(g_captured[0].category, LogCategory::Core);
    EXPECT_EQ(g_captured[0].level, LogLevel::Info);
    EXPECT_EQ(g_captured[0].message, "plain message");
}

TEST_F(LogTest, CategoryIsRecorded)
{
    DE_LOG_INFO(LogCategory::Render, "draw {}", 7);
    ASSERT_EQ(g_captured.size(), 1u);
    EXPECT_EQ(g_captured[0].category, LogCategory::Render);
    EXPECT_EQ(g_captured[0].message, "draw 7");
}

TEST_F(LogTest, DisabledCategoryDropsInfoWarnTrace)
{
    Log::setCategoryEnabled(LogCategory::Audio, false);
    EXPECT_FALSE(Log::isCategoryEnabled(LogCategory::Audio));
    EXPECT_FALSE(Log::shouldLog(LogLevel::Info, LogCategory::Audio));
    EXPECT_FALSE(Log::shouldLog(LogLevel::Warn, LogCategory::Audio));
    EXPECT_FALSE(Log::shouldLog(LogLevel::Trace, LogCategory::Audio));

    DE_LOG_TRACE(LogCategory::Audio, "trace-hidden");
    DE_LOG_INFO(LogCategory::Audio, "info-hidden");
    DE_LOG_WARN(LogCategory::Audio, "warn-hidden");
    DE_LOG_INFO(LogCategory::Render, "render-visible");

    ASSERT_EQ(g_captured.size(), 1u);
    EXPECT_EQ(g_captured[0].category, LogCategory::Render);
    EXPECT_EQ(g_captured[0].message, "render-visible");
}

TEST_F(LogTest, DisabledCategoryStillEmitsErrorAndFatal)
{
    Log::setCategoryEnabled(LogCategory::Collision, false);
    EXPECT_TRUE(Log::shouldLog(LogLevel::Error, LogCategory::Collision));
    EXPECT_TRUE(Log::shouldLog(LogLevel::Fatal, LogCategory::Collision));

    DE_LOG_ERROR(LogCategory::Collision, "error-visible");
    DE_LOG_FATAL(LogCategory::Collision, "fatal-visible");

    ASSERT_EQ(g_captured.size(), 2u);
    EXPECT_EQ(g_captured[0].level, LogLevel::Error);
    EXPECT_EQ(g_captured[0].message, "error-visible");
    EXPECT_EQ(g_captured[1].level, LogLevel::Fatal);
    EXPECT_EQ(g_captured[1].message, "fatal-visible");
}

TEST_F(LogTest, ResetFiltersReenablesCategories)
{
    Log::setCategoryEnabled(LogCategory::Networking, false);
    Log::setMinLevel(LogLevel::Error);
    Log::resetFilters();

    EXPECT_TRUE(Log::isCategoryEnabled(LogCategory::Networking));
    EXPECT_EQ(Log::minLevel(), LogLevel::Trace);

    DE_LOG_INFO(LogCategory::Networking, "net-visible");
    ASSERT_EQ(g_captured.size(), 1u);
    EXPECT_EQ(g_captured[0].message, "net-visible");
}

TEST_F(LogTest, MinLevelDropsLowerSeverities)
{
    Log::setMinLevel(LogLevel::Warn);
    DE_LOG_TRACE(LogCategory::AI, "trace-hidden");
    DE_LOG_INFO(LogCategory::AI, "info-hidden");
    DE_LOG_WARN(LogCategory::AI, "warn-visible");
    DE_LOG_ERROR(LogCategory::AI, "error-visible");

    ASSERT_EQ(g_captured.size(), 2u);
    EXPECT_EQ(g_captured[0].message, "warn-visible");
    EXPECT_EQ(g_captured[1].message, "error-visible");
}

TEST_F(LogTest, FileContainsCategoryTag)
{
    const auto path = std::filesystem::temp_directory_path() / "darkengine6_log_test.log";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    Log::init(path);
    g_captured.clear();
    DE_LOG_INFO(LogCategory::Input, "key down");
    DE_LOG_INFO(LogCategory::Audio, "clip loaded");
    Log::setCategoryEnabled(LogCategory::Audio, false);
    DE_LOG_INFO(LogCategory::Audio, "clip spam");
    Log::shutdown();

    const std::string text = readTextFile(path);
    std::filesystem::remove(path, ec);

    EXPECT_NE(text.find("[DE/INFO][Input] key down"), std::string::npos);
    EXPECT_NE(text.find("[DE/INFO][Audio] clip loaded"), std::string::npos);
    EXPECT_EQ(text.find("clip spam"), std::string::npos);
    EXPECT_NE(text.find("[DE/INFO][Core] Log: writing to"), std::string::npos);
}
