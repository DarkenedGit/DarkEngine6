#include <gtest/gtest.h>
#include "Core/UUID.h"
#include <unordered_set>

using namespace Dark;

TEST(UUID, ExplicitValue)
{
    UUID a{42ull};
    UUID b{42ull};
    UUID c{7ull};

    EXPECT_EQ(static_cast<uint64_t>(a), 42ull);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(UUID, DefaultIsNonZeroRandom)
{
    UUID a;
    UUID b;
    // Extremely unlikely both are zero or equal if RNG is working.
    EXPECT_NE(static_cast<uint64_t>(a), 0ull);
    EXPECT_NE(static_cast<uint64_t>(b), 0ull);
    EXPECT_NE(a, b);
}

TEST(UUID, HashUsableInUnorderedSet)
{
    std::unordered_set<UUID> set;
    set.insert(UUID{1ull});
    set.insert(UUID{2ull});
    set.insert(UUID{1ull});
    EXPECT_EQ(set.size(), 2u);
    EXPECT_TRUE(set.contains(UUID{1ull}));
}
