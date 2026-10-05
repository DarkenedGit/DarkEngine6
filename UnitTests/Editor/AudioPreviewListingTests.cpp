#include <gtest/gtest.h>

#include "Editor/AudioPreviewListing.h"

#include <filesystem>
#include <fstream>

using namespace Dark;

namespace
{

std::filesystem::path testRoot()
{
    std::error_code ec;
    return std::filesystem::current_path(ec) / "audio-preview-list-test";
}

void writeFile(const std::filesystem::path& path)
{
    std::ofstream out(path, std::ios::binary);
    out << "x";
}

} // namespace

TEST(AudioPreviewListing, ListsWavsRecursivelyAndSkipsOtherFiles)
{
    std::error_code ec;
    const std::filesystem::path root = testRoot();
    std::filesystem::remove_all(root, ec);
    ec.clear();
    std::filesystem::create_directories(root / "human" / "pain", ec);
    ASSERT_FALSE(ec);
    std::filesystem::create_directories(root / ".hidden", ec);
    ASSERT_FALSE(ec);

    writeFile(root / "coin.wav");
    writeFile(root / "notes.txt");
    writeFile(root / "top.WAV");
    writeFile(root / "human" / "readme.md");
    writeFile(root / "human" / "pain" / "a.wav");
    writeFile(root / ".hidden" / "secret.wav");

    std::vector<AudioPreviewEntry> entries;
    ASSERT_TRUE(listContentAudioWavs(root, entries));
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].relative, "coin.wav");
    EXPECT_EQ(entries[0].virtualPath, "audio/coin.wav");
    EXPECT_EQ(entries[1].relative, "human/pain/a.wav");
    EXPECT_EQ(entries[1].virtualPath, "audio/human/pain/a.wav");
    EXPECT_EQ(entries[2].relative, "top.WAV");
    EXPECT_EQ(entries[2].virtualPath, "audio/top.WAV");

    std::filesystem::remove_all(root, ec);
}

TEST(AudioPreviewListing, MissingDirectoryReturnsFalse)
{
    std::vector<AudioPreviewEntry> entries;
    entries.push_back(AudioPreviewEntry{ "keep", "keep" });
    const std::filesystem::path missing = testRoot() / "does-not-exist";
    EXPECT_FALSE(listContentAudioWavs(missing, entries));
    EXPECT_TRUE(entries.empty());
}
