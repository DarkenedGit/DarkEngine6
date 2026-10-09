#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Math/Vector3f.h"
#include "Terrain/GrassField.h"
#include "Terrain/GrassWind.h"
#include "Terrain/TerrainGrid.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace Dark::Terrain;
using Dark::Math::Vector3f;

namespace
{
    uint32_t bladeTotal(const GrassField& field)
    {
        uint32_t n = 0;
        for (int lod = 0; lod < kGrassLodCount; ++lod)
            n += field.blades(lod).count;
        return n;
    }

    void enable(GrassField& field, uint32_t seed)
    {
        GrassParams params;
        params.enabled = true;
        params.seed    = seed;
        field.setParams(params);
    }

    bool makeGrid(TerrainGrid& grid, uint32_t samples, Vector3f origin, float raw, uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool withSplat)
    {
        HeightMap height;
        if (!height.create(samples, samples, 1.0f, 1.0f))
            return false;
        height.setOrigin(origin);
        float* samplesOut = height.mutableSamples();
        const size_t count = static_cast<size_t>(samples) * samples;
        for (size_t i = 0; i < count; ++i)
            samplesOut[i] = raw;
        if (!grid.createFromHeightMap(std::move(height), 16))
            return false;
        if (!withSplat)
            return true;
        SplatMap splat;
        if (!splat.create(samples, samples))
            return false;
        for (uint32_t z = 0; z < samples; ++z)
        {
            for (uint32_t x = 0; x < samples; ++x)
                splat.setTexel(static_cast<int>(x), static_cast<int>(z), r, g, b, a);
        }
        return grid.setWorkingSplat(std::move(splat));
    }

    bool makeRamp(TerrainGrid& grid, float slopePerSample)
    {
        constexpr uint32_t kSamples = 9;
        HeightMap          height;
        if (!height.create(kSamples, kSamples, 1.0f, 1.0f))
            return false;
        height.setOrigin(Vector3f(0.0f, 0.0f, 0.0f));
        for (uint32_t z = 0; z < kSamples; ++z)
        {
            for (uint32_t x = 0; x < kSamples; ++x)
                height.setHeight(static_cast<int>(x), static_cast<int>(z), slopePerSample * static_cast<float>(x));
        }
        if (!grid.createFromHeightMap(std::move(height), 16))
            return false;
        SplatMap splat;
        if (!splat.create(kSamples, kSamples))
            return false;
        for (uint32_t z = 0; z < kSamples; ++z)
        {
            for (uint32_t x = 0; x < kSamples; ++x)
                splat.setTexel(static_cast<int>(x), static_cast<int>(z), 0, 255, 0, 0);
        }
        return grid.setWorkingSplat(std::move(splat));
    }

    std::vector<GrassBlade> tileCopy(const GrassField& field, int tileX, int tileZ)
    {
        const GrassField::BladeSpan span = field.tileBlades(tileX, tileZ);
        if (!span.data || span.count == 0)
            return {};
        return std::vector<GrassBlade>(span.data, span.data + span.count);
    }

    bool hasXz(const std::vector<GrassBlade>& blades, float x, float z)
    {
        for (const GrassBlade& blade : blades)
        {
            if (blade.x == x && blade.z == z)
                return true;
        }
        return false;
    }
} // namespace

TEST(GrassField, RegionSlotsSumToBladeCap)
{
    const uint32_t sum = kGrassLod0Tiles * kGrassLod0PerTile + kGrassLod1Tiles * kGrassLod1PerTile + kGrassLod2Tiles * kGrassLod2PerTile + kGrassLod3Tiles * kGrassLod3PerTile;
    EXPECT_EQ(sum, kGrassBladeCap);
    EXPECT_EQ(sum, 1048576u);
    EXPECT_EQ(kGrassLod0Slots, kGrassLod0Tiles * kGrassLod0PerTile);
    EXPECT_EQ(kGrassLod1Slots, kGrassLod1Tiles * kGrassLod1PerTile);
    EXPECT_EQ(kGrassLod2Slots, kGrassLod2Tiles * kGrassLod2PerTile);
    EXPECT_EQ(kGrassLod3Slots, kGrassLod3Tiles * kGrassLod3PerTile);
}

