#include <gtest/gtest.h>

#include "SaveTestWorld.h"

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Save/AtomicFile.h"
#include "Save/SavePaths.h"

#include <filesystem>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

using namespace Dark;

namespace
{
    int g_begin = 0;
    void beginLoad(void*, World&) { ++g_begin; }

    std::filesystem::path tempSave()
    {
        std::error_code ec;
        return std::filesystem::temp_directory_path(ec) / "quick_2026-10-03_18-04-40_ab12.json";
    }

    bool regularFile(const std::filesystem::path& path)
    {
        std::error_code ec;
        return std::filesystem::is_regular_file(path, ec);
    }

    std::filesystem::path corruptPath(const std::filesystem::path& path)
    {
        std::filesystem::path corrupt = path;
        corrupt += L".corrupt";
        return corrupt;
    }

    struct HeldSave
    {
        std::filesystem::path path;
        HANDLE                handle = INVALID_HANDLE_VALUE;

        HeldSave()                   = default;
        HeldSave(const HeldSave&)    = delete;
        HeldSave& operator=(const HeldSave&) = delete;

        ~HeldSave()
        {
            if (handle != INVALID_HANDLE_VALUE)
                CloseHandle(handle);
            std::error_code ec;
            std::filesystem::remove(path, ec);
            std::filesystem::remove(corruptPath(path), ec);
        }
    };

    Save::SaveResult writeSave(HeldSave& held, const char* name, const char* body)
    {
        held.path = Save::savesDirectory() / name;
        std::error_code ec;
        std::filesystem::remove(held.path, ec);
        std::filesystem::remove(corruptPath(held.path), ec);
        return Save::writeAtomic(held.path, body);
    }

    bool listedAs(const char* name)
    {
        std::vector<Save::SaveFileInfo> files;
        if (Save::listSaveFiles(files) != Save::SaveResult::Ok)
            return false;
        for (const Save::SaveFileInfo& info : files)
        {
            if (info.name == name)
                return true;
        }
        return false;
    }
}

TEST(SaveCorruption, ChecksumMismatchLeavesTheWorld)
{
    g_begin = 0;
    World world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = { 1.0f, 2.0f, 3.0f };
    world.emplace<TransformComponent>(e, xf);
    stampProceduralId(world, e, "unit/sum");
    Save::SaveSystem save;
    Save::SaveHost host = testHost();
    host.beginLoad = beginLoad;
    armSave(save, host);
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(json, root));
    root["integrity"]["payload"] = "0000000000000000";
    world.get<TransformComponent>(e)->position.x = 7.0f;
    EXPECT_EQ(save.applyPayloadText(world, root.dump(2)), Save::SaveResult::ChecksumMismatch);
    EXPECT_EQ(g_begin, 0);
    EXPECT_EQ(world.get<TransformComponent>(e)->position.x, 7.0f);
}

TEST(SaveCorruption, TruncatedFileDoesNotApply)
{
    World world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = { 1.0f, 0.0f, 0.0f };
    world.emplace<TransformComponent>(e, xf);
    stampProceduralId(world, e, "unit/trunc");
    Save::SaveSystem save;
    armSave(save, testHost());
    const auto path = tempSave();
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ASSERT_EQ(Save::writeAtomic(path, "{"), Save::SaveResult::Ok);
    EXPECT_EQ(save.loadNow(world, path), Save::SaveResult::ParseFailed);
    EXPECT_EQ(world.get<TransformComponent>(e)->position.x, 1.0f);
    std::filesystem::remove(path, ec);
}

TEST(SaveCorruption, BadDocumentIsRenamedAndReadableSiblingRemains)
{
    ASSERT_FALSE(Save::savesDirectory().empty());
    HeldSave parseFail;
    HeldSave badUtc;
    HeldSave readable;
    ASSERT_EQ(writeSave(parseFail, "quick_2099-11-01_00-00-01_a011.json", "{"), Save::SaveResult::Ok);
    ASSERT_EQ(writeSave(badUtc, "quick_2099-11-01_00-00-02_a012.json", "{\"savedAtUtc\":1}"), Save::SaveResult::Ok);
    ASSERT_EQ(writeSave(readable, "quick_2099-11-01_00-00-03_a013.json", "{\"savedAtUtc\":\"2001-01-01T00:00:00Z\"}"), Save::SaveResult::Ok);

    Save::SaveSystem save;
    const Save::SaveResult result = save.requestLoadNewest(Save::SaveKind::Quick);
    EXPECT_TRUE(result == Save::SaveResult::Ok || result == Save::SaveResult::IoReadFailed);
    EXPECT_FALSE(regularFile(parseFail.path));
    EXPECT_TRUE(regularFile(corruptPath(parseFail.path)));
    EXPECT_FALSE(regularFile(badUtc.path));
    EXPECT_TRUE(regularFile(corruptPath(badUtc.path)));
    EXPECT_TRUE(regularFile(readable.path));
    EXPECT_FALSE(regularFile(corruptPath(readable.path)));
}

TEST(SaveCorruption, UnreadableSaveIsLeftAndNewestLoadFails)
{
    ASSERT_FALSE(Save::savesDirectory().empty());
    HeldSave older;
    HeldSave locked;
    ASSERT_EQ(writeSave(older, "quick_2099-11-01_00-00-05_a015.json", "{\"savedAtUtc\":\"2001-01-01T00:00:00Z\"}"), Save::SaveResult::Ok);
    ASSERT_EQ(writeSave(locked, "quick_2099-11-01_00-00-04_a014.json", "{\"savedAtUtc\":\"2099-06-01T00:00:00Z\"}"), Save::SaveResult::Ok);
    locked.handle = CreateFileW(locked.path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(locked.handle, INVALID_HANDLE_VALUE);
    ASSERT_TRUE(listedAs("quick_2099-11-01_00-00-04_a014.json"));

    Save::SaveSystem save;
    EXPECT_EQ(save.requestLoadNewest(Save::SaveKind::Quick), Save::SaveResult::IoReadFailed);
    EXPECT_TRUE(regularFile(locked.path));
    EXPECT_FALSE(regularFile(corruptPath(locked.path)));
    EXPECT_TRUE(regularFile(older.path));
    EXPECT_FALSE(regularFile(corruptPath(older.path)));
}
