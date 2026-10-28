#include <gtest/gtest.h>

#include "Terrain/LeafField.h"
#include "Terrain/LeafWind.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace Dark::Terrain;

namespace
{
    struct FlatWind
    {
        float strength = 0.0f;
        float dirX     = 1.0f;
        float dirZ     = 0.0f;
    };

    struct FlatBed
    {
        float y   = 0.0f;
        bool  hit = true;
    };

    void flatWind(float, float, double, float& dirX, float& dirZ, float& strength, void* user)
    {
        const FlatWind* wind = static_cast<const FlatWind*>(user);
        dirX                 = wind->dirX;
        dirZ                 = wind->dirZ;
        strength             = wind->strength;
    }

    bool flatBed(float, float, float& bedY, void* user)
    {
        const FlatBed* bed = static_cast<const FlatBed*>(user);
        if (!bed->hit)
            return false;
        bedY = bed->y;
        return true;
    }

    LeafField::Crown crownAt(uint32_t id, float x, float z, float crownY)
    {
        LeafField::Crown crown;
        crown.id     = id;
        crown.x      = x;
        crown.seatY  = 0.0f;
        crown.z      = z;
        crown.crownY = crownY;
        return crown;
    }

    LeafField::Step makeStep(const LeafField::Crown* crowns, int count, FlatWind* wind, FlatBed* bed)
    {
        LeafField::Step step;
        step.dt         = 0.05f;
        step.crowns     = crowns;
        step.crownCount = count;
        step.wind       = flatWind;
        step.windUser   = wind;
        step.tryHeight  = flatBed;
        step.heightUser = bed;
        step.cellMetres = 1.0f;
        return step;
    }

    void stepFor(LeafField& field, LeafField::Step step, float seconds)
    {
        float t = 0.0f;
        while (t < seconds - 1.0e-4f)
        {
            const float dt = std::min(step.dt, seconds - t);
            step.dt        = dt;
            step.simTime += static_cast<double>(dt);
            field.step(step);
            t += dt;
        }
    }

    int countPhase(const LeafField& field, uint32_t crownId, LeafPhase phase)
    {
        int n = 0;
        for (int i = 0; i < LeafField::kPoolSize; ++i)
        {
            LeafSlot slot;
            if (!field.copySlot(i, slot))
                continue;
            if (slot.phase == phase && slot.crownId == crownId)
                ++n;
        }
        return n;
    }

    bool firstOccupied(const LeafField& field, LeafSlot& out)
    {
        for (int i = 0; i < LeafField::kPoolSize; ++i)
        {
            if (!field.copySlot(i, out) || out.phase == LeafPhase::Free)
                continue;
            return true;
        }
        return false;
    }

    int signOf(float v)
    {
        if (v > 0.05f)
            return 1;
        if (v < -0.05f)
            return -1;
        return 0;
    }

    void forwardLeafWind(float x, float z, double simTime, float& dirX, float& dirZ, float& strength, void*)
    {
        const LeafWindSample sample = sampleLeafWind(x, z, simTime);
        dirX                         = sample.dirX;
        dirZ                         = sample.dirZ;
        strength                     = sample.strength;
    }

    struct RiseBed
    {
        float low    = 0.0f;
        float high   = 6.0f;
        float splitX = 1.0f;
    };

    bool riseBed(float x, float, float& bedY, void* user)
    {
        const RiseBed* bed = static_cast<const RiseBed*>(user);
        bedY               = x >= bed->splitX ? bed->high : bed->low;
        return true;
    }

    struct TiltNormal
    {
        float nx = 0.0f;
        float ny = 0.0f;
        float nz = 1.0f;
    };

    void tiltNormal(float, float, float& nx, float& ny, float& nz, void* user)
    {
        const TiltNormal* tilt = static_cast<const TiltNormal*>(user);
        nx                     = tilt->nx;
        ny                     = tilt->ny;
        nz                     = tilt->nz;
    }

    bool findSlot(const LeafField& field, uint32_t serial, LeafSlot& out)
    {
        for (int i = 0; i < LeafField::kPoolSize; ++i)
        {
            LeafSlot slot;
            if (!field.copySlot(i, slot))
                continue;
            if (slot.phase != LeafPhase::Free && slot.serial == serial)
            {
                out = slot;
                return true;
            }
        }
        return false;
    }

    struct Landing
    {
        uint32_t serial  = 0;
        uint32_t crownId = 0;
        float    x       = 0.0f;
        float    z       = 0.0f;
    };

    void collectLandings(const LeafField& field, std::vector<Landing>& lands)
    {
        for (int i = 0; i < LeafField::kPoolSize; ++i)
        {
            LeafSlot slot;
            if (!field.copySlot(i, slot))
                continue;
            if (slot.phase != LeafPhase::Rest && slot.phase != LeafPhase::Fade)
                continue;
            bool seen = false;
            for (const Landing& land : lands)
            {
                if (land.serial == slot.serial)
                    seen = true;
            }
            if (seen)
                continue;
            Landing land;
            land.serial  = slot.serial;
            land.crownId = slot.crownId;
            land.x       = slot.x;
            land.z       = slot.z;
            lands.push_back(land);
        }
    }

