#include <gtest/gtest.h>

#include "Core/ContentRoots.h"
#include "Render/EchoChirpFile.h"
#include "Render/EchoPipeline.h"

#include <fstream>
#include <sstream>

using namespace Dark;

TEST(EchoChirpFile, BadJsonLeavesSettings)
{
    EchoChirpSettings rows = builtinEchoChirpSettings();
    rows.periodSeconds     = 3.0f;
    rows.speedMetersPerSec = 12.0f;
    EXPECT_FALSE(parseEchoChirpSettings("{", rows));
    EXPECT_FALSE(parseEchoChirpSettings("{\"version\":2,\"periodSeconds\":1.25,\"speed\":50}", rows));
    EXPECT_FALSE(parseEchoChirpSettings("{\"version\":1,\"periodSeconds\":0,\"speed\":50}", rows));
    EXPECT_FALSE(parseEchoChirpSettings("{\"version\":1,\"periodSeconds\":1.25}", rows));
    EXPECT_FALSE(parseEchoChirpSettings("{\"version\":1,\"periodSeconds\":1.25,\"speed\":-4}", rows));
    EXPECT_FLOAT_EQ(rows.periodSeconds, 3.0f);
    EXPECT_FLOAT_EQ(rows.speedMetersPerSec, 12.0f);
}

TEST(EchoChirpFile, ClampsIntoTheSliderWindow)
{
    EchoChirpSettings rows = builtinEchoChirpSettings();
    ASSERT_TRUE(parseEchoChirpSettings("{\"version\":1,\"periodSeconds\":100,\"speed\":9000}", rows));
    EXPECT_FLOAT_EQ(rows.periodSeconds, EchoChirpSettings::kPeriodMax);
    EXPECT_FLOAT_EQ(rows.speedMetersPerSec, EchoChirpSettings::kSpeedMax);
    ASSERT_TRUE(parseEchoChirpSettings("{\"periodSeconds\":0.01,\"speed\":0.2}", rows));
    EXPECT_FLOAT_EQ(rows.periodSeconds, EchoChirpSettings::kPeriodMin);
    EXPECT_FLOAT_EQ(rows.speedMetersPerSec, EchoChirpSettings::kSpeedMin);
}

TEST(EchoChirpFile, ContentFileMatchesTheBuiltinChirp)
{
    std::filesystem::path path;
    for (const std::filesystem::path& root : contentRootCandidates())
    {
        const std::filesystem::path candidate = root / "render" / "echolocation.json";
        std::error_code             ec;
        if (std::filesystem::is_regular_file(candidate, ec) && !ec)
        {
            path = candidate;
            break;
        }
    }
    ASSERT_FALSE(path.empty());
    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    EchoChirpSettings parsed = builtinEchoChirpSettings();
    parsed.periodSeconds     = 4.0f;
    parsed.speedMetersPerSec = 9.0f;
    ASSERT_TRUE(parseEchoChirpSettings(ss.str(), parsed));
    EXPECT_FLOAT_EQ(parsed.periodSeconds, 1.25f);
    EXPECT_FLOAT_EQ(parsed.speedMetersPerSec, 50.0f);
    EXPECT_FLOAT_EQ(parsed.periodSeconds, builtinEchoChirpSettings().periodSeconds);
    EXPECT_FLOAT_EQ(parsed.speedMetersPerSec, builtinEchoChirpSettings().speedMetersPerSec);
    EXPECT_FLOAT_EQ(parsed.periodSeconds, EchoPipeline::kPeriodSeconds);
    EXPECT_FLOAT_EQ(parsed.speedMetersPerSec, EchoPipeline::kSpeedMetersPerSec);
    EXPECT_GT(parsed.periodSeconds, EchoPipeline::kRangeMeters / parsed.speedMetersPerSec);

    EchoChirpSettings loaded = builtinEchoChirpSettings();
    loaded.periodSeconds     = 4.0f;
    ASSERT_TRUE(loadEchoChirpSettings(loaded));
    EXPECT_FLOAT_EQ(loaded.periodSeconds, parsed.periodSeconds);
    EXPECT_FLOAT_EQ(loaded.speedMetersPerSec, parsed.speedMetersPerSec);
}