TEST(GrassField, TileIndexUsesFloor)
{
    int x = 0;
    int z = 0;
    GrassField::tileIndex(-0.1f, -0.1f, x, z);
    EXPECT_EQ(x, -1);
    EXPECT_EQ(z, -1);
    GrassField::tileIndex(-8.0f, 0.0f, x, z);
    EXPECT_EQ(x, -1);
    EXPECT_EQ(z, 0);
    GrassField::tileIndex(-8.01f, 8.0f, x, z);
    EXPECT_EQ(x, -2);
    EXPECT_EQ(z, 1);
    GrassField::tileIndex(8.0f, 8.0f, x, z);
    EXPECT_EQ(x, 1);
    EXPECT_EQ(z, 1);
}

TEST(GrassField, SeedStableAndDistinct)
{
    TerrainGrid grid;
    ASSERT_TRUE(makeGrid(grid, 9, Vector3f(0.0f, 0.0f, 0.0f), 0.0f, 0, 255, 0, 0, true));

    GrassField a;
    GrassField b;
    GrassField c;
    enable(a, 1337u);
    enable(b, 1337u);
    enable(c, 1338u);
    a.update(grid, 4.0f, 4.0f, 0.0, -1000.0f);
    b.update(grid, 4.0f, 4.0f, 0.0, -1000.0f);
    c.update(grid, 4.0f, 4.0f, 0.0, -1000.0f);

    const std::vector<GrassBlade> left  = tileCopy(a, 0, 0);
    const std::vector<GrassBlade> right = tileCopy(b, 0, 0);
    const std::vector<GrassBlade> other = tileCopy(c, 0, 0);
    ASSERT_FALSE(left.empty());
    ASSERT_EQ(left.size(), right.size());
    for (size_t i = 0; i < left.size(); ++i)
    {
        EXPECT_FLOAT_EQ(left[i].x, right[i].x);
        EXPECT_FLOAT_EQ(left[i].z, right[i].z);
    }

    bool differ = left.size() != other.size();
    if (!differ)
    {
        for (size_t i = 0; i < left.size(); ++i)
        {
            if (left[i].x != other[i].x || left[i].z != other[i].z)
            {
                differ = true;
                break;
            }
        }
    }
    EXPECT_TRUE(differ);

    GrassParams changed = a.params();
    changed.seed        = 99u;
    a.setParams(changed);
    a.update(grid, 4.0f, 4.0f, 0.0, -1000.0f);
    const std::vector<GrassBlade> rebuilt = tileCopy(a, 0, 0);
    differ                                = rebuilt.size() != left.size();
    if (!differ)
    {
        for (size_t i = 0; i < left.size(); ++i)
        {
            if (left[i].x != rebuilt[i].x || left[i].z != rebuilt[i].z)
            {
                differ = true;
                break;
            }
        }
    }
    EXPECT_TRUE(differ);
}