    float downwind(const Landing& land, float originX)
    {
        return land.x - originX;
    }

    bool waitForLiving(LeafField& field, LeafField::Step& step, float maxSeconds)
    {
        float t = 0.0f;
        while (field.livingCount() < 1 && t < maxSeconds)
        {
            step.dt = 0.05f;
            step.simTime += 0.05;
            field.step(step);
            t += 0.05f;
        }
        return field.livingCount() >= 1;
    }
} // namespace

TEST(LeafField, CalmCrown_FewLeavesLandNearTheTree)
{
    FlatWind wind;
    wind.strength = 0.0f;
    FlatBed bed;
    const LeafField::Crown tree = crownAt(1, 4.0f, 6.0f, 12.0f);
    LeafField              field;
    field.setShedScale(1.0f);
    LeafField::Step step = makeStep(&tree, 1, &wind, &bed);
    step.cameraX         = tree.x;
    step.cameraZ         = tree.z;

    std::vector<Landing> lands;
    float                t = 0.0f;
    while (t < 6.0f)
    {
        step.dt = 0.05f;
        step.simTime += 0.05;
        field.step(step);
        collectLandings(field, lands);
        t += 0.05f;
    }

    EXPECT_GE(field.spawnedTotal(), 1u);
    EXPECT_LE(field.spawnedTotal(), 5u);
    EXPECT_FALSE(lands.empty());
    for (const Landing& land : lands)
    {
        const float dx = land.x - tree.x;
        const float dz = land.z - tree.z;
        EXPECT_LT(dx * dx + dz * dz, 1.5f * 1.5f);
    }
}

TEST(LeafField, StrongWind_LandsFartherAndShedsMore)
{
    FlatBed bed;
    const LeafField::Crown tree = crownAt(1, 0.0f, 0.0f, 12.0f);

    auto run = [&](float strength, uint32_t& spawned, float& furthest) {
        FlatWind wind;
        wind.strength = strength;
        wind.dirX     = 1.0f;
        wind.dirZ     = 0.0f;
        LeafField       field;
        LeafField::Step step = makeStep(&tree, 1, &wind, &bed);
        std::vector<Landing> lands;
        float                t = 0.0f;
        while (t < 6.0f)
        {
            step.dt = 0.05f;
            step.simTime += 0.05;
            field.step(step);
            collectLandings(field, lands);
            t += 0.05f;
        }
        spawned  = field.spawnedTotal();
        furthest = 0.0f;
        for (const Landing& land : lands)
            furthest = std::max(furthest, downwind(land, tree.x));
    };

    uint32_t calmSpawned = 0;
    uint32_t windSpawned = 0;
    float    calmFar     = 0.0f;
    float    windFar     = 0.0f;
    run(0.0f, calmSpawned, calmFar);
    run(1.0f, windSpawned, windFar);

    EXPECT_GT(windSpawned, calmSpawned);
    EXPECT_GT(windFar, calmFar + 2.0f);
}

TEST(LeafField, TwoCrowns_WindierLandsFartherDownwind)
{
    FlatBed bed;
    LeafField::Crown trees[2] = { crownAt(1, 0.0f, 0.0f, 12.0f), crownAt(2, 40.0f, 0.0f, 12.0f) };

    struct SplitWind
    {
    };
    auto windAt = [](float x, float, double, float& dirX, float& dirZ, float& strength, void*) {
        dirX     = 1.0f;
        dirZ     = 0.0f;
        strength = x < 20.0f ? 0.1f : 0.95f;
    };

    LeafField       field;
    LeafField::Step step;
    step.dt         = 0.05f;
    step.crowns     = trees;
    step.crownCount = 2;
    step.wind       = windAt;
    step.tryHeight  = flatBed;
    step.heightUser = &bed;
    step.cameraX    = 20.0f;

    std::vector<Landing> lands;
    float                t = 0.0f;
    while (t < 6.0f)
    {
        step.dt = 0.05f;
        step.simTime += 0.05;
        field.step(step);
        collectLandings(field, lands);
        t += 0.05f;
    }

    float calmFar = -1000.0f;
    float windFar = -1000.0f;
    for (const Landing& land : lands)
    {
        if (land.crownId == 1)
            calmFar = std::max(calmFar, downwind(land, trees[0].x));
        if (land.crownId == 2)
            windFar = std::max(windFar, downwind(land, trees[1].x));
    }
    EXPECT_GT(calmFar, -100.0f);
    EXPECT_GT(windFar, calmFar + 2.0f);
}

