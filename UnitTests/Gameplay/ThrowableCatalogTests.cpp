#include <gtest/gtest.h>

#include "Core/ContentRoots.h"
#include "Gameplay/ThrowableCatalog.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

using namespace Dark;

namespace
{

    constexpr float kBuiltinRockWeight          = 2.0f;
    constexpr float kBuiltinRockThrowDistance   = 10.0f;
    constexpr float kBuiltinRockCallDistance    = 6.0f;
    constexpr float kBuiltinBottleWeight        = 0.5f;
    constexpr float kBuiltinBottleThrowDistance = 11.0f;
    constexpr float kBuiltinBottleCallDistance  = 16.0f;

    constexpr float kSampleRockWeight          = 0.75f;
    constexpr float kSampleRockThrowDistance   = 12.0f;
    constexpr float kSampleRockCallDistance    = 8.0f;
    constexpr float kSampleBottleWeight        = 0.5f;
    constexpr float kSampleBottleThrowDistance = 14.0f;
    constexpr float kSampleBottleCallDistance  = 22.0f;

    constexpr std::string_view kRockModel    = "models/NaturePack/glTF/Rock_Medium_1.gltf";
    constexpr std::string_view kBottleModel  = "models/unit_glass.gltf";
    constexpr std::string_view kRockImpact   = "audio/foot_rock.wav";
    constexpr std::string_view kBottleImpact = "audio/splash.wav";