TEST(GrassField, NominalLodAndHysteresis)
{
    EXPECT_FLOAT_EQ(kGrassPromoteTo0Metres, 13.6f);
    EXPECT_FLOAT_EQ(kGrassPromoteTo1Metres, 34.0f);
    EXPECT_FLOAT_EQ(kGrassPromoteTo2Metres, 68.0f);
    EXPECT_FLOAT_EQ(kGrassDemoteFrom0Metres, 18.4f);
    EXPECT_FLOAT_EQ(kGrassDemoteFrom1Metres, 46.0f);
    EXPECT_FLOAT_EQ(kGrassDemoteFrom2Metres, 92.0f);

    TerrainGrid grid;
    ASSERT_TRUE(makeGrid(grid, 9, Vector3f(0.0f, 0.0f, 0.0f), 0.0f, 0, 255, 0, 0, true));
    const float center = 4.0f;

    struct Band
    {
        float    distance;
        int      lod;
        uint32_t count;
    };
    const Band bands[] = {
        { 0.0f, 0, kGrassLod0PerTile },
        { 20.0f, 1, kGrassLod1PerTile },
        { 50.0f, 2, kGrassLod2PerTile },
        { 100.0f, 3, kGrassLod3PerTile },
    };
    for (const Band& band : bands)
    {
        GrassField field;
        enable(field, 1337u);
        field.update(grid, center + band.distance, center, 0.0, -1000.0f);
        EXPECT_EQ(field.residentLod(0, 0), band.lod) << band.distance;
        EXPECT_EQ(field.blades(band.lod).count, band.count) << band.distance;
        EXPECT_EQ(bladeTotal(field), band.count) << band.distance;
    }

    GrassField farField;
    enable(farField, 1337u);
    farField.update(grid, center + 160.0f, center, 0.0, -1000.0f);
    EXPECT_EQ(farField.residentLod(0, 0), -1);
    EXPECT_EQ(bladeTotal(farField), 0u);
    farField.update(grid, center + 140.0f, center, 0.0, -1000.0f);
    EXPECT_EQ(farField.residentLod(0, 0), -1);

    GrassField edge;
    enable(edge, 1337u);
    edge.update(grid, center + 16.1f, center, 0.0, -1000.0f);
    EXPECT_EQ(edge.residentLod(0, 0), 1);
    edge.update(grid, center + 16.0f, center, 0.0, -1000.0f);
    EXPECT_EQ(edge.residentLod(0, 0), 1);

    GrassField other;
    enable(other, 1337u);
    other.update(grid, center + 16.0f, center, 0.0, -1000.0f);
    EXPECT_EQ(other.residentLod(0, 0), 0);
    other.update(grid, center + 16.1f, center, 0.0, -1000.0f);
    EXPECT_EQ(other.residentLod(0, 0), 0);

    GrassField promote;
    enable(promote, 1337u);
    promote.update(grid, center + 20.0f, center, 0.0, -1000.0f);
    promote.update(grid, center + kGrassPromoteTo0Metres, center, 0.0, -1000.0f);
    EXPECT_EQ(promote.residentLod(0, 0), 1);
    promote.update(grid, center + (kGrassPromoteTo0Metres - 0.1f), center, 0.0, -1000.0f);
    EXPECT_EQ(promote.residentLod(0, 0), 0);

    GrassField demote;
    enable(demote, 1337u);
    demote.update(grid, center, center, 0.0, -1000.0f);
    demote.update(grid, center + kGrassDemoteFrom0Metres, center, 0.0, -1000.0f);
    EXPECT_EQ(demote.residentLod(0, 0), 0);
    demote.update(grid, center + (kGrassDemoteFrom0Metres + 0.1f), center, 0.0, -1000.0f);
    EXPECT_EQ(demote.residentLod(0, 0), 1);

    GrassField mid;
    enable(mid, 1337u);
    mid.update(grid, center + 50.0f, center, 0.0, -1000.0f);
    mid.update(grid, center + kGrassPromoteTo1Metres, center, 0.0, -1000.0f);
    EXPECT_EQ(mid.residentLod(0, 0), 2);
    mid.update(grid, center + (kGrassPromoteTo1Metres - 0.1f), center, 0.0, -1000.0f);
    EXPECT_EQ(mid.residentLod(0, 0), 1);

    GrassField midDemote;
    enable(midDemote, 1337u);
    midDemote.update(grid, center + 20.0f, center, 0.0, -1000.0f);
    midDemote.update(grid, center + kGrassDemoteFrom1Metres, center, 0.0, -1000.0f);
    EXPECT_EQ(midDemote.residentLod(0, 0), 1);
    midDemote.update(grid, center + (kGrassDemoteFrom1Metres + 0.1f), center, 0.0, -1000.0f);
    EXPECT_EQ(midDemote.residentLod(0, 0), 2);

    GrassField outer;
    enable(outer, 1337u);
    outer.update(grid, center + 100.0f, center, 0.0, -1000.0f);
    outer.update(grid, center + kGrassPromoteTo2Metres, center, 0.0, -1000.0f);
    EXPECT_EQ(outer.residentLod(0, 0), 3);
    outer.update(grid, center + (kGrassPromoteTo2Metres - 0.1f), center, 0.0, -1000.0f);
    EXPECT_EQ(outer.residentLod(0, 0), 2);

    GrassField outerDemote;
    enable(outerDemote, 1337u);
    outerDemote.update(grid, center + 50.0f, center, 0.0, -1000.0f);
    outerDemote.update(grid, center + kGrassDemoteFrom2Metres, center, 0.0, -1000.0f);
    EXPECT_EQ(outerDemote.residentLod(0, 0), 2);
    outerDemote.update(grid, center + (kGrassDemoteFrom2Metres + 0.1f), center, 0.0, -1000.0f);
    EXPECT_EQ(outerDemote.residentLod(0, 0), 3);

    GrassField jump;
    enable(jump, 1337u);
    jump.update(grid, center + 50.0f, center, 0.0, -1000.0f);
    EXPECT_EQ(jump.residentLod(0, 0), 2);
    jump.update(grid, center + 10.0f, center, 0.0, -1000.0f);
    EXPECT_EQ(jump.residentLod(0, 0), 1);
    jump.update(grid, center + 10.0f, center, 0.0, -1000.0f);
    EXPECT_EQ(jump.residentLod(0, 0), 0);

    GrassField linger;
    enable(linger, 1337u);
    linger.update(grid, center + 100.0f, center, 0.0, -1000.0f);
    ASSERT_EQ(linger.residentLod(0, 0), 3);
    linger.update(grid, center + kGrassEvictRadiusMetres, center, 0.0, -1000.0f);
    EXPECT_EQ(linger.residentLod(0, 0), 3);
    linger.update(grid, center + (kGrassEvictRadiusMetres + 0.05f), center, 0.0, -1000.0f);
    EXPECT_EQ(linger.residentLod(0, 0), -1);
    EXPECT_EQ(bladeTotal(linger), 0u);
}