TEST(LeafField, FullGrove_EdgeCrownStillShedsAndPoolStaysSparse)
{
    constexpr int kCrowns = 400;
    std::vector<LeafField::Crown> trees(static_cast<size_t>(kCrowns));
    for (int i = 0; i < kCrowns; ++i)
    {
        const float z = 4.0f + static_cast<float>(i) * (86.0f / static_cast<float>(kCrowns - 1));
        trees[static_cast<size_t>(i)] = crownAt(static_cast<uint32_t>(i + 1), 0.0f, z, 8.0f);
    }
    LeafField::Crown outside = crownAt(9001, 200.0f, 0.0f, 8.0f);
    trees.push_back(outside);

    FlatWind wind;
    wind.strength = 0.8f;
    FlatBed bed;
    LeafField field;
    field.setShedScale(1.0f);
    LeafField::Step step = makeStep(trees.data(), static_cast<int>(trees.size()), &wind, &bed);
    step.cameraX         = 0.0f;
    step.cameraZ         = 0.0f;
    step.inFrustum       = [](float, float, float, void*) { return true; };

    int  maxLiving   = 0;
    bool edgeSpawned = false;
    bool outsideSeen = false;
    float t          = 0.0f;
    while (t < 8.0f)
    {
        step.dt = 0.05f;
        step.simTime += 0.05;
        field.step(step);
        maxLiving = std::max(maxLiving, field.livingCount());
        EXPECT_LE(field.livingCount(), LeafField::kPoolSize);
        for (int i = 0; i < LeafField::kPoolSize; ++i)
        {
            LeafSlot slot;
            if (!field.copySlot(i, slot) || slot.phase == LeafPhase::Free)
                continue;
            if (slot.crownId == trees[static_cast<size_t>(kCrowns - 1)].id)
                edgeSpawned = true;
            if (slot.crownId == outside.id)
                outsideSeen = true;
        }
        t += 0.05f;
    }

    EXPECT_GT(field.spawnedTotal(), 0u);
    EXPECT_TRUE(edgeSpawned);
    EXPECT_FALSE(outsideSeen);
    EXPECT_LE(maxLiving, LeafField::kPoolSize);
    EXPECT_GT(maxLiving, 8);
}

TEST(LeafField, ScaleZero_SpawnsNothing)
{
    FlatWind wind;
    wind.strength = 1.0f;
    FlatBed bed;
    const LeafField::Crown tree = crownAt(1, 0.0f, 0.0f, 12.0f);
    LeafField              field;
    field.setShedScale(0.0f);
    LeafField::Step step = makeStep(&tree, 1, &wind, &bed);
    stepFor(field, step, 4.0f);
    EXPECT_EQ(field.spawnedTotal(), 0u);
    EXPECT_EQ(field.livingCount(), 0);
}

TEST(LeafField, SmallStep_StaysAirborneAboveADistantSurface)
{
    FlatWind wind;
    wind.strength = 0.0f;
    FlatBed bed;
    bed.y = 0.0f;
    const LeafField::Crown tree = crownAt(1, 0.0f, 0.0f, 12.0f);
    LeafField              field;
    LeafField::Step        step = makeStep(&tree, 1, &wind, &bed);
    ASSERT_TRUE(waitForLiving(field, step, 5.0f));

    EXPECT_EQ(field.livingCount(), 1);
    LeafSlot slot;
    bool found = false;
    for (int i = 0; i < LeafField::kPoolSize; ++i)
    {
        if (!field.copySlot(i, slot))
            continue;
        if (slot.phase == LeafPhase::Free)
            continue;
        found = true;
        EXPECT_EQ(slot.phase, LeafPhase::Airborne);
        EXPECT_GT(slot.y, 10.0f);
    }
    EXPECT_TRUE(found);
}

TEST(LeafField, CrossingSegment_SnapsToBedAndStops)
{
    FlatWind wind;
    wind.strength = 0.0f;
    FlatBed bed;
    bed.y = 4.0f;
    const LeafField::Crown tree = crownAt(1, 2.0f, 3.0f, 5.0f);
    LeafField              field;
    LeafField::Step        step = makeStep(&tree, 1, &wind, &bed);
    ASSERT_TRUE(waitForLiving(field, step, 5.0f));
    step.dt = 1.0f;
    field.step(step);

    LeafSlot slot{};
    bool     found = false;
    for (int i = 0; i < LeafField::kPoolSize; ++i)
    {
        if (!field.copySlot(i, slot) || slot.phase == LeafPhase::Free)
            continue;
        found = true;
        break;
    }
    ASSERT_TRUE(found);
    EXPECT_EQ(slot.phase, LeafPhase::Rest);
    EXPECT_FLOAT_EQ(slot.y, 4.0f);
    EXPECT_FLOAT_EQ(slot.vx, 0.0f);
    EXPECT_FLOAT_EQ(slot.vy, 0.0f);
    EXPECT_FLOAT_EQ(slot.vz, 0.0f);
    EXPECT_FLOAT_EQ(slot.nx, 0.0f);
    EXPECT_FLOAT_EQ(slot.ny, 1.0f);
    EXPECT_FLOAT_EQ(slot.nz, 0.0f);
}

TEST(LeafField, StillWater_RestsOnTheStillLevelNotTheBed)
{
    FlatWind wind;
    FlatBed  bed;
    bed.y = 1.0f;
    const LeafField::Crown tree = crownAt(1, 0.0f, 0.0f, 8.0f);
    LeafField::Pond        pond;
    pond.minX  = -5.0f;
    pond.minZ  = -5.0f;
    pond.maxX  = 5.0f;
    pond.maxZ  = 5.0f;
    pond.level = 4.0f;

    LeafField       field;
    LeafField::Step step = makeStep(&tree, 1, &wind, &bed);
    step.ponds           = &pond;
    step.pondCount       = 1;
    ASSERT_TRUE(waitForLiving(field, step, 5.0f));
    step.dt = 1.0f;
    field.step(step);

    LeafSlot slot{};
    bool     found = false;
    for (int i = 0; i < LeafField::kPoolSize; ++i)
    {
        if (!field.copySlot(i, slot) || slot.phase == LeafPhase::Free)
            continue;
        found = true;
        break;
    }
    ASSERT_TRUE(found);
    EXPECT_EQ(slot.phase, LeafPhase::Rest);
    EXPECT_FLOAT_EQ(slot.y, pond.level);
    EXPECT_NE(slot.y, bed.y);
}

