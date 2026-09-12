#include <gtest/gtest.h>

#include "Assets/Image.h"
#include "Assets/ImageCache.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace Dark;

TEST(TextureCache, NormalizePathIsCaseInsensitiveAndStable)
{
    const std::string a = ImageCache::normalizePath("C:/Game/Content/Textures/Foo.PNG");
    const std::string b = ImageCache::normalizePath("c:/game/content/textures/foo.png");
    EXPECT_FALSE(a.empty());
    EXPECT_EQ(a, b);
    EXPECT_EQ(ImageCache::fileKey("C:/x/Y.png"), ImageCache::fileKey("c:/x/y.PNG"));
}

TEST(TextureCache, SolidAndCircleKeys)
{
    EXPECT_EQ(ImageCache::solidKey(1, 2, 3, 4), "s:1,2,3,4");
    EXPECT_NE(ImageCache::solidKey(1, 2, 3, 4), ImageCache::solidKey(1, 2, 3, 5));
    EXPECT_EQ(ImageCache::softCircleKey(64), "c:64");
    EXPECT_EQ(ImageCache::gltfKey("models/foo.gltf", 2), "gltf:models/foo.gltf:2");
}

TEST(TextureCache, ConcurrentDecodeSingleFlight)
{
    ImageCache       cache;
    std::atomic<int> creates{ 0 };

    auto worker = [&]()
    {
        cache.decodeOnce("unit:race",
                         [&creates]() -> AssetRef<Image>
                         {
                             ++creates;
                             std::this_thread::sleep_for(std::chrono::milliseconds(30));
                             auto img = std::make_shared<Image>();
                             img->createSolidColor(1, 2, 3, 4);
                             return img;
                         });
    };

    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int i = 0; i < 8; ++i)
        threads.emplace_back(worker);
    for (auto& t : threads)
        t.join();

    EXPECT_EQ(creates.load(), 1);
}
