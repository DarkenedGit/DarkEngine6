#include <gtest/gtest.h>

#include "Core/Log.h"
#include "Terrain/FoliageFile.h"
#include "Terrain/TerrainTileFile.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
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
            path = root / "darkengine6_foliage_ut" / name;
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

    std::vector<std::string>* g_logs = nullptr;

    void captureLog(Dark::LogLevel, Dark::LogCategory, const char* message)
    {
        if (g_logs != nullptr && message != nullptr)
            g_logs->push_back(message);
    }

    class LogCapture
    {
    public:
        explicit LogCapture(std::vector<std::string>& out) :
            m_out(out)
        {
            m_out.clear();
            g_logs = &m_out;
            Dark::Log::setCapture(&captureLog);
        }

        ~LogCapture()
        {
            Dark::Log::setCapture(nullptr);
            g_logs = nullptr;
        }

        LogCapture(const LogCapture&)            = delete;
        LogCapture& operator=(const LogCapture&) = delete;

    private:
        std::vector<std::string>& m_out;
    };

    void storeU32(uint8_t* dst, uint32_t v)
    {
        dst[0] = static_cast<uint8_t>(v & 0xFFu);
        dst[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
        dst[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
        dst[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    }

    void fillHeader(uint8_t* dst, uint32_t magic, uint32_t version, int tileX, int tileZ, uint32_t count)
    {
        storeU32(dst + 0, magic);
        storeU32(dst + 4, version);
        storeU32(dst + 8, static_cast<uint32_t>(tileX));
        storeU32(dst + 12, static_cast<uint32_t>(tileZ));
        storeU32(dst + 16, count);
        storeU32(dst + 20, 0);
        storeU32(dst + 24, 0);
        storeU32(dst + 28, 0);
    }

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

    void expectLoadFails(const std::filesystem::path& path, int tileX, int tileZ)
    {
        std::vector<FoliageRecord> out(1);
        EXPECT_FALSE(loadFoliageTile(path, tileX, tileZ, out));
        EXPECT_TRUE(out.empty());
    }

    bool logHas(const std::vector<std::string>& logs, const char* token)
    {
        if (logs.empty() || token == nullptr)
            return false;
        return logs.back().find(token) != std::string::npos;
    }

} // namespace

TEST(FoliageFile, RoundTrip_TreeFlowerRock)
{
    TempDir dir("round_trip");
    ASSERT_FALSE(dir.path.empty());
    const auto path = dir.path / "t_04_05.foliage.bin";

    FoliageRecord recs[3]{};
    recs[0].x       = 1.25f;
    recs[0].y       = 2.5f;
    recs[0].z       = 3.75f;
    recs[0].yaw     = 0.5f;
    recs[0].scale   = 0.85f;
    recs[0].pitch   = 0.125f;
    recs[0].tiltYaw = 0.25f;
    recs[0].kind    = static_cast<uint8_t>(FoliageKind::Tree);
    recs[0].pad[0]  = 0xAB;
    recs[0].pad[1]  = 0xCD;
    recs[0].pad[2]  = 0xEF;

    recs[1].x       = 10.0f;
    recs[1].y       = 20.0f;
    recs[1].z       = 30.0f;
    recs[1].yaw     = 1.5f;
    recs[1].scale   = 1.2f;
    recs[1].pitch   = 0.3f;
    recs[1].tiltYaw = 0.4f;
    recs[1].kind    = static_cast<uint8_t>(FoliageKind::Flower);
    recs[1].pad[0]  = 1;
    recs[1].pad[1]  = 2;
    recs[1].pad[2]  = 3;

    recs[2].x       = -4.0f;
    recs[2].y       = 8.0f;
    recs[2].z       = 16.0f;
    recs[2].yaw     = 2.25f;
    recs[2].scale   = 1.8f;
    recs[2].pitch   = 0.7f;
    recs[2].tiltYaw = -0.2f;
    recs[2].kind    = static_cast<uint8_t>(FoliageKind::Rock);
    recs[2].pad[0]  = 9;
    recs[2].pad[1]  = 8;
    recs[2].pad[2]  = 7;

    ASSERT_TRUE(saveFoliageTile(path, 4, 5, recs, 3));

    const std::vector<uint8_t> raw = readAll(path);
    ASSERT_EQ(raw.size(), 32u + 3u * 32u);
    EXPECT_EQ(raw[0], static_cast<uint8_t>('D'));
    EXPECT_EQ(raw[1], static_cast<uint8_t>('E'));
    EXPECT_EQ(raw[2], static_cast<uint8_t>('F'));
    EXPECT_EQ(raw[3], static_cast<uint8_t>('L'));
    EXPECT_EQ(raw[4], 1);
    EXPECT_EQ(raw[8], 4);
    EXPECT_EQ(raw[12], 5);
    EXPECT_EQ(raw[16], 3);
    for (int i = 20; i < 32; ++i)
        EXPECT_EQ(raw[static_cast<size_t>(i)], 0);
    for (int rec = 0; rec < 3; ++rec)
    {
        EXPECT_EQ(raw[32u + static_cast<size_t>(rec) * 32u + 29u], 0);
        EXPECT_EQ(raw[32u + static_cast<size_t>(rec) * 32u + 30u], 0);
        EXPECT_EQ(raw[32u + static_cast<size_t>(rec) * 32u + 31u], 0);
    }

    std::vector<FoliageRecord> loaded(1);
    ASSERT_TRUE(loadFoliageTile(path, 4, 5, loaded));
    ASSERT_EQ(loaded.size(), 3u);
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_FLOAT_EQ(loaded[static_cast<size_t>(i)].x, recs[i].x);
        EXPECT_FLOAT_EQ(loaded[static_cast<size_t>(i)].y, recs[i].y);
        EXPECT_FLOAT_EQ(loaded[static_cast<size_t>(i)].z, recs[i].z);
        EXPECT_FLOAT_EQ(loaded[static_cast<size_t>(i)].yaw, recs[i].yaw);
        EXPECT_FLOAT_EQ(loaded[static_cast<size_t>(i)].scale, recs[i].scale);
        EXPECT_FLOAT_EQ(loaded[static_cast<size_t>(i)].pitch, recs[i].pitch);
        EXPECT_FLOAT_EQ(loaded[static_cast<size_t>(i)].tiltYaw, recs[i].tiltYaw);
        EXPECT_EQ(loaded[static_cast<size_t>(i)].kind, recs[i].kind);
        EXPECT_EQ(loaded[static_cast<size_t>(i)].pad[0], 0);
        EXPECT_EQ(loaded[static_cast<size_t>(i)].pad[1], 0);
        EXPECT_EQ(loaded[static_cast<size_t>(i)].pad[2], 0);
    }
}

TEST(FoliageFile, CountZero)
{
    TempDir dir("count_zero");
    ASSERT_FALSE(dir.path.empty());
    const auto path = dir.path / "t_03_04.foliage.bin";

    ASSERT_TRUE(saveFoliageTile(path, 3, 4, nullptr, 0));
    std::error_code ec;
    EXPECT_EQ(std::filesystem::file_size(path, ec), static_cast<std::uintmax_t>(32));
    EXPECT_FALSE(ec);

    std::vector<FoliageRecord> loaded(1);
    ASSERT_TRUE(loadFoliageTile(path, 3, 4, loaded));
    EXPECT_TRUE(loaded.empty());

    FoliageRecord dummy{};
    dummy.x    = 9.0f;
    dummy.kind = static_cast<uint8_t>(FoliageKind::Tree);
    ASSERT_TRUE(saveFoliageTile(path, 3, 4, &dummy, 0));
    EXPECT_EQ(std::filesystem::file_size(path, ec), static_cast<std::uintmax_t>(32));
    EXPECT_FALSE(ec);
    loaded.push_back(dummy);
    ASSERT_TRUE(loadFoliageTile(path, 3, 4, loaded));
    EXPECT_TRUE(loaded.empty());
}

TEST(FoliageFile, RejectsBadMagicVersionAndTile)
{
    TempDir dir("bad_header");
    ASSERT_FALSE(dir.path.empty());

    uint8_t header[32]{};
    const auto magicPath = dir.path / "bad_magic.bin";
    fillHeader(header, 0x11223344u, kFoliageVersion, 1, 2, 0);
    ASSERT_TRUE(writeFile(magicPath, header, sizeof(header)));
    expectLoadFails(magicPath, 1, 2);

    const auto versionPath = dir.path / "bad_version.bin";
    fillHeader(header, kFoliageMagic, 99u, 1, 2, 0);
    ASSERT_TRUE(writeFile(versionPath, header, sizeof(header)));
    expectLoadFails(versionPath, 1, 2);

    const auto tilePath = dir.path / "bad_tile.bin";
    ASSERT_TRUE(saveFoliageTile(tilePath, 1, 2, nullptr, 0));
    expectLoadFails(tilePath, 1, 9);
}

TEST(FoliageFile, RejectsHostileCountBeforeAlloc)
{
    TempDir dir("hostile_count");
    ASSERT_FALSE(dir.path.empty());
    const auto path = dir.path / "hostile.bin";

    uint8_t header[32]{};
    std::vector<std::string> logs;
    {
        LogCapture capture(logs);
        fillHeader(header, kFoliageMagic, kFoliageVersion, 2, 3, 0xFFFFFFFFu);
        ASSERT_TRUE(writeFile(path, header, sizeof(header)));
        expectLoadFails(path, 2, 3);
        EXPECT_TRUE(logHas(logs, "rejected"));
        EXPECT_TRUE(logHas(logs, "4294967295"));
        EXPECT_FALSE(logHas(logs, "truncated"));
    }
    {
        LogCapture capture(logs);
        fillHeader(header, kFoliageMagic, kFoliageVersion, 2, 3, 1048576u);
        ASSERT_TRUE(writeFile(path, header, sizeof(header)));
        expectLoadFails(path, 2, 3);
        EXPECT_TRUE(logHas(logs, "rejected"));
        EXPECT_TRUE(logHas(logs, "1048576"));
        EXPECT_FALSE(logHas(logs, "truncated"));
    }
    {
        // 1048575 fits in the sidecar. A header-only file fails as truncated, not as an oversize count.
        LogCapture capture(logs);
        fillHeader(header, kFoliageMagic, kFoliageVersion, 2, 3, 1048575u);
        ASSERT_TRUE(writeFile(path, header, sizeof(header)));
        expectLoadFails(path, 2, 3);
        EXPECT_TRUE(logHas(logs, "truncated"));
        EXPECT_TRUE(logHas(logs, "1048575"));
        EXPECT_FALSE(logHas(logs, "rejected"));
    }
}

TEST(FoliageFile, SaveRejectsCountOverCap)
{
    TempDir dir("save_over_cap");
    ASSERT_FALSE(dir.path.empty());
    const auto path = dir.path / "over.bin";

    FoliageRecord rec{};
    rec.kind = static_cast<uint8_t>(FoliageKind::Tree);
    std::vector<std::string> logs;
    LogCapture capture(logs);
    EXPECT_FALSE(saveFoliageTile(path, 0, 0, &rec, 1048576u));
    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists(path, ec));
    EXPECT_TRUE(logHas(logs, "rejected"));
    EXPECT_TRUE(logHas(logs, "1048576"));
}