TEST(LeafField, LakeSheet_RestsOnTheStillLevelWhenTheBedIsBelowIt)
{
    FlatWind wind;
    FlatBed  bed;
    bed.y = 2.0f;
    const LeafField::Crown tree = crownAt(1, 10.0f, -4.0f, 12.0f);
    LeafField              field;
    LeafField::Step        step = makeStep(&tree, 1, &wind, &bed);
    step.lake                   = true;
    step.lakeLevel              = 7.0f;
    ASSERT_TRUE(waitForLiving(field, step, 5.0f));
    step.dt = 1.0f;
    field.step(step);

    LeafSlot slot{};
    bool     found = false;
    for (int i = 0; i < LeafField::kPoolSize; ++i)
    {
        if (!field.copySlot(i, slot) || slot.phase == LeafPhase::Free)
            continue;
        found = true;
        break;
    }
    ASSERT_TRUE(found);
    EXPECT_FLOAT_EQ(slot.y, 7.0f);
}

TEST(LeafField, OffHeightMap_FliesThenDiesWithoutABorderSnap)
{
    FlatWind wind;
    wind.strength = 0.0f;
    FlatBed bed;
    bed.hit = false;
    bed.y   = 1000.0f;
    const LeafField::Crown tree = crownAt(1, 50.0f, 50.0f, 20.0f);
    LeafField              field;
    LeafField::Step        step = makeStep(&tree, 1, &wind, &bed);
    step.cameraX                = tree.x;
    step.cameraZ                = tree.z;
    ASSERT_TRUE(waitForLiving(field, step, 5.0f));
    step.dt = 2.0f;
    field.step(step);

    LeafSlot slot{};
    bool     found = false;
    for (int i = 0; i < LeafField::kPoolSize; ++i)
    {
        if (!field.copySlot(i, slot) || slot.phase == LeafPhase::Free)
            continue;
        found = true;
        break;
    }
    ASSERT_TRUE(found);
    EXPECT_EQ(slot.phase, LeafPhase::Airborne);
    EXPECT_LT(slot.y, 19.0f);
    EXPECT_NE(slot.y, 1000.0f);
    const uint32_t serial = slot.serial;

    field.setShedScale(0.0f);
    step.dt = 6.1f;
    field.step(step);
    EXPECT_FALSE(findSlot(field, serial, slot));
    EXPECT_EQ(field.livingCount(), 0);
}

TEST(LeafField, RestHoldsThenFadeFreesTheSlot)
{
    FlatWind wind;
    wind.strength = 0.0f;
    FlatBed bed;
    bed.y = 4.0f;
    const LeafField::Crown tree = crownAt(1, 1.0f, 1.0f, 5.0f);
    LeafField              field;
    LeafField::Step        step = makeStep(&tree, 1, &wind, &bed);
    ASSERT_TRUE(waitForLiving(field, step, 5.0f));
    field.setShedScale(0.0f);
    step.dt = 1.0f;
    field.step(step);

    LeafSlot landed{};
    bool     found = false;
    for (int i = 0; i < LeafField::kPoolSize; ++i)
    {
        if (!field.copySlot(i, landed) || landed.phase == LeafPhase::Free)
            continue;
        found = true;
        break;
    }
    ASSERT_TRUE(found);
    ASSERT_EQ(landed.phase, LeafPhase::Rest);
    const uint32_t serial = landed.serial;
    const float    x      = landed.x;
    const float    y      = landed.y;
    const float    z      = landed.z;

    step.dt = 1.4f;
    field.step(step);
    LeafSlot held{};
    ASSERT_TRUE(findSlot(field, serial, held));
    EXPECT_EQ(held.phase, LeafPhase::Rest);
    EXPECT_FLOAT_EQ(held.x, x);
    EXPECT_FLOAT_EQ(held.y, y);
    EXPECT_FLOAT_EQ(held.z, z);

    step.dt = 0.2f;
    field.step(step);
    ASSERT_TRUE(findSlot(field, serial, held));
    EXPECT_EQ(held.phase, LeafPhase::Fade);
    EXPECT_FLOAT_EQ(held.x, x);
    EXPECT_FLOAT_EQ(held.y, y);
    EXPECT_FLOAT_EQ(held.z, z);

    step.dt = 1.05f;
    field.step(step);
    EXPECT_FALSE(findSlot(field, serial, held));
}

