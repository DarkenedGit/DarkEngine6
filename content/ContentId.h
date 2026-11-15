#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Dark
{

    struct ContentError
    {
        std::string message;
    };

    struct ContentId
    {
        uint64_t hash = 0;

        bool operator==(const ContentId& other) const { return hash == other.hash; }
        bool operator!=(const ContentId& other) const { return hash != other.hash; }
    };

    // "wolf" -> "core:wolf". A string that already contains ':' is unchanged.
    std::string canonicalContentId(std::string_view raw);
    ContentId   hashContentId(std::string_view canonical);

    // Records canonical under hash. A second different string with the same hash fails.
    // The same string is accepted again so a package override can replace a record.
    bool claimContentHash(std::unordered_map<uint64_t, std::string>& taken, uint64_t hash, std::string_view canonical, ContentError& err);

} // namespace Dark
