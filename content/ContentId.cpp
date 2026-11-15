#include "Content/ContentId.h"

#include "Save/SaveTypes.h"

namespace Dark
{

    std::string canonicalContentId(std::string_view raw)
    {
        if (raw.empty())
            return {};
        if (raw.find(':') != std::string_view::npos)
            return std::string(raw);
        std::string out = "core:";
        out.append(raw.data(), raw.size());
        return out;
    }

    ContentId hashContentId(std::string_view canonical)
    {
        ContentId id;
        id.hash = Save::fnv1a64(canonical.data(), canonical.size());
        return id;
    }

    bool claimContentHash(std::unordered_map<uint64_t, std::string>& taken, uint64_t hash, std::string_view canonical, ContentError& err)
    {
        const auto it = taken.find(hash);
        if (it == taken.end())
        {
            taken.emplace(hash, std::string(canonical));
            return true;
        }
        if (it->second == canonical)
            return true;
        err.message = "content id hash collision between '" + it->second + "' and '" + std::string(canonical) + "'";
        return false;
    }

} // namespace Dark