    constexpr std::string_view kDistinct = R"({
  "version": 1,
  "throwables": [
    {
      "id": "rock",
      "weight": 3,
      "throwDistance": 18,
      "impactSound": "audio/place.wav",
      "callDistance": 7,
      "model": "models/unit_cube.gltf"
    },
    {
      "id": "bottle",
      "weight": 1,
      "throwDistance": 21,
      "impactSound": "audio/coin.wav",
      "callDistance": 33,
      "model": "models/unit_glass.gltf"
    }
  ]
})";

    constexpr std::string_view kMissingThrowDistance = R"({
  "version": 1,
  "throwables": [
    {
      "id": "rock",
      "weight": 9,
      "throwDistance": 1,
      "impactSound": "audio/foot_rock.wav",
      "callDistance": 1,
      "model": "models/NaturePack/glTF/Rock_Medium_1.gltf"
    },
    {
      "id": "bottle",
      "weight": 1,
      "impactSound": "audio/splash.wav",
      "callDistance": 50,
      "model": "models/unit_glass.gltf"
    }
  ]
})";

    struct FileSlot
    {
        std::filesystem::path original;
        std::filesystem::path backup;
        bool                  hidden   = false;
        bool                  replaced = false;
    };

    class ContentGuard
    {
    public:
        ~ContentGuard()
        {
            restoreAll();
        }

        ContentGuard(const ContentGuard&)            = delete;
        ContentGuard& operator=(const ContentGuard&) = delete;
        ContentGuard()                               = default;

        bool hideAll(const char* fileName)
        {
            bool any = false;
            for (const std::filesystem::path& root : contentRootCandidates())
            {
                if (root.empty())
                    continue;
                const std::filesystem::path path = root / "items" / fileName;
                std::error_code             ec;
                if (!std::filesystem::is_regular_file(path, ec) || ec)
                    continue;
                FileSlot slot;
                slot.original = path;
                slot.backup   = path;
                slot.backup += ".throwablecatalogbak";
                std::filesystem::remove(slot.backup, ec);
                std::filesystem::rename(slot.original, slot.backup, ec);
                if (ec)
                    return false;
                slot.hidden = true;
                m_slots.push_back(std::move(slot));
                any = true;
            }
            return any;
        }

        bool replaceFirst(const char* fileName, std::string_view text)
        {
            for (const std::filesystem::path& root : contentRootCandidates())
            {
                if (root.empty())
                    continue;
                const std::filesystem::path path = root / "items" / fileName;
                std::error_code             ec;
                if (!std::filesystem::is_regular_file(path, ec) || ec)
                    continue;
                FileSlot slot;
                slot.original = path;
                slot.backup   = path;
                slot.backup += ".throwablecatalogbak";
                std::filesystem::remove(slot.backup, ec);
                std::filesystem::rename(slot.original, slot.backup, ec);
                if (ec)
                    return false;
                slot.hidden = true;
                if (!writeFile(slot.original, text))
                {
                    std::filesystem::rename(slot.backup, slot.original, ec);
                    return false;
                }
                slot.replaced = true;
                m_slots.push_back(std::move(slot));
                return true;
            }
            return false;
        }

        bool writeOverFirstHidden(const char* fileName, std::string_view text)
        {
            for (FileSlot& slot : m_slots)
            {
                if (!slot.hidden || slot.original.filename() != fileName)
                    continue;
                if (!writeFile(slot.original, text))
                    return false;
                slot.replaced = true;
                return true;
            }
            return false;
        }

        void restoreAll()
        {
            std::error_code ec;
            for (auto it = m_slots.rbegin(); it != m_slots.rend(); ++it)
            {
                if (it->replaced || std::filesystem::exists(it->original, ec))
                    std::filesystem::remove(it->original, ec);
                if (it->hidden)
                    std::filesystem::rename(it->backup, it->original, ec);
            }
            m_slots.clear();
        }

    private:
        static bool writeFile(const std::filesystem::path& path, std::string_view text)
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            return static_cast<bool>(out);
        }

        std::vector<FileSlot> m_slots;
    };

    void expectDef(const ThrowableDef& def, std::string_view id, float weight, float throwDistance, std::string_view impactSound, float callDistance, std::string_view model)
    {
        EXPECT_EQ(def.id, id);
        EXPECT_FLOAT_EQ(def.weight, weight);
        EXPECT_FLOAT_EQ(def.throwDistance, throwDistance);
        EXPECT_EQ(def.impactSound, impactSound);
        EXPECT_FLOAT_EQ(def.callDistance, callDistance);
        EXPECT_EQ(def.model, model);
    }

    void expectBuiltin(const ThrowableCatalog& cat)
    {
        const ThrowableDef* rock   = cat.find("rock");
        const ThrowableDef* bottle = cat.find("bottle");
        ASSERT_NE(rock, nullptr);
        ASSERT_NE(bottle, nullptr);
        expectDef(*rock, "rock", kBuiltinRockWeight, kBuiltinRockThrowDistance, kRockImpact, kBuiltinRockCallDistance, kRockModel);
        expectDef(*bottle, "bottle", kBuiltinBottleWeight, kBuiltinBottleThrowDistance, kBottleImpact, kBuiltinBottleCallDistance, kBottleModel);
        EXPECT_EQ(cat.items().size(), 2u);
    }

    void expectDistinct(const ThrowableCatalog& cat)
    {
        const ThrowableDef* rock   = cat.find("rock");
        const ThrowableDef* bottle = cat.find("bottle");
        ASSERT_NE(rock, nullptr);
        ASSERT_NE(bottle, nullptr);
        expectDef(*rock, "rock", 3.0f, 18.0f, "audio/place.wav", 7.0f, "models/unit_cube.gltf");
        expectDef(*bottle, "bottle", 1.0f, 21.0f, "audio/coin.wav", 33.0f, "models/unit_glass.gltf");
    }

} // namespace

TEST(ThrowableCatalog, FileLoadsWeightDistanceSoundCallAndModel)
{
    ContentGuard guard;
    ASSERT_TRUE(guard.replaceFirst("throwables.json", kDistinct));

    ThrowableCatalog cat;
    ASSERT_TRUE(cat.loadFromContent());
    expectDistinct(cat);
}

