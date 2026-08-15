#include <gtest/gtest.h>

#include "Assets/TextureCache.h"
#include "Render/Texture2D.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace Dark;

TEST(TextureCache, NormalizePathIsCaseInsensitiveAndStable)
{
    const std::string a = TextureCache::normalizePath("C:/Game/Content/Textures/Foo.PNG");
    const std::string b = TextureCache::normalizePath("c:/game/content/textures/foo.png");
    EXPECT_FALSE(a.empty());
    EXPECT_EQ(a, b);
    EXPECT_EQ(TextureCache::fileKey("C:/x/Y.png"), TextureCache::fileKey("c:/x/y.PNG"));
}

TEST(TextureCache, SolidAndCircleKeys)
{
    EXPECT_EQ(TextureCache::solidKey(1, 2, 3, 4), "s:1,2,3,4");
    EXPECT_NE(TextureCache::solidKey(1, 2, 3, 4), TextureCache::solidKey(1, 2, 3, 5));
    EXPECT_EQ(TextureCache::softCircleKey(64), "c:64");
}

TEST(TextureCache, SameKeyReturnsSameInstance)
{
    TextureCache cache;
    int          creates = 0;
    auto         make    = [&creates]()
    {
        ++creates;
        return std::make_shared<Texture2D>();
    };

    const auto a = cache.getOrCreate("unit:a", make);
    const auto b = cache.getOrCreate("unit:a", make);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_EQ(a.get(), b.get());
    EXPECT_EQ(creates, 1);
    EXPECT_EQ(cache.size(), 1u);
}

TEST(TextureCache, DifferentKeysAreDistinct)
{
    TextureCache cache;
    const auto   a = cache.getOrCreate("unit:a", []() { return std::make_shared<Texture2D>(); });
    const auto   b = cache.getOrCreate("unit:b", []() { return std::make_shared<Texture2D>(); });
    ASSERT_TRUE(a && b);
    EXPECT_NE(a.get(), b.get());
    EXPECT_EQ(cache.size(), 2u);
}

TEST(TextureCache, NullFactoryIsNotCached)
{
    TextureCache cache;
    int          creates = 0;
    auto         fail    = [&creates]() -> std::shared_ptr<Texture2D>
    {
        ++creates;
        return {};
    };

    EXPECT_FALSE(cache.getOrCreate("unit:miss", fail));
    EXPECT_FALSE(cache.getOrCreate("unit:miss", fail));
    EXPECT_EQ(creates, 2);
    EXPECT_EQ(cache.size(), 0u);
}

TEST(TextureCache, CollectUnusedDropsOrphans)
{
    TextureCache cache;
    auto         tex = cache.getOrCreate("unit:gc", []() { return std::make_shared<Texture2D>(); });
    ASSERT_EQ(cache.size(), 1u);
    tex.reset();
    cache.collectUnused();
    EXPECT_EQ(cache.size(), 0u);
}

TEST(TextureCache, ConcurrentLoadsSingleFlight)
{
    TextureCache     cache;
    std::atomic<int> creates{ 0 };

    auto worker = [&]()
    {
        cache.getOrCreate("unit:race",
                          [&creates]()
                          {
                              ++creates;
                              std::this_thread::sleep_for(std::chrono::milliseconds(30));
                              return std::make_shared<Texture2D>();
                          });
    };

    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int i = 0; i < 8; ++i)
        threads.emplace_back(worker);
    for (auto& t : threads)
        t.join();

    EXPECT_EQ(creates.load(), 1);
    EXPECT_EQ(cache.size(), 1u);
    const auto pinned = cache.find("unit:race");
    ASSERT_TRUE(pinned);
    EXPECT_GE(pinned.use_count(), 2); // cache + local
}