TEST(LeafField, OneSecondStep_SpawnsAtMostEight)
{
    FlatWind wind;
    wind.strength = 1.0f;
    FlatBed bed;
    bed.y = -100.0f;
    std::vector<LeafField::Crown> trees;
    for (int i = 0; i < 20; ++i)
        trees.push_back(crownAt(static_cast<uint32_t>(i + 1), static_cast<float>(i), 0.0f, 30.0f));
    LeafField       field;
    LeafField::Step step = makeStep(trees.data(), static_cast<int>(trees.size()), &wind, &bed);
    step.dt              = 1.0f;
    field.step(step);
    EXPECT_LE(field.spawnedTotal(), static_cast<uint32_t>(LeafField::kMaxSpawnsPerStep));
    EXPECT_EQ(field.spawnedTotal(), static_cast<uint32_t>(LeafField::kMaxSpawnsPerStep));
}

TEST(LeafField, FailedSpawn_KeepsCarryForTheNextFreeSlot)
{
    FlatWind wind;
    wind.strength = 1.0f;
    FlatBed bed;
    bed.hit = false;
    constexpr int kCrowns = 32;
    std::vector<LeafField::Crown> trees;
    for (int i = 0; i < kCrowns; ++i)
        trees.push_back(crownAt(static_cast<uint32_t>(i + 1), 0.0f, static_cast<float>(i), 15.0f));

    LeafField       field;
    LeafField::Step step = makeStep(trees.data(), kCrowns, &wind, &bed);
    step.dt              = 0.05f;
    while (field.livingCount() < LeafField::kPoolSize && step.simTime < 10.0)
    {
        step.simTime += 0.05;
        field.step(step);
    }
    ASSERT_EQ(field.livingCount(), LeafField::kPoolSize);
    const uint32_t filled = field.spawnedTotal();

    step.dt = 0.001f;
    step.simTime += 0.001;
    field.step(step);
    EXPECT_EQ(field.spawnedTotal(), filled);

    bool respawned = false;
    for (int n = 0; n < 12000 && !respawned; ++n)
    {
        step.simTime += 0.001;
        field.step(step);
        if (field.spawnedTotal() > filled)
            respawned = true;
    }
    EXPECT_TRUE(respawned);
    EXPECT_EQ(field.livingCount(), LeafField::kPoolSize);
}

TEST(LeafField, OverCapCrown_DropsTheRestingLeafAndKeepsTheAirborneOne)
{
    FlatWind wind;
    wind.strength = 1.0f;
    FlatBed bed;
    bed.y = 0.0f;
    LeafField::Crown tree = crownAt(7, 0.0f, 0.0f, 6.0f);
    LeafField        field;
    LeafField::Step  step = makeStep(&tree, 1, &wind, &bed);

    bool ready = false;
    for (int n = 0; n < 400 && !ready; ++n)
    {
        step.dt = 0.05f;
        step.simTime += 0.05;
        field.step(step);
        ready = countPhase(field, tree.id, LeafPhase::Rest) >= 1 && countPhase(field, tree.id, LeafPhase::Airborne) >= 1;
    }
    ASSERT_TRUE(ready);

    std::vector<LeafField::Crown> grove;
    grove.push_back(tree);
    for (int i = 0; i < 600; ++i)
        grove.push_back(crownAt(1000u + static_cast<uint32_t>(i), 1.0f, static_cast<float>(i % 40), 6.0f));
    step.crowns     = grove.data();
    step.crownCount = static_cast<int>(grove.size());
    step.dt         = 0.05f;
    field.step(step);

    EXPECT_EQ(countPhase(field, tree.id, LeafPhase::Rest), 0);
    EXPECT_GE(countPhase(field, tree.id, LeafPhase::Airborne), 1);
}

TEST(LeafField, ListenerPosition_DoesNotChangeSpawnCount)
{
    FlatWind wind;
    wind.strength = 0.4f;
    FlatBed bed;
    const LeafField::Crown tree = crownAt(1, 3.0f, 3.0f, 12.0f);

    auto run = [&](float listenerY) {
        LeafField       field;
        LeafField::Step step = makeStep(&tree, 1, &wind, &bed);
        step.listenerX       = 80.0f;
        step.listenerY       = listenerY;
        step.listenerZ       = -20.0f;
        stepFor(field, step, 3.0f);
        return field.spawnedTotal();
    };

    EXPECT_EQ(run(0.0f), run(40.0f));
    EXPECT_GT(run(0.0f), 0u);
}

TEST(LeafField, OutsideFrustum_DoesNotShed)
{
    FlatWind wind;
    wind.strength = 1.0f;
    FlatBed bed;
    bed.y = -50.0f;
    LeafField::Crown trees[2] = { crownAt(1, 0.0f, 8.0f, 12.0f), crownAt(2, 0.0f, -8.0f, 12.0f) };
    LeafField        field;
    LeafField::Step  step = makeStep(trees, 2, &wind, &bed);
    step.inFrustum        = [](float, float, float z, void*) { return z > 0.0f; };
    stepFor(field, step, 2.0f);

    EXPECT_GE(countPhase(field, 1, LeafPhase::Airborne) + countPhase(field, 1, LeafPhase::Rest), 1);
    EXPECT_EQ(countPhase(field, 2, LeafPhase::Airborne), 0);
    EXPECT_EQ(countPhase(field, 2, LeafPhase::Rest), 0);
    EXPECT_EQ(countPhase(field, 2, LeafPhase::Fade), 0);
}