TEST(ThrowableCatalog, MissingFileKeepsBuiltinRockAndBottle)
{
    ContentGuard guard;
    ASSERT_TRUE(guard.hideAll("throwables.json"));

    ThrowableCatalog cat;
    ASSERT_TRUE(cat.loadFromContent());
    expectBuiltin(cat);

    ASSERT_TRUE(guard.writeOverFirstHidden("throwables.json", kDistinct));
    EXPECT_TRUE(cat.loadFromContent());
    expectBuiltin(cat);
}

TEST(ThrowableCatalog, BrokenFileKeepsCatalogAlreadyLoaded)
{
    {
        ContentGuard guard;
        ASSERT_TRUE(guard.replaceFirst("throwables.json", "{"));

        ThrowableCatalog cat;
        EXPECT_FALSE(cat.loadFromContent());
        expectBuiltin(cat);

        guard.restoreAll();
        EXPECT_FALSE(cat.loadFromContent());
        expectBuiltin(cat);
    }

    ThrowableCatalog loaded;
    ASSERT_TRUE(loaded.parse(kDistinct));
    expectDistinct(loaded);

    ContentGuard guard;
    ASSERT_TRUE(guard.replaceFirst("throwables.json", "{"));
    EXPECT_FALSE(loaded.loadFromContent());
    expectDistinct(loaded);

    guard.restoreAll();
    EXPECT_FALSE(loaded.loadFromContent());
    expectDistinct(loaded);
}

TEST(ThrowableCatalog, MissingThrowDistanceDoesNotReplaceCatalog)
{
    ThrowableCatalog cat;
    ASSERT_TRUE(cat.parse(kDistinct));
    expectDistinct(cat);

    EXPECT_FALSE(cat.parse(kMissingThrowDistance));
    expectDistinct(cat);

    ContentGuard guard;
    ASSERT_TRUE(guard.replaceFirst("throwables.json", kMissingThrowDistance));
    EXPECT_FALSE(cat.loadFromContent());
    expectDistinct(cat);
}

TEST(ThrowableCatalog, BadJsonDoesNotReplaceCatalog)
{
    ThrowableCatalog cat;
    ASSERT_TRUE(cat.parse(kDistinct));

    EXPECT_FALSE(cat.parse("{"));
    EXPECT_FALSE(cat.parse(""));
    EXPECT_FALSE(cat.parse("[]"));
    EXPECT_FALSE(cat.parse("null"));
    EXPECT_FALSE(cat.parse(R"({ "version": 1 })"));
    EXPECT_FALSE(cat.parse(R"({ "version": 1, "throwables": [] })"));
    EXPECT_FALSE(cat.parse(R"({ "version": 2, "throwables": [] })"));
    EXPECT_FALSE(cat.parse(R"({
      "version": 1,
      "throwables": [
        {
          "id": "rock",
          "weight": 3,
          "throwDistance": 18,
          "callDistance": 7,
          "model": "models/unit_cube.gltf"
        }
      ]
    })"));
    expectDistinct(cat);

    ContentGuard guard;
    ASSERT_TRUE(guard.replaceFirst("throwables.json", "{ \"version\": 1, \"throwables\": ["));
    EXPECT_FALSE(cat.loadFromContent());
    expectDistinct(cat);
}

TEST(ThrowableCatalog, SampleBottleCallsFartherAndWeighsLess)
{
    ThrowableCatalog cat;
    ASSERT_TRUE(cat.loadFromContent());

    const ThrowableDef* rock   = cat.find("rock");
    const ThrowableDef* bottle = cat.find("bottle");
    ASSERT_NE(rock, nullptr);
    ASSERT_NE(bottle, nullptr);

    expectDef(*rock, "rock", kSampleRockWeight, kSampleRockThrowDistance, kRockImpact, kSampleRockCallDistance, kRockModel);
    expectDef(*bottle, "bottle", kSampleBottleWeight, kSampleBottleThrowDistance, kBottleImpact, kSampleBottleCallDistance, kBottleModel);
    EXPECT_GT(bottle->callDistance, rock->callDistance);
    EXPECT_LT(bottle->weight, rock->weight);
    EXPECT_NE(rock->impactSound, bottle->impactSound);
}
