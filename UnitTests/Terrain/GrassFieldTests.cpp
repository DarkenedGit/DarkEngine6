#include <gtest/gtest.h>

#include "Math/MathHelper.h"
#include "Math/Vector3f.h"
#include "Terrain/GrassBend.h"
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

    int longestCircularRun(const bool* line, int n)
    {
        bool all  = true;
        int  best = 0;
        int  cur  = 0;
        for (int i = 0; i < n; ++i)
        {
            if (line[i])
            {
                ++cur;
                if (cur > best)
                    best = cur;
            }
            else
            {
                all = false;
                cur = 0;
            }
        }
        if (all)
            return n;
        int head = 0;
        while (head < n && line[head])
            ++head;
        int tail = 0;
        while (tail < n && line[n - 1 - tail])
            ++tail;
        if (head + tail > best)
            best = head + tail;
        return best;
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

TEST(GrassField, CoarseTilesAreScattered)
{
    struct Band
    {
        float    playerX;
        int      lod;
        int      perLine;
        int      maxGap;
        uint32_t blades;
    };
    const Band bands[] = {
        { 24.0f, 1, 32, 4, kGrassLod1PerTile },
        { 54.0f, 2, 16, 6, kGrassLod2PerTile },
        { 104.0f, 3, 10, 8, kGrassLod3PerTile },
    };
    constexpr int   kSide = 64;
    constexpr float kCell = kGrassTileMetres / static_cast<float>(kSide);

    for (const Band& band : bands)
    {
        TerrainGrid grid;
        ASSERT_TRUE(makeGrid(grid, 9, Vector3f(0.0f, 0.0f, 0.0f), 0.0f, 0, 255, 0, 0, true));
        GrassField field;
        enable(field, 1337u);
        field.update(grid, band.playerX, 4.0f, 0.0, -1000.0f);
        ASSERT_EQ(field.residentLod(0, 0), band.lod) << band.playerX;
        const std::vector<GrassBlade> blades = tileCopy(field, 0, 0);
        ASSERT_EQ(blades.size(), static_cast<size_t>(band.blades)) << band.lod;

        bool occupied[kSide][kSide] = {};
        for (const GrassBlade& blade : blades)
        {
            const int ix = static_cast<int>(std::floor(blade.x / kCell));
            const int iz = static_cast<int>(std::floor(blade.z / kCell));
            ASSERT_GE(ix, 0) << band.lod;
            ASSERT_LT(ix, kSide) << band.lod;
            ASSERT_GE(iz, 0) << band.lod;
            ASSERT_LT(iz, kSide) << band.lod;
            EXPECT_FALSE(occupied[iz][ix]) << band.lod;
            occupied[iz][ix] = true;
        }

        int gap = 0;
        for (int z = 0; z < kSide; ++z)
        {
            int  count = 0;
            bool empty[kSide];
            for (int x = 0; x < kSide; ++x)
            {
                count += occupied[z][x] ? 1 : 0;
                empty[x] = !occupied[z][x];
            }
            EXPECT_EQ(count, band.perLine) << band.lod;
            const int run = longestCircularRun(empty, kSide);
            if (run > gap)
                gap = run;
        }
        for (int x = 0; x < kSide; ++x)
        {
            int  count = 0;
            bool empty[kSide];
            for (int z = 0; z < kSide; ++z)
            {
                count += occupied[z][x] ? 1 : 0;
                empty[z] = !occupied[z][x];
            }
            EXPECT_EQ(count, band.perLine) << band.lod;
            const int run = longestCircularRun(empty, kSide);
            if (run > gap)
                gap = run;
        }
        for (int d = 0; d < kSide; ++d)
        {
            bool emptyDiag[kSide];
            bool emptyAnti[kSide];
            for (int z = 0; z < kSide; ++z)
            {
                emptyDiag[z] = !occupied[z][(z + d) & (kSide - 1)];
                emptyAnti[z] = !occupied[z][(d - z) & (kSide - 1)];
            }
            const int diag = longestCircularRun(emptyDiag, kSide);
            const int anti = longestCircularRun(emptyAnti, kSide);
            if (diag > gap)
                gap = diag;
            if (anti > gap)
                gap = anti;
        }
        EXPECT_LE(gap, band.maxGap) << band.lod;
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

namespace
{
    float shoveLength(const GrassShove& shove)
    {
        return std::sqrt(shove.x * shove.x + shove.z * shove.z);
    }

    bool makeGrass(TerrainGrid& grid)
    {
        return makeGrid(grid, 9, Vector3f(0.0f, 0.0f, 0.0f), 0.0f, 0, 255, 0, 0, true);
    }
} // namespace

TEST(GrassField, PlayerCenterShove_IsFlexTimesPush)
{
    TerrainGrid grid;
    ASSERT_TRUE(makeGrass(grid));
    GrassField field;
    enable(field, 1337u);

    GrassPlayerSample player;
    player.x       = 4.0f;
    player.z       = 4.0f;
    player.shoving = true;
    field.update(grid, player.x, player.z, 0.0, -1000.0f, player);

    EXPECT_FLOAT_EQ(field.interactors()[0].x, player.x);
    EXPECT_FLOAT_EQ(field.interactors()[0].z, player.z);
    EXPECT_FLOAT_EQ(field.interactors()[0].strength, 1.0f);
    EXPECT_FLOAT_EQ(field.interactors()[0].radius, kGrassShoveRadiusMetres);
    EXPECT_FLOAT_EQ(field.prevInteractors()[0].strength, 0.0f);
    for (int i = 1; i < kGrassInteractorSlots; ++i)
    {
        EXPECT_FLOAT_EQ(field.interactors()[i].strength, 0.0f);
        EXPECT_FLOAT_EQ(field.interactors()[i].radius, 0.0f);
    }

    const float height = 1.0f;
    const float flex   = 0.65f;
    EXPECT_LT(flex * kGrassPushMetres, 0.85f * height);
    EXPECT_FLOAT_EQ(grassShoveFalloff(0.0f, kGrassShoveRadiusMetres), 1.0f);

    const GrassShove shove = grassShoveMetres(0, player.x, player.z, field.interactors(), field.footprints(), player.velX, player.velZ, kGrassPushMetres);
    GrassTipIn       in;
    in.height          = height;
    in.flex            = flex;
    in.shoveX          = shove.x;
    in.shoveZ          = shove.z;
    const GrassTip tip = grassTipOffset(in);
    EXPECT_NEAR(shoveLength(shove), kGrassPushMetres, 1e-5f);
    EXPECT_NEAR(std::sqrt(tip.x * tip.x + tip.z * tip.z), flex * kGrassPushMetres, 1e-5f);
}

TEST(GrassField, ShovePastRadius_IsZero)
{
    TerrainGrid grid;
    ASSERT_TRUE(makeGrass(grid));
    GrassField field;
    enable(field, 1337u);

    GrassPlayerSample player;
    player.x       = 4.0f;
    player.z       = 4.0f;
    player.shoving = true;
    field.update(grid, player.x, player.z, 0.0, -1000.0f, player);

    EXPECT_FLOAT_EQ(grassShoveFalloff(kGrassShoveRadiusMetres, kGrassShoveRadiusMetres), 0.0f);
    const GrassShove past = grassShoveMetres(0, player.x + 1.20f, player.z, field.interactors(), field.footprints(), 0.0f, 0.0f, kGrassPushMetres);
    EXPECT_FLOAT_EQ(past.x, 0.0f);
    EXPECT_FLOAT_EQ(past.z, 0.0f);

    GrassTipIn in;
    in.height          = 1.0f;
    in.flex            = 1.0f;
    in.shoveX          = past.x;
    in.shoveZ          = past.z;
    const GrassTip tip = grassTipOffset(in);
    EXPECT_FLOAT_EQ(tip.x, 0.0f);
    EXPECT_FLOAT_EQ(tip.z, 0.0f);
}

TEST(GrassField, Lod2AndLod3_IgnorePlayerOnBlade)
{
    TerrainGrid grid;
    ASSERT_TRUE(makeGrass(grid));
    GrassField field;
    enable(field, 1337u);

    GrassPlayerSample player;
    player.x       = 4.0f;
    player.z       = 4.0f;
    player.shoving = true;
    field.update(grid, player.x, player.z, 0.0, -1000.0f, player);

    const GrassShove lod0 = grassShoveMetres(0, player.x, player.z, field.interactors(), field.footprints(), 0.0f, 0.0f, kGrassPushMetres);
    const GrassShove lod1 = grassShoveMetres(1, player.x, player.z, field.interactors(), field.footprints(), 0.0f, 0.0f, kGrassPushMetres);
    EXPECT_GT(shoveLength(lod0), 0.5f);
    EXPECT_NEAR(lod1.x, lod0.x, 1e-6f);
    EXPECT_NEAR(lod1.z, lod0.z, 1e-6f);

    for (int lod = 2; lod <= 3; ++lod)
    {
        const GrassShove ignored = grassShoveMetres(lod, player.x, player.z, field.interactors(), field.footprints(), 0.0f, 0.0f, kGrassPushMetres);
        EXPECT_FLOAT_EQ(ignored.x, 0.0f);
        EXPECT_FLOAT_EQ(ignored.z, 0.0f);
        GrassTipIn in;
        in.height          = 1.0f;
        in.flex            = 1.0f;
        in.shoveX          = ignored.x;
        in.shoveZ          = ignored.z;
        const GrassTip tip = grassTipOffset(in);
        EXPECT_FLOAT_EQ(tip.x, 0.0f);
        EXPECT_FLOAT_EQ(tip.z, 0.0f);
    }
}

TEST(GrassField, FootprintStrength_DecaysOverHalfSecond)
{
    TerrainGrid grid;
    ASSERT_TRUE(makeGrass(grid));
    GrassField field;
    enable(field, 1337u);

    GrassPlayerSample player;
    player.x       = 4.0f;
    player.z       = 4.0f;
    player.shoving = true;
    field.update(grid, player.x, player.z, 0.0, -1000.0f, player);

    player.x = 4.36f;
    field.update(grid, player.x, player.z, 0.0, -1000.0f, player);

    int live = -1;
    for (int i = 0; i < kGrassFootprintSlots; ++i)
    {
        if (field.footprints()[i].strength > 0.0f)
        {
            live = i;
            break;
        }
    }
    ASSERT_GE(live, 0);
    EXPECT_FLOAT_EQ(field.footprints()[live].strength, 1.0f);
    EXPECT_FLOAT_EQ(field.footprints()[live].radius, kGrassShoveRadiusMetres);
    EXPECT_FLOAT_EQ(field.footprints()[live].x, player.x);
    EXPECT_FLOAT_EQ(field.footprints()[live].z, player.z);

    field.update(grid, player.x, player.z, 0.25, -1000.0f, player);
    EXPECT_NEAR(field.footprints()[live].strength, 0.5f, 1e-5f);
    EXPECT_NEAR(field.prevFootprints()[live].strength, 1.0f, 1e-5f);

    field.update(grid, player.x, player.z, 0.50, -1000.0f, player);
    EXPECT_FLOAT_EQ(field.footprints()[live].x, 0.0f);
    EXPECT_FLOAT_EQ(field.footprints()[live].z, 0.0f);
    EXPECT_FLOAT_EQ(field.footprints()[live].strength, 0.0f);
    EXPECT_FLOAT_EQ(field.footprints()[live].radius, 0.0f);
    EXPECT_NEAR(field.prevFootprints()[live].strength, 0.5f, 1e-5f);
}

TEST(GrassField, NonShovingSample_WritesZeroInteractor)
{
    TerrainGrid grid;
    ASSERT_TRUE(makeGrass(grid));
    GrassField field;
    enable(field, 1337u);
    field.update(grid, 4.0f, 4.0f, 0.0, -1000.0f);

    GrassFrameConstants frame{};
    frame.viewProj[0]            = 7.0f;
    frame.interactor[0].strength = 4.0f;
    frame.pad0                   = 3.0f;
    field.writeFrameInteraction(frame);
    EXPECT_FLOAT_EQ(frame.viewProj[0], 7.0f);
    EXPECT_FLOAT_EQ(frame.pushMetres, kGrassPushMetres);
    EXPECT_FLOAT_EQ(frame.pad0, 0.0f);
    for (int i = 0; i < kGrassInteractorSlots; ++i)
    {
        EXPECT_FLOAT_EQ(field.interactors()[i].x, 0.0f);
        EXPECT_FLOAT_EQ(field.interactors()[i].z, 0.0f);
        EXPECT_FLOAT_EQ(field.interactors()[i].strength, 0.0f);
        EXPECT_FLOAT_EQ(field.interactors()[i].radius, 0.0f);
        EXPECT_FLOAT_EQ(frame.interactor[i].strength, 0.0f);
        EXPECT_FLOAT_EQ(frame.interactor[i].radius, 0.0f);
        EXPECT_FLOAT_EQ(frame.prevInteractor[i].strength, 0.0f);
    }

    GrassPlayerSample shove;
    shove.x       = 2.0f;
    shove.z       = 3.0f;
    shove.shoving = true;
    field.update(grid, shove.x, shove.z, 1.0, -1000.0f, shove);
    EXPECT_FLOAT_EQ(field.interactors()[0].strength, 1.0f);

    field.update(grid, shove.x, shove.z, 1.0, -1000.0f);
    EXPECT_FLOAT_EQ(field.interactors()[0].x, 0.0f);
    EXPECT_FLOAT_EQ(field.interactors()[0].z, 0.0f);
    EXPECT_FLOAT_EQ(field.interactors()[0].strength, 0.0f);
    EXPECT_FLOAT_EQ(field.interactors()[0].radius, 0.0f);
    EXPECT_FLOAT_EQ(field.prevInteractors()[0].x, shove.x);
    EXPECT_FLOAT_EQ(field.prevInteractors()[0].z, shove.z);
    EXPECT_FLOAT_EQ(field.prevInteractors()[0].strength, 1.0f);
    EXPECT_FLOAT_EQ(field.prevInteractors()[0].radius, kGrassShoveRadiusMetres);
    for (int i = 1; i < kGrassInteractorSlots; ++i)
        EXPECT_FLOAT_EQ(field.prevInteractors()[i].strength, 0.0f);
}

TEST(GrassField, MovingPlayer_BiasesShoveTowardVelocity)
{
    TerrainGrid grid;
    ASSERT_TRUE(makeGrass(grid));
    GrassField field;
    enable(field, 1337u);

    GrassPlayerSample player;
    player.x       = 4.0f;
    player.z       = 4.0f;
    player.velX    = 0.0f;
    player.velZ    = 2.0f;
    player.shoving = true;
    field.update(grid, player.x, player.z, 0.0, -1000.0f, player);

    const float      bladeX = player.x + 1.0f;
    const float      bladeZ = player.z;
    const float      fall   = grassShoveFalloff(1.0f, kGrassShoveRadiusMetres);
    const GrassShove still  = grassShoveMetres(0, bladeX, bladeZ, field.interactors(), field.footprints(), 0.0f, 0.0f, kGrassPushMetres);
    EXPECT_NEAR(still.x, kGrassPushMetres * fall, 1e-5f);
    EXPECT_NEAR(still.z, 0.0f, 1e-5f);

    const GrassShove moving = grassShoveMetres(0, bladeX, bladeZ, field.interactors(), field.footprints(), player.velX, player.velZ, kGrassPushMetres);
    const float      mx     = kGrassShoveAwayWeight;
    const float      mz     = kGrassShoveVelWeight;
    const float      inv    = 1.0f / std::sqrt(mx * mx + mz * mz);
    EXPECT_NEAR(moving.x, inv * mx * kGrassPushMetres * fall, 1e-5f);
    EXPECT_NEAR(moving.z, inv * mz * kGrassPushMetres * fall, 1e-5f);

    EXPECT_EQ(grassPackYawBits(0.50f, 0.0f), 0);
    EXPECT_GT(grassPackYawBits(0.0f, 0.51f), 0);

    GrassFrameConstants frame{};
    field.writeFrameInteraction(frame);
    GrassPlanarYaw current{};
    GrassPlanarYaw previous{};
    grassUnpackPlanarYaw(frame.pad0, current, previous);
    EXPECT_TRUE(current.bias);
    EXPECT_FALSE(previous.bias);
    EXPECT_NEAR(current.x, 0.0f, 1e-3f);
    EXPECT_NEAR(current.z, 1.0f, 1e-3f);

    player.velX = 2.0f;
    player.velZ = 0.0f;
    field.update(grid, player.x, player.z, 0.10, -1000.0f, player);
    field.writeFrameInteraction(frame);
    grassUnpackPlanarYaw(frame.pad0, current, previous);
    EXPECT_TRUE(current.bias);
    EXPECT_TRUE(previous.bias);
    EXPECT_NEAR(current.x, 1.0f, 1e-3f);
    EXPECT_NEAR(current.z, 0.0f, 1e-3f);
    EXPECT_NEAR(previous.x, 0.0f, 1e-3f);
    EXPECT_NEAR(previous.z, 1.0f, 1e-3f);
}
