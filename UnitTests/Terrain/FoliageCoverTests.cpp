#include <gtest/gtest.h>

#include "Terrain/FoliageCover.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace Dark::Terrain;

namespace
{
    class TempDir
    {
    public:
        explicit TempDir(const char* name)
        {
            std::error_code ec;
            const auto root = std::filesystem::temp_directory_path(ec);
            if (ec)
                return;
            path = root / "darkengine6_cover_ut" / name;
            std::filesystem::remove_all(path, ec);
            ec.clear();
            std::filesystem::create_directories(path, ec);
            if (ec)
                path.clear();
        }

        ~TempDir()
        {
            if (path.empty())
                return;
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }

        TempDir(const TempDir&)            = delete;
        TempDir& operator=(const TempDir&) = delete;

        std::filesystem::path path;
    };

    bool writeFile(const std::filesystem::path& path, const void* data, size_t size)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        if (size > 0)
            out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        out.flush();
        return static_cast<bool>(out);
    }

    std::vector<uint8_t> readAll(const std::filesystem::path& path)
    {
        std::error_code ec;
        const auto n = std::filesystem::file_size(path, ec);
        if (ec || n > 1024u * 1024u)
            return {};
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return {};
        std::vector<uint8_t> buf(static_cast<size_t>(n));
        if (n > 0)
            in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(n));
        if (!in)
            return {};
        return buf;
    }

    void stain(FoliageCover& cover)
    {
        cover.stampDisk(4.5f, 4.5f, 2.0f, 0, 0, false);
    }
} // namespace

TEST(FoliageCover, CoverRoundTrip)
{
    FoliageCover src;
    ASSERT_TRUE(src.create(16.0f, 16.0f, 0.0f, 0.0f));
    EXPECT_EQ(src.tilesX(), 1);
    EXPECT_EQ(src.cellsX(), 16);
    EXPECT_EQ(src.cellsZ(), 16);
    EXPECT_FLOAT_EQ(src.cellSize(), 1.0f);
    src.stampDisk(3.2f, 4.2f, 1.5f, 0, 0, true);
    src.stampSoftAnnulus(10.0f, 10.0f, 1.0f, 3.0f, 0, 200, 0, 64, true);
    EXPECT_EQ(src.sampleNearest(3.2f, 4.2f).coverage, 0);
    EXPECT_EQ(src.sampleNearest(3.2f, 4.2f).heightScale, 0);

    TempDir dir("round");
    ASSERT_FALSE(dir.path.empty());
    const auto path = dir.path / "t_00_00.cover.bin";
    ASSERT_TRUE(src.save(path, 0, 0));

    std::vector<uint8_t> bytes = readAll(path);
    ASSERT_EQ(bytes.size(), 40u + 16u * 16u * 2u);
    float savedCell = 0.0f;
    std::memcpy(&savedCell, bytes.data() + 24, sizeof(savedCell));
    EXPECT_FLOAT_EQ(savedCell, 1.0f);

    FoliageCover dst;
    ASSERT_TRUE(dst.create(16.0f, 16.0f, 0.0f, 0.0f));
    ASSERT_TRUE(dst.load(path, 0, 0));
    for (int z = 0; z < 16; ++z)
    {
        for (int x = 0; x < 16; ++x)
        {
            const float wx = static_cast<float>(x) + 0.5f;
            const float wz = static_cast<float>(z) + 0.5f;
            const CoverSample a = src.sampleNearest(wx, wz);
            const CoverSample b = dst.sampleNearest(wx, wz);
            EXPECT_EQ(a.coverage, b.coverage) << x << "," << z;
            EXPECT_EQ(a.heightScale, b.heightScale) << x << "," << z;
        }
    }

    bytes[0] ^= 0xFFu;
    ASSERT_TRUE(writeFile(path, bytes.data(), bytes.size()));
    stain(dst);
    EXPECT_EQ(dst.sampleNearest(4.5f, 4.5f).coverage, 0);
    EXPECT_FALSE(dst.load(path, 0, 0));
    EXPECT_TRUE(dst.valid());
    EXPECT_EQ(dst.cellsX(), 16);
    EXPECT_EQ(dst.sampleNearest(4.5f, 4.5f).coverage, 255);
    EXPECT_EQ(dst.sampleNearest(4.5f, 4.5f).heightScale, 255);

    ASSERT_TRUE(src.save(path, 0, 0));
    bytes = readAll(path);
    ASSERT_GT(bytes.size(), 50u);
    ASSERT_TRUE(writeFile(path, bytes.data(), 50u));
    stain(dst);
    EXPECT_FALSE(dst.load(path, 0, 0));
    EXPECT_TRUE(dst.valid());
    EXPECT_EQ(dst.sampleNearest(4.5f, 4.5f).coverage, 255);
    EXPECT_EQ(dst.sampleNearest(4.5f, 4.5f).heightScale, 255);

    ASSERT_TRUE(src.save(path, 0, 0));
    bytes = readAll(path);
    bytes[8] = 1;
    ASSERT_TRUE(writeFile(path, bytes.data(), bytes.size()));
    stain(dst);
    EXPECT_FALSE(dst.load(path, 0, 0));
    EXPECT_TRUE(dst.valid());
    EXPECT_EQ(dst.sampleNearest(4.5f, 4.5f).coverage, 255);
    EXPECT_EQ(dst.sampleNearest(4.5f, 4.5f).heightScale, 255);

    ASSERT_TRUE(src.save(path, 0, 0));
    bytes = readAll(path);
    const float wrongCell = 2.0f;
    std::memcpy(bytes.data() + 24, &wrongCell, sizeof(wrongCell));
    ASSERT_TRUE(writeFile(path, bytes.data(), bytes.size()));
    stain(dst);
    EXPECT_FALSE(dst.load(path, 0, 0));
    EXPECT_TRUE(dst.valid());
    EXPECT_EQ(dst.cellsX(), 16);
    EXPECT_EQ(dst.sampleNearest(4.5f, 4.5f).coverage, 255);
    EXPECT_EQ(dst.sampleNearest(4.5f, 4.5f).heightScale, 255);
    EXPECT_EQ(dst.sampleNearest(1.5f, 1.5f).coverage, 255);

    ASSERT_TRUE(src.save(path, 0, 0));
    ASSERT_TRUE(dst.load(path, 0, 0));
    EXPECT_EQ(dst.sampleNearest(3.2f, 4.2f).coverage, 0);
    EXPECT_EQ(dst.sampleNearest(14.5f, 14.5f).coverage, 255);
}