TEST(LeafField, HigherGround_RestsWithoutADownwardCrossing)
{
    FlatWind wind;
    wind.strength = 1.0f;
    wind.dirX     = 1.0f;
    wind.dirZ     = 0.0f;
    RiseBed bed;
    const LeafField::Crown tree = crownAt(1, 0.0f, 0.0f, 3.0f);
    LeafField              field;
    LeafField::Step        step = makeStep(&tree, 1, &wind, nullptr);
    step.tryHeight              = riseBed;
    step.heightUser             = &bed;
    step.dt                     = 0.5f;
    field.step(step);
    ASSERT_EQ(countPhase(field, tree.id, LeafPhase::Airborne), 1);

    field.setShedScale(0.0f);
    field.step(step);

    LeafSlot slot{};
    ASSERT_TRUE(firstOccupied(field, slot));
    EXPECT_EQ(slot.phase, LeafPhase::Rest);
    EXPECT_GE(slot.x, bed.splitX);
    EXPECT_FLOAT_EQ(slot.y, bed.high);
    EXPECT_FLOAT_EQ(slot.vx, 0.0f);
    EXPECT_FLOAT_EQ(slot.vy, 0.0f);
    EXPECT_FLOAT_EQ(slot.vz, 0.0f);
    EXPECT_FLOAT_EQ(slot.phaseTime, 0.0f);
    EXPECT_EQ(field.livingCount(), 1);
}

TEST(LeafField, StillAboveBed_FlightCapFreesTheLeaf)
{
    FlatWind wind;
    FlatBed  bed;
    bed.y = 0.0f;
    // Four 2 s steps drop well under 500 m, so the leaf is still above the bed at 8 s.
    const LeafField::Crown tree = crownAt(1, 0.0f, 0.0f, 500.0f);
    LeafField              field;
    LeafField::Step        step = makeStep(&tree, 1, &wind, &bed);
    ASSERT_TRUE(waitForLiving(field, step, 5.0f));
    field.setShedScale(0.0f);

    for (int n = 0; n < 3; ++n)
    {
        step.dt = 2.0f;
        field.step(step);
    }
    LeafSlot slot{};
    ASSERT_TRUE(firstOccupied(field, slot));
    EXPECT_EQ(slot.phase, LeafPhase::Airborne);
    EXPECT_GT(slot.y, bed.y);

    step.dt = 2.0f;
    field.step(step);
    EXPECT_EQ(field.livingCount(), 0);
    EXPECT_FALSE(firstOccupied(field, slot));
}

TEST(LeafField, FlightCap_RestWinsWhenTheLeafHasLanded)
{
    FlatWind wind;
    FlatBed  bed;
    bed.y = 0.0f;
    const LeafField::Crown tree = crownAt(1, 0.0f, 0.0f, 10.0f);
    LeafField              field;
    LeafField::Step        step = makeStep(&tree, 1, &wind, &bed);
    ASSERT_TRUE(waitForLiving(field, step, 5.0f));
    field.setShedScale(0.0f);
    step.dt = 8.0f;
    field.step(step);

    LeafSlot slot{};
    ASSERT_TRUE(firstOccupied(field, slot));
    EXPECT_EQ(slot.phase, LeafPhase::Rest);
    EXPECT_FLOAT_EQ(slot.y, bed.y);
    EXPECT_EQ(field.livingCount(), 1);
}

TEST(LeafField, FullCrownTable_NewCrownStillSheds)
{
    std::vector<LeafField::Crown> filled(static_cast<size_t>(LeafField::kMaxCrowns));
    for (int i = 0; i < LeafField::kMaxCrowns; ++i)
        filled[static_cast<size_t>(i)] = crownAt(static_cast<uint32_t>(i + 1), 0.0f, 0.0f, 8.0f);

    FlatWind wind;
    wind.strength = 1.0f;
    FlatBed bed;
    bed.y = -100.0f;
    LeafField       field;
    field.setShedScale(0.0f);
    LeafField::Step step = makeStep(filled.data(), static_cast<int>(filled.size()), &wind, &bed);
    step.dt              = 0.05f;
    field.step(step);
    EXPECT_EQ(field.spawnedTotal(), 0u);

    const LeafField::Crown fresh = crownAt(100000u, 0.0f, 0.0f, 12.0f);
    field.setShedScale(1.0f);
    step.crowns     = &fresh;
    step.crownCount = 1;
    step.dt         = 1.0f;
    field.step(step);
    EXPECT_GE(field.spawnedTotal(), 1u);
    EXPECT_GE(countPhase(field, fresh.id, LeafPhase::Airborne), 1);
}

TEST(LeafField, FrustumMiss_KeepsShedCarry)
{
    FlatWind wind;
    wind.strength = 1.0f;
    FlatBed bed;
    bed.y = -50.0f;
    const LeafField::Crown tree = crownAt(4, 0.0f, 0.0f, 30.0f);
    LeafField              field;
    LeafField::Step        step = makeStep(&tree, 1, &wind, &bed);
    bool                   visible = true;
    step.inFrustum                 = [](float, float, float, void* user) { return *static_cast<const bool*>(user); };
    step.frustumUser               = &visible;

    step.dt = 0.3f;
    field.step(step);
    EXPECT_EQ(field.spawnedTotal(), 0u);

    visible = false;
    step.dt = 0.2f;
    field.step(step);
    EXPECT_EQ(field.spawnedTotal(), 0u);

    visible = true;
    step.dt = 0.1f;
    field.step(step);
    EXPECT_EQ(field.spawnedTotal(), 1u);
}

