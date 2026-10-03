#include <gtest/gtest.h>

#include "Save/SavePaths.h"
#include "Save/SaveTypes.h"

using namespace Dark;

TEST(SavePath, AcceptsKindNames)
{
    EXPECT_TRUE(Save::validSaveFileName("save_2026-10-03_18-04-40_ab12.json"));
    EXPECT_TRUE(Save::validSaveFileName("quick_2026-10-03_18-04-40_9f03.json"));
    EXPECT_TRUE(Save::validSaveFileName("auto_2026-10-03_18-04-40_51c7.json"));
}

TEST(SavePath, RejectsUnsafeNames)
{
    EXPECT_FALSE(Save::validSaveFileName(""));
    EXPECT_FALSE(Save::validSaveFileName("."));
    EXPECT_FALSE(Save::validSaveFileName(".."));
    EXPECT_FALSE(Save::validSaveFileName("manual_2026-10-03_18-04-40_ab12.json"));
    EXPECT_FALSE(Save::validSaveFileName("Quick_2026-10-03_18-04-40_9f03.json"));
    EXPECT_FALSE(Save::validSaveFileName("quick_2026-10-03_18-04-40_9F03.json"));
    EXPECT_FALSE(Save::validSaveFileName("saves/quick_2026-10-03_18-04-40_9f03.json"));
    EXPECT_FALSE(Save::validSaveFileName("CON"));
    EXPECT_FALSE(Save::validSaveFileName("quick_2026-10-03_18-04-40_9f03.txt"));
}

TEST(SavePath, MakeNameMatchesThePattern)
{
    std::string name;
    EXPECT_EQ(Save::makeSaveName(Save::SaveKind::Quick, name), Save::SaveResult::Ok);
    EXPECT_TRUE(Save::validSaveFileName(name));
    EXPECT_TRUE(name.starts_with("quick_"));
}