TEST(GrassField, Lod3IsSubsetAndDemoteKeepsRoots)
{
    TerrainGrid grid;
    ASSERT_TRUE(makeGrid(grid, 9, Vector3f(0.0f, 0.0f, 0.0f), 0.0f, 0, 255, 0, 0, true));

    GrassField field;
    enable(field, 1337u);
    field.update(grid, 104.0f, 4.0f, 0.0, -1000.0f);
    ASSERT_EQ(field.residentLod(0, 0), 3);
    const std::vector<GrassBlade> coarse = tileCopy(field, 0, 0);
    ASSERT_EQ(coarse.size(), static_cast<size_t>(kGrassLod3PerTile));

    HeightMap* height = grid.editableWorking();
    ASSERT_NE(height, nullptr);
    for (uint32_t z = 0; z < height->height(); ++z)
    {
        for (uint32_t x = 0; x < height->width(); ++x)
            height->setHeight(static_cast<int>(x), static_cast<int>(z), 50.0f);
    }

    field.update(grid, 4.0f, 4.0f, 0.0, -1000.0f);
    field.update(grid, 4.0f, 4.0f, 0.0, -1000.0f);
    field.update(grid, 4.0f, 4.0f, 0.0, -1000.0f);
    ASSERT_EQ(field.residentLod(0, 0), 0);
    const std::vector<GrassBlade> fine = tileCopy(field, 0, 0);
    EXPECT_EQ(fine.size(), static_cast<size_t>(kGrassLod0PerTile));
    for (const GrassBlade& blade : coarse)
    {
        bool found = false;
        for (const GrassBlade& kept : fine)
        {
            if (kept.x == blade.x && kept.y == blade.y && kept.z == blade.z)
            {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);
    }
    int resampled = 0;
    for (const GrassBlade& blade : fine)
    {
        if (!hasXz(coarse, blade.x, blade.z))
        {
            EXPECT_FLOAT_EQ(blade.y, 50.0f + 0.015f);
            ++resampled;
        }
    }
    EXPECT_GT(resampled, 0);

    TerrainGrid flat;
    ASSERT_TRUE(makeGrid(flat, 9, Vector3f(0.0f, 0.0f, 0.0f), 0.0f, 0, 255, 0, 0, true));
    GrassField nearField;
    enable(nearField, 1337u);
    nearField.update(flat, 4.0f, 4.0f, 0.0, -1000.0f);
    const std::vector<GrassBlade> full = tileCopy(nearField, 0, 0);
    ASSERT_EQ(full.size(), static_cast<size_t>(kGrassLod0PerTile));
    const uint32_t slot = full[0].tileSlot;
    HeightMap*     flatHeight = flat.editableWorking();
    ASSERT_NE(flatHeight, nullptr);
    for (uint32_t z = 0; z < flatHeight->height(); ++z)
    {
        for (uint32_t x = 0; x < flatHeight->width(); ++x)
            flatHeight->setHeight(static_cast<int>(x), static_cast<int>(z), 50.0f);
    }
    nearField.update(flat, 34.0f, 4.0f, 0.0, -1000.0f);
    ASSERT_EQ(nearField.residentLod(0, 0), 1);
    const std::vector<GrassBlade> dropped = tileCopy(nearField, 0, 0);
    EXPECT_EQ(dropped.size(), static_cast<size_t>(kGrassLod1PerTile));
    for (const GrassBlade& blade : dropped)
    {
        bool found = false;
        for (const GrassBlade& kept : full)
        {
            if (kept.x == blade.x && kept.y == blade.y && kept.z == blade.z)
            {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);
        EXPECT_EQ(blade.tileSlot, slot);
    }
}

TEST(GrassField, SpawnGates)
{
    TerrainGrid grass;
    ASSERT_TRUE(makeGrid(grass, 9, Vector3f(0.0f, 0.0f, 0.0f), 0.0f, 0, 255, 0, 0, true));
    GrassField field;
    enable(field, 1337u);
    field.update(grass, 4.0f, 4.0f, 2.0, -1000.0f);
    EXPECT_EQ(bladeTotal(field), kGrassLod0PerTile);
    const GrassField::BladeSpan span = field.tileBlades(0, 0);
    ASSERT_NE(span.data, nullptr);
    const GrassWindSample wind = sampleGrassWind(4.0f, 4.0f, 2.0, field.params());
    const GrassTileWind&  row  = field.tileWind()[span.data[0].tileSlot];
    EXPECT_FLOAT_EQ(row.fade, 1.0f);
    EXPECT_FLOAT_EQ(row.windX, wind.dirX * wind.strength * field.params().windTipMetres);
    EXPECT_FLOAT_EQ(row.windZ, wind.dirZ * wind.strength * field.params().windTipMetres);
    for (uint32_t i = 0; i < span.count; ++i)
        EXPECT_TRUE(grass.containsXZ(span.data[i].x, span.data[i].z));

    const uint8_t gates[][4] = {
        { 0, 0, 255, 0 },
        { 0, 0, 0, 255 },
        { 255, 0, 0, 0 },
        { 0, 0, 0, 0 },
    };
    for (const uint8_t* texel : gates)
    {
        TerrainGrid blocked;
        ASSERT_TRUE(makeGrid(blocked, 9, Vector3f(0.0f, 0.0f, 0.0f), 0.0f, texel[0], texel[1], texel[2], texel[3], true));
        GrassField none;
        enable(none, 1337u);
        none.update(blocked, 4.0f, 4.0f, 0.0, -1000.0f);
        EXPECT_EQ(bladeTotal(none), 0u);
    }

    TerrainGrid steep;
    ASSERT_TRUE(makeRamp(steep, std::sqrt(3.0f)));
    GrassField slope;
    enable(slope, 1337u);
    slope.update(steep, 4.0f, 4.0f, 0.0, -1000.0f);
    EXPECT_EQ(bladeTotal(slope), 0u);

    TerrainGrid wet;
    ASSERT_TRUE(makeGrid(wet, 9, Vector3f(0.0f, 0.0f, 0.0f), 0.0f, 0, 255, 0, 0, true));
    GrassField drowned;
    enable(drowned, 1337u);
    drowned.update(wet, 4.0f, 4.0f, 0.0, 1.0f);
    EXPECT_EQ(bladeTotal(drowned), 0u);

    TerrainGrid bare;
    ASSERT_TRUE(makeGrid(bare, 2, Vector3f(0.0f, 0.0f, 0.0f), 0.0f, 0, 255, 0, 0, true));
    GrassField outside;
    enable(outside, 1337u);
    outside.update(bare, 0.5f, 0.5f, 0.0, -1000.0f);
    EXPECT_EQ(bladeTotal(outside), 0u);
    EXPECT_EQ(outside.residentTileCount(), 0u);

    TerrainGrid unsplat;
    ASSERT_TRUE(makeGrid(unsplat, 9, Vector3f(0.0f, 0.0f, 0.0f), 0.0f, 0, 255, 0, 0, false));
    GrassField missing;
    enable(missing, 1337u);
    missing.update(unsplat, 4.0f, 4.0f, 0.0, -1000.0f);
    EXPECT_EQ(bladeTotal(missing), 0u);
    EXPECT_EQ(missing.residentTileCount(), 0u);
}