TEST(FoliageFile, TruncatedRecord)
{
    TempDir dir("truncated");
    ASSERT_FALSE(dir.path.empty());
    const auto path = dir.path / "short.bin";

    uint8_t header[32]{};
    fillHeader(header, kFoliageMagic, kFoliageVersion, 0, 1, 1);
    ASSERT_TRUE(writeFile(path, header, sizeof(header)));
    expectLoadFails(path, 0, 1);
}

TEST(FoliageFile, RejectsKind)
{
    TempDir dir("bad_kind");
    ASSERT_FALSE(dir.path.empty());
    const auto path = dir.path / "kind.bin";

    ASSERT_TRUE(saveFoliageTile(path, 1, 2, nullptr, 0));

    FoliageRecord bad[2]{};
    bad[0].kind = static_cast<uint8_t>(FoliageKind::Tree);
    bad[0].x    = 1.0f;
    bad[1].kind = 255;
    EXPECT_FALSE(saveFoliageTile(path, 1, 2, bad, 2));
    std::error_code ec;
    EXPECT_EQ(std::filesystem::file_size(path, ec), static_cast<std::uintmax_t>(32));
    EXPECT_FALSE(ec);

    bad[0].kind = static_cast<uint8_t>(FoliageKind::Count);
    EXPECT_FALSE(saveFoliageTile(path, 1, 2, bad, 1));
    EXPECT_EQ(std::filesystem::file_size(path, ec), static_cast<std::uintmax_t>(32));

    uint8_t bytes[32 + 64]{};
    fillHeader(bytes, kFoliageMagic, kFoliageVersion, 6, 7, 1);
    bytes[32 + 28] = 255;
    ASSERT_TRUE(writeFile(path, bytes, 64));
    expectLoadFails(path, 6, 7);

    bytes[32 + 28] = static_cast<uint8_t>(FoliageKind::Count);
    ASSERT_TRUE(writeFile(path, bytes, 64));
    expectLoadFails(path, 6, 7);

    fillHeader(bytes, kFoliageMagic, kFoliageVersion, 6, 7, 2);
    bytes[32 + 28] = static_cast<uint8_t>(FoliageKind::Tree);
    bytes[64 + 28] = 255;
    ASSERT_TRUE(writeFile(path, bytes, sizeof(bytes)));
    expectLoadFails(path, 6, 7);
}

