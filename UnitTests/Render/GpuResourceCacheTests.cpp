#include <gtest/gtest.h>

#include "Math/Color.h"
#include "Render/GpuResourceCache.h"

using Dark::CachedTextureReuse;
using Dark::classifyCachedTextureReuse;
using Dark::Color::TextureUsage;

TEST(GpuResourceCache, ClassifyReuseSameUsage)
{
    EXPECT_EQ(classifyCachedTextureReuse(true, TextureUsage::Albedo, TextureUsage::Albedo), CachedTextureReuse::Reuse);
    EXPECT_EQ(classifyCachedTextureReuse(true, TextureUsage::Hud, TextureUsage::Hud), CachedTextureReuse::Reuse);
}

TEST(GpuResourceCache, ClassifyReuseConflictKeepsFirst)
{
    EXPECT_EQ(classifyCachedTextureReuse(true, TextureUsage::Albedo, TextureUsage::Hud), CachedTextureReuse::Conflict);
    EXPECT_EQ(classifyCachedTextureReuse(true, TextureUsage::Hud, TextureUsage::Albedo), CachedTextureReuse::Conflict);
    EXPECT_EQ(classifyCachedTextureReuse(true, TextureUsage::Data, TextureUsage::Albedo), CachedTextureReuse::Conflict);
}

TEST(GpuResourceCache, ClassifyMissingOrInvalidGpuCreates)
{
    EXPECT_EQ(classifyCachedTextureReuse(false, TextureUsage::Albedo, TextureUsage::Albedo), CachedTextureReuse::Create);
    EXPECT_EQ(classifyCachedTextureReuse(false, TextureUsage::Albedo, TextureUsage::Hud), CachedTextureReuse::Create);
    EXPECT_EQ(classifyCachedTextureReuse(false, TextureUsage::Hud, TextureUsage::Hud), CachedTextureReuse::Create);
}