TEST(LeafField, CrownLeftDisk_DropsShedCarry)
{
    FlatWind wind;
    wind.strength = 1.0f;
    FlatBed bed;
    bed.y = -50.0f;
    const LeafField::Crown tree = crownAt(3, 0.0f, 0.0f, 20.0f);
    LeafField              field;
    LeafField::Step        step = makeStep(&tree, 1, &wind, &bed);
    step.dt                     = 0.3f;
    field.step(step);
    EXPECT_EQ(field.spawnedTotal(), 0u);

    step.crowns     = nullptr;
    step.crownCount = 0;
    step.dt         = 0.05f;
    field.step(step);

    step.crowns     = &tree;
    step.crownCount = 1;
    step.dt         = 0.3f;
    field.step(step);
    EXPECT_EQ(field.spawnedTotal(), 0u);
}

TEST(LeafField, LivingCrown_KeepsCarryWhenItLeavesTheDisk)
{
    FlatWind wind;
    wind.strength = 1.0f;
    FlatBed bed;
    bed.y = -100.0f;
    const LeafField::Crown tree = crownAt(9, 0.0f, 0.0f, 40.0f);
    LeafField              field;
    LeafField::Step        step = makeStep(&tree, 1, &wind, &bed);
    step.dt                     = 0.4f;
    field.step(step);
    ASSERT_EQ(field.spawnedTotal(), 1u);

    for (int n = 0; n < 6; ++n)
    {
        step.dt = 0.05f;
        field.step(step);
    }
    EXPECT_EQ(field.spawnedTotal(), 1u);
    EXPECT_EQ(countPhase(field, tree.id, LeafPhase::Airborne), 1);

    step.crowns     = nullptr;
    step.crownCount = 0;
    step.dt         = 0.05f;
    field.step(step);
    EXPECT_EQ(field.spawnedTotal(), 1u);
    EXPECT_EQ(countPhase(field, tree.id, LeafPhase::Airborne), 1);

    step.crowns     = &tree;
    step.crownCount = 1;
    step.dt         = 0.1f;
    field.step(step);
    EXPECT_EQ(field.spawnedTotal(), 2u);
}

TEST(LeafField, RestedOnBed_CopiesCallbackNormal)
{
    FlatWind wind;
    FlatBed  bed;
    bed.y = 4.0f;
    TiltNormal tilt;
    tilt.nx = 0.5f;
    tilt.ny = 0.25f;
    tilt.nz = 0.75f;
    const LeafField::Crown tree = crownAt(1, 2.0f, 3.0f, 5.0f);
    LeafField              field;
    LeafField::Step        step = makeStep(&tree, 1, &wind, &bed);
    step.sampleNormal           = tiltNormal;
    step.normalUser             = &tilt;
    ASSERT_TRUE(waitForLiving(field, step, 5.0f));
    step.dt = 1.0f;
    field.step(step);

    LeafSlot slot{};
    ASSERT_TRUE(firstOccupied(field, slot));
    EXPECT_EQ(slot.phase, LeafPhase::Rest);
    EXPECT_FLOAT_EQ(slot.y, bed.y);
    EXPECT_FLOAT_EQ(slot.nx, tilt.nx);
    EXPECT_FLOAT_EQ(slot.ny, tilt.ny);
    EXPECT_FLOAT_EQ(slot.nz, tilt.nz);
}

TEST(LeafField, RestedOnStillWater_KeepsWorldUpNormal)
{
    FlatWind wind;
    FlatBed  bed;
    bed.y = 1.0f;
    TiltNormal tilt;
    tilt.nx = 0.5f;
    tilt.ny = 0.25f;
    tilt.nz = 0.75f;
    LeafField::Pond pond;
    pond.minX  = -5.0f;
    pond.minZ  = -5.0f;
    pond.maxX  = 5.0f;
    pond.maxZ  = 5.0f;
    pond.level = 4.0f;
    const LeafField::Crown tree = crownAt(1, 0.0f, 0.0f, 8.0f);
    LeafField              field;
    LeafField::Step        step = makeStep(&tree, 1, &wind, &bed);
    step.ponds                  = &pond;
    step.pondCount              = 1;
    step.sampleNormal           = tiltNormal;
    step.normalUser             = &tilt;
    ASSERT_TRUE(waitForLiving(field, step, 5.0f));
    step.dt = 1.0f;
    field.step(step);

    LeafSlot slot{};
    ASSERT_TRUE(firstOccupied(field, slot));
    EXPECT_EQ(slot.phase, LeafPhase::Rest);
    EXPECT_FLOAT_EQ(slot.y, pond.level);
    EXPECT_FLOAT_EQ(slot.nx, 0.0f);
    EXPECT_FLOAT_EQ(slot.ny, 1.0f);
    EXPECT_FLOAT_EQ(slot.nz, 0.0f);
}

