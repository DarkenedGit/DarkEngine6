#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace Dark
{

    struct AudioPreviewEntry
    {
        std::string virtualPath; // "audio/...", forward slashes, for loadWav
        std::string relative;    // path under the audio directory
    };

    // Lists .wav files under a content/audio directory. Virtual paths use the same
    // "audio/..." form AssetManager::resolve and AudioSystem::loadWav already use.
    inline bool listContentAudioWavs(const std::filesystem::path& audioDir, std::vector<AudioPreviewEntry>& out)
    {
        out.clear();
        std::error_code ec;
        if (audioDir.empty() || !std::filesystem::is_directory(audioDir, ec) || ec)
            return false;

        const std::filesystem::path canonical = std::filesystem::weakly_canonical(audioDir, ec);
        const std::filesystem::path& base     = ec ? audioDir : canonical;
        ec.clear();

        std::filesystem::recursive_directory_iterator it(base, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec)
            return false;
        const std::filesystem::recursive_directory_iterator end;
        for (; it != end; it.increment(ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }
            const std::filesystem::path& path = it->path();
            const std::string            name = path.filename().string();
            if (name.empty() || name[0] == '.')
            {
                std::error_code dirEc;
                if (it->is_directory(dirEc) && !dirEc)
                    it.disable_recursion_pending();
                continue;
            }

            std::error_code fileEc;
            if (!it->is_regular_file(fileEc) || fileEc)
                continue;

            std::string ext = path.extension().string();
            for (char& c : ext)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".wav")
                continue;

            const std::filesystem::path rel = std::filesystem::relative(path, base, ec);
            if (ec || rel.empty())
            {
                ec.clear();
                continue;
            }
            AudioPreviewEntry entry;
            entry.relative    = rel.generic_string();
            entry.virtualPath = std::string("audio/") + entry.relative;
            out.push_back(std::move(entry));
        }

        std::sort(out.begin(), out.end(), [](const AudioPreviewEntry& a, const AudioPreviewEntry& b) {
            return a.relative < b.relative;
        });
        return true;
    }

} // namespace Dark