TEST(FoliageFile, FileName)
{
    EXPECT_EQ(tileFoliageFileName(1, 2), "t_01_02.foliage.bin");
}

TEST(FoliageFile, FlagsIgnored)
{
    TempDir dir("flags");
    ASSERT_FALSE(dir.path.empty());
    const auto path = dir.path / "flags.bin";

    FoliageRecord rec{};
    rec.x       = 4.5f;
    rec.y       = 6.5f;
    rec.z       = 7.5f;
    rec.yaw     = 0.25f;
    rec.scale   = 1.1f;
    rec.pitch   = 0.05f;
    rec.tiltYaw = 0.15f;
    rec.kind    = static_cast<uint8_t>(FoliageKind::Flower);
    ASSERT_TRUE(saveFoliageTile(path, 8, 1, &rec, 1));

    std::vector<uint8_t> raw = readAll(path);
    ASSERT_EQ(raw.size(), 64u);
    raw[20] = 0xFF;
    raw[21] = 0xFF;
    raw[22] = 0xFF;
    raw[23] = 0xFF;
    raw[24] = 1;
    raw[28] = 2;
    ASSERT_TRUE(writeFile(path, raw.data(), raw.size()));

    std::vector<FoliageRecord> loaded;
    ASSERT_TRUE(loadFoliageTile(path, 8, 1, loaded));
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_FLOAT_EQ(loaded[0].x, 4.5f);
    EXPECT_FLOAT_EQ(loaded[0].scale, 1.1f);
    EXPECT_EQ(loaded[0].kind, static_cast<uint8_t>(FoliageKind::Flower));
    EXPECT_EQ(loaded[0].pad[0], 0);
    EXPECT_EQ(loaded[0].pad[1], 0);
    EXPECT_EQ(loaded[0].pad[2], 0);
}

TEST(FoliageFile, MissingAndShort)
{
    TempDir dir("missing");
    ASSERT_FALSE(dir.path.empty());
    expectLoadFails(dir.path / "no_such.foliage.bin", 0, 0);

    const auto path = dir.path / "tiny.bin";
    const uint8_t tiny[16]{};
    ASSERT_TRUE(writeFile(path, tiny, sizeof(tiny)));
    expectLoadFails(path, 0, 0);
}

TEST(FoliageFile, SaveNullAndOpenFailure)
{
    TempDir dir("save_fail");
    ASSERT_FALSE(dir.path.empty());
    const auto path = dir.path / "null.bin";
    EXPECT_FALSE(saveFoliageTile(path, 0, 0, nullptr, 1));
    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists(path, ec));

    EXPECT_FALSE(saveFoliageTile(dir.path, 0, 0, nullptr, 0));

    const auto blocker = dir.path / "not_a_dir";
    {
        std::ofstream out(blocker, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out.write("x", 1);
    }
    EXPECT_FALSE(saveFoliageTile(blocker / "t_00_00.foliage.bin", 0, 0, nullptr, 0));
}