TEST(LeafField, NullWind_MatchesForwardedSample)
{
    float bestStrength = -1.0f;
    float bestX        = 0.0f;
    float bestZ        = 0.0f;
    for (int z = 0; z < 8; ++z)
    {
        for (int x = 0; x < 8; ++x)
        {
            const float          wx     = static_cast<float>(x) * 40.0f;
            const float          wz     = static_cast<float>(z) * 35.0f;
            const LeafWindSample sample = sampleLeafWind(wx, wz, 1.0);
            if (sample.strength > bestStrength)
            {
                bestStrength = sample.strength;
                bestX        = wx;
                bestZ        = wz;
            }
        }
    }
    ASSERT_GT(bestStrength, 0.2f);

    FlatBed bed;
    bed.y = 0.0f;
    const LeafField::Crown tree = crownAt(1, bestX, bestZ, 12.0f);

    auto run = [&](bool forward, std::vector<Landing>& lands) {
        LeafField       field;
        LeafField::Step step = makeStep(&tree, 1, nullptr, &bed);
        step.wind            = forward ? forwardLeafWind : nullptr;
        step.windUser        = nullptr;
        step.cameraX         = tree.x;
        step.cameraZ         = tree.z;
        float t              = 0.0f;
        while (t < 4.0f)
        {
            step.dt = 0.05f;
            step.simTime += 0.05;
            field.step(step);
            collectLandings(field, lands);
            t += 0.05f;
        }
        return field.spawnedTotal();
    };

    std::vector<Landing> nullLands;
    std::vector<Landing> hookLands;
    const uint32_t       nullSpawned = run(false, nullLands);
    const uint32_t       hookSpawned = run(true, hookLands);
    EXPECT_EQ(nullSpawned, hookSpawned);
    EXPECT_GT(nullSpawned, 0u);
    ASSERT_FALSE(nullLands.empty());
    ASSERT_EQ(nullLands.size(), hookLands.size());
    const float dx = nullLands[0].x - tree.x;
    const float dz = nullLands[0].z - tree.z;
    EXPECT_GT(dx * dx + dz * dz, 0.2f * 0.2f);
    for (size_t i = 0; i < nullLands.size(); ++i)
    {
        EXPECT_EQ(signOf(nullLands[i].x - tree.x), signOf(hookLands[i].x - tree.x));
        EXPECT_EQ(signOf(nullLands[i].z - tree.z), signOf(hookLands[i].z - tree.z));
    }
}

TEST(LeafField, LakeAboveBed_RestsOnTheBed)
{
    FlatWind wind;
    FlatBed  bed;
    bed.y = 4.0f;
    const LeafField::Crown tree = crownAt(1, 0.0f, 0.0f, 8.0f);
    LeafField              field;
    LeafField::Step        step = makeStep(&tree, 1, &wind, &bed);
    step.lake                   = true;
    step.lakeLevel              = 1.0f;
    ASSERT_TRUE(waitForLiving(field, step, 5.0f));
    step.dt = 1.0f;
    field.step(step);

    LeafSlot slot{};
    ASSERT_TRUE(firstOccupied(field, slot));
    EXPECT_EQ(slot.phase, LeafPhase::Rest);
    EXPECT_FLOAT_EQ(slot.y, bed.y);
    EXPECT_NE(slot.y, step.lakeLevel);
}

TEST(LeafField, PondMiss_RestsOnTheBed)
{
    FlatWind wind;
    FlatBed  bed;
    bed.y = 3.0f;
    LeafField::Pond pond;
    pond.minX  = 40.0f;
    pond.minZ  = 40.0f;
    pond.maxX  = 50.0f;
    pond.maxZ  = 50.0f;
    pond.level = 9.0f;
    const LeafField::Crown tree = crownAt(1, 0.0f, 0.0f, 8.0f);
    LeafField              field;
    LeafField::Step        step = makeStep(&tree, 1, &wind, &bed);
    step.ponds                  = &pond;
    step.pondCount              = 1;
    ASSERT_TRUE(waitForLiving(field, step, 5.0f));
    step.dt = 1.0f;
    field.step(step);

    LeafSlot slot{};
    ASSERT_TRUE(firstOccupied(field, slot));
    EXPECT_EQ(slot.phase, LeafPhase::Rest);
    EXPECT_FLOAT_EQ(slot.y, bed.y);
    EXPECT_NE(slot.y, pond.level);
}

TEST(LeafField, HalfShedScale_SpawnsFewerLeavesThanFull)
{
    FlatWind wind;
    wind.strength = 1.0f;
    FlatBed bed;
    bed.y = -100.0f;
    const LeafField::Crown tree = crownAt(1, 0.0f, 0.0f, 16.0f);

    auto spawnedAt = [&](float scale) {
        LeafField       field;
        field.setShedScale(scale);
        LeafField::Step step = makeStep(&tree, 1, &wind, &bed);
        stepFor(field, step, 4.0f);
        return field.spawnedTotal();
    };

    const uint32_t full = spawnedAt(1.0f);
    const uint32_t half = spawnedAt(0.5f);
    EXPECT_GT(half, 0u);
    EXPECT_LT(half, full);
}