TEST(FoliageCover, InteriorPointReadsBare)
{
    FoliageCover cover;
    ASSERT_TRUE(cover.create(16.0f, 16.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(cover.cellSize(), kCoverCellSizeM);
    cover.stampDisk(8.0f, 8.0f, 1.25f, 0, 0, true);
    EXPECT_EQ(cover.sampleNearest(8.0f, 8.0f).coverage, 0);
    EXPECT_EQ(cover.sampleNearest(8.0f, 8.0f).heightScale, 0);
    EXPECT_EQ(cover.sampleNearest(10.5f, 8.5f).coverage, 255);
    EXPECT_EQ(cover.sampleNearest(10.5f, 8.5f).heightScale, 255);

    cover.stampDisk(8.0f, 8.0f, 1.25f, 255, 200, true);
    EXPECT_EQ(cover.sampleNearest(8.0f, 8.0f).coverage, 0);
    EXPECT_EQ(cover.sampleNearest(8.0f, 8.0f).heightScale, 0);

    EXPECT_EQ(cover.sampleNearest(-0.1f, 1.0f).coverage, 255);
    EXPECT_EQ(cover.sampleNearest(16.0f, 1.0f).coverage, 255);
    EXPECT_EQ(cover.sampleBilinear(16.0f, 1.0f).heightScale, 255);

    FoliageCover bad;
    EXPECT_FALSE(bad.create(0.0f, 16.0f, 0.0f, 0.0f));
    EXPECT_FALSE(bad.valid());
    EXPECT_EQ(bad.sampleNearest(1.0f, 1.0f).coverage, 255);
    EXPECT_EQ(bad.sampleNearest(1.0f, 1.0f).heightScale, 255);
}

TEST(FoliageCover, StampCrossesTileBoundary)
{
    FoliageCover cover;
    ASSERT_TRUE(cover.create(600.0f, 600.0f, 0.0f, 0.0f));
    EXPECT_EQ(cover.cellsX(), 600);
    EXPECT_EQ(cover.cellsZ(), 600);
    EXPECT_EQ(cover.tilesX(), 2);
    EXPECT_EQ(cover.tilesZ(), 2);
    EXPECT_FLOAT_EQ(cover.cellSize(), 1.0f);

    cover.stampDisk(512.0f, 10.0f, 2.0f, 0, 0, true);
    EXPECT_EQ(cover.sampleNearest(511.5f, 10.25f).coverage, 0);
    EXPECT_EQ(cover.sampleNearest(511.5f, 10.25f).heightScale, 0);
    EXPECT_EQ(cover.sampleNearest(513.25f, 10.25f).coverage, 0);
    EXPECT_EQ(cover.sampleNearest(516.5f, 10.25f).coverage, 255);
    EXPECT_EQ(cover.sampleNearest(516.5f, 10.25f).heightScale, 255);
    EXPECT_EQ(cover.sampleNearest(590.5f, 590.5f).coverage, 255);
}
