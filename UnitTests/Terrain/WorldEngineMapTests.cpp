#include <gtest/gtest.h>

#include "Core/ContentRoots.h"
#include "Math/Color.h"
#include "Terrain/WorldEngineMap.h"

#include <filesystem>

using namespace Dark;
using namespace Dark::Terrain;

TEST(WorldEngine, MissingDirectoryFails)
{
    EXPECT_TRUE(resolveWorldEngineDirectory("terrain/does-not-exist-worldengine").empty());
    WorldEngineMaps maps;
    EXPECT_FALSE(loadWorldEngineDirectory("terrain/does-not-exist-worldengine", {}, maps));
    EXPECT_FALSE(maps.height.valid());
}

TEST(WorldEngine, HurricaneRidgeHeightAndGrass)
{
    bool found = false;
    for (const std::filesystem::path& root : contentRootCandidates())
    {
        std::error_code ec;
        if (std::filesystem::is_directory(root / "terrain" / "HurricaneRidge", ec) && !ec)
            found = true;
    }
    ASSERT_TRUE(found);

    const std::filesystem::path dir = resolveWorldEngineDirectory("terrain/HurricaneRidge");
    ASSERT_FALSE(dir.empty());

    WorldEngineMaps maps;
    ASSERT_TRUE(loadWorldEngineDirectory(dir, {}, maps));
    ASSERT_TRUE(maps.height.valid());
    EXPECT_EQ(maps.height.width(), 1025u);
    EXPECT_EQ(maps.height.height(), 1025u);
    EXPECT_FLOAT_EQ(maps.height.cellSize(), 1.0f);
    EXPECT_FLOAT_EQ(maps.height.heightScale(), 480.0f);
    EXPECT_FLOAT_EQ(maps.height.origin().x, -512.0f);
    EXPECT_FLOAT_EQ(maps.height.origin().y, 0.0f);
    EXPECT_FLOAT_EQ(maps.height.origin().z, -512.0f);

    const float* samples = maps.height.samples();
    const size_t count   = static_cast<size_t>(maps.height.width()) * maps.height.height();
    float        lo      = samples[0];
    float        hi      = samples[0];
    for (size_t i = 0; i < count; ++i)
    {
        if (samples[i] < lo)
            lo = samples[i];
        if (samples[i] > hi)
            hi = samples[i];
    }
    EXPECT_FLOAT_EQ(lo, 0.0f);
    EXPECT_FLOAT_EQ(hi, 1.0f);

    ASSERT_TRUE(maps.splat.valid());
    EXPECT_EQ(maps.splat.width(), maps.height.width());
    EXPECT_EQ(maps.splat.height(), maps.height.height());
    const uint8_t* rgba  = maps.splat.rgba();
    size_t         grass = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const uint8_t r = rgba[i * 4u + 0u];
        const uint8_t g = rgba[i * 4u + 1u];
        const uint8_t b = rgba[i * 4u + 2u];
        const uint8_t a = rgba[i * 4u + 3u];
        if (g >= r && g >= b && g >= a && g > 0)
            ++grass;
    }
    EXPECT_GT(static_cast<double>(grass) / static_cast<double>(count), 0.6);

    EXPECT_NE(maps.sourceKey.find("HurricaneRidge"), std::string::npos);
    EXPECT_FALSE(maps.diffuse.empty());

    Image orm;
    ASSERT_TRUE(buildWorldEngineOrmImage(maps.roughness, maps.mask, orm));
    EXPECT_EQ(orm.colorSpace(), Color::ColorSpace::Linear);
    ASSERT_NE(orm.pixels(), nullptr);
    EXPECT_EQ(orm.width(), 1025u);
    EXPECT_EQ(orm.pixels()[0], 255); // AO
    EXPECT_GT(orm.pixels()[1], 0);   // roughness
    EXPECT_EQ(orm.pixels()[2], 0);   // metal stays 0 on a flat mask
}
