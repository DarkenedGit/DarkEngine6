#include "Media/MoviePath.h"

#include "Assets/AssetManager.h"
#include "Core/Log.h"

#include <filesystem>
#include <string_view>

namespace Dark
{

    namespace
    {

        constexpr uint32_t kMaxCodedWidth     = 3840;
        constexpr uint32_t kMaxCodedHeight    = 2160;
        constexpr uint32_t kMaxSoftwareWidth  = 1920;
        constexpr uint32_t kMaxSoftwareHeight = 1080;

        std::string asciiLowerGeneric(std::string s)
        {
            for (char& c : s)
            {
                if (c == '\\')
                    c = '/';
                else if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            }
            return s;
        }

        bool extensionIs(const std::filesystem::path& path, const char* ext)
        {
            std::string got = asciiLowerGeneric(path.extension().string());
            return got == ext;
        }

        void logRejected(const char* virtualPath)
        {
            DE_LOG_ERROR(LogCategory::Media, "Movie path rejected: '{}'", virtualPath ? virtualPath : "");
        }

    } // namespace

    bool movieExtensionAllowed(const char* virtualPath)
    {
        if (!virtualPath || virtualPath[0] == '\0')
            return false;

        const std::filesystem::path path(virtualPath);
        return extensionIs(path, ".mp4") || extensionIs(path, ".m4v") || extensionIs(path, ".mov") || extensionIs(path, ".mpg") || extensionIs(path, ".mpeg")
            || extensionIs(path, ".vob") || extensionIs(path, ".ts") || extensionIs(path, ".m2ts") || extensionIs(path, ".mts");
    }

    MovieStatus movieVirtualPathPrecheck(const char* virtualPath)
    {
        if (!virtualPath || virtualPath[0] == '\0')
            return MovieStatus::FileMissing;

        const std::string_view text(virtualPath);
        if (text.find("://") != std::string_view::npos)
            return MovieStatus::UnsupportedContainer;

        const std::filesystem::path path(virtualPath);
        if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
            return MovieStatus::FileMissing;

        for (const std::filesystem::path& part : path)
        {
            if (part == "..")
                return MovieStatus::FileMissing;
        }

        if (!movieExtensionAllowed(virtualPath))
            return MovieStatus::UnsupportedContainer;

        return MovieStatus::Ok;
    }

    bool movieCodedSizeAllowed(uint32_t width, uint32_t height)
    {
        return width > 0 && height > 0 && width <= kMaxCodedWidth && height <= kMaxCodedHeight;
    }

    bool movieSoftwareFallbackAllowed(uint32_t width, uint32_t height)
    {
        return width > 0 && height > 0 && width <= kMaxSoftwareWidth && height <= kMaxSoftwareHeight;
    }

    bool movieNormalizedFileUnderMount(const std::string& normalizedFile, const std::string& normalizedMount)
    {
        const std::string file  = asciiLowerGeneric(normalizedFile);
        const std::string mount = asciiLowerGeneric(normalizedMount);
        if (file.empty() || mount.empty())
            return false;

        std::string prefix = mount;
        if (prefix.back() != '/')
            prefix.push_back('/');
        return file.size() > prefix.size() && file.compare(0, prefix.size(), prefix) == 0;
    }

    MovieStatus moviePathStatus(AssetManager& assets, const char* virtualPath)
    {
        const MovieStatus pre = movieVirtualPathPrecheck(virtualPath);
        if (pre != MovieStatus::Ok)
        {
            logRejected(virtualPath);
            return pre;
        }

        const std::filesystem::path resolved = assets.resolve(virtualPath);
        if (resolved.empty())
        {
            logRejected(virtualPath);
            return MovieStatus::FileMissing;
        }

        std::error_code ec;
        if (!std::filesystem::is_regular_file(resolved, ec) || ec)
        {
            logRejected(virtualPath);
            return MovieStatus::FileMissing;
        }

        const auto size = std::filesystem::file_size(resolved, ec);
        if (ec || size == 0)
        {
            logRejected(virtualPath);
            return MovieStatus::FileMissing;
        }

        // Containment is the public prefix check. Do not walk m_mounts.
        if (assets.virtualPathFromAbsolute(resolved).empty())
        {
            logRejected(virtualPath);
            return MovieStatus::FileMissing;
        }

        return MovieStatus::Ok;
    }

} // namespace Dark
