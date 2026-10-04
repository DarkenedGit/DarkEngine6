#include <gtest/gtest.h>

#include "Physics/FoliageCollision.h"

#include <cstdint>
#include <vector>

using namespace Dark::Physics;
using namespace Dark::Terrain;

namespace
{
    FoliageRecord makeRec(FoliageKind kind, float x, float z)
    {
        FoliageRecord rec{};
        rec.kind = static_cast<uint8_t>(kind);
        rec.x    = x;
        rec.z    = z;
        rec.scale = 1.0f;
        return rec;
    }
}

TEST(FoliageCollision, SelectsTreesAndRocksInsideRadius)
{
    const FoliageRecord recs[] = {
        makeRec(FoliageKind::Flower, 1.0f, 0.0f),
        makeRec(FoliageKind::Tree, 4.0f, 0.0f),
        makeRec(FoliageKind::Rock, 0.0f, 10.0f),
        makeRec(FoliageKind::Tree, 70.0f, 0.0f),
    };

    uint32_t indices[4]{};
    uint32_t qualified = 0;
    const uint32_t kept = selectFoliageBodies(recs, 4, 0.0f, 0.0f, indices, 4, &qualified);

    EXPECT_EQ(qualified, 2u);
    ASSERT_EQ(kept, 2u);
    EXPECT_EQ(indices[0], 1u);
    EXPECT_EQ(indices[1], 2u);
}

TEST(FoliageCollision, KeepsNearestWhenOverCap)
{
    constexpr uint32_t kExtra = 16u;
    constexpr uint32_t kCandidates = 2048u + kExtra;
    std::vector<FoliageRecord> recs(kCandidates);
    for (uint32_t i = 0; i < kCandidates; ++i)
    {
        recs[i] = makeRec(FoliageKind::Tree, 0.01f * static_cast<float>(kCandidates - i), 0.0f);
    }
    recs[kCandidates - 1].kind = static_cast<uint8_t>(FoliageKind::Rock);
    recs.push_back(makeRec(FoliageKind::Flower, 0.0f, 0.0f));
    recs.push_back(makeRec(FoliageKind::Tree, 80.0f, 0.0f));

    std::vector<uint32_t> indices(3000u);
    uint32_t qualified = 99u;
    const uint32_t kept = selectFoliageBodies(
        recs.data(),
        static_cast<uint32_t>(recs.size()),
        0.0f,
        0.0f,
        indices.data(),
        static_cast<uint32_t>(indices.size()),
        &qualified);

    EXPECT_EQ(qualified, kCandidates);
    ASSERT_EQ(kept, 2048u);

    uint32_t mismatch = kept;
    for (uint32_t i = 0; i < kept; ++i)
    {
        if (indices[i] != (kCandidates - 1u - i))
        {
            mismatch = i;
            break;
        }
    }
    EXPECT_EQ(mismatch, kept);
}
