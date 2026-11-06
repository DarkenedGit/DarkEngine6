#pragma once

#include "Media/MovieStatus.h"

#include <cstdint>
#include <string>

namespace Dark
{

    class AssetManager;

    // Empty, absolute, and `..` are FileMissing. `://` and an extension outside the allow-list are UnsupportedContainer.
    // Runs before any mount join.
    MovieStatus movieVirtualPathPrecheck(const char* virtualPath);

    bool movieExtensionAllowed(const char* virtualPath);

    bool movieCodedSizeAllowed(uint32_t width, uint32_t height);
    bool movieSoftwareFallbackAllowed(uint32_t width, uint32_t height);

    // Prefix rule inside virtualPathFromAbsolute: generic separators, ASCII lower, trailing '/', strictly longer file.
    bool movieNormalizedFileUnderMount(const std::string& normalizedFile, const std::string& normalizedMount);

    // Precheck, then resolve, a non-empty regular file, then AssetManager::virtualPathFromAbsolute.
    MovieStatus moviePathStatus(AssetManager& assets, const char* virtualPath);

} // namespace Dark
