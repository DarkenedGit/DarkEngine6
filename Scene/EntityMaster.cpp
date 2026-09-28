#include "Scene/EntityMaster.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"

#include "third_party/nlohmann/json.hpp"

#include <fstream>
#include <sstream>

namespace Dark
{
    namespace
    {
        using json = nlohmann::json;

        constexpr std::uintmax_t kMaxJsonBytes = 64u * 1024u;

        bool readString(const json& obj, const char* key, std::string& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end())
                return false;
            if (const auto* s = it->get_ptr<const json::string_t*>())
            {
                out = *s;
                return true;
            }
            return false;
        }

        bool jsonToFloat(const json& v, float& out)
        {
            if (const auto* f = v.get_ptr<const json::number_float_t*>())
            {
                out = static_cast<float>(*f);
                return true;
            }
            if (const auto* i = v.get_ptr<const json::number_integer_t*>())
            {
                out = static_cast<float>(*i);
                return true;
            }
            if (const auto* u = v.get_ptr<const json::number_unsigned_t*>())
            {
                out = static_cast<float>(*u);
                return true;
            }
            return false;
        }
    }

    std::string entityMasterVirtualPath(std::string_view typeName)
    {
        return std::string("entities/") + std::string(typeName) + ".entity.json";
    }

    bool parseEntityMaster(std::string_view jsonText, EntityMaster& out)
    {
        if (jsonText.empty() || jsonText.size() > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::Core, "EntityMaster: empty or oversized json");
            return false;
        }
        const json root = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            DE_LOG_ERROR(LogCategory::Core, "EntityMaster: discarded or non-object json");
            return false;
        }

        const auto verIt = root.find("version");
        if (verIt != root.end())
        {
            float ver = 0.0f;
            if (!jsonToFloat(*verIt, ver) || static_cast<int>(ver) != 1)
            {
                DE_LOG_ERROR(LogCategory::Core, "EntityMaster: unsupported version");
                return false;
            }
        }

        EntityMaster parsed;
        if (!readString(root, "type", parsed.type) || parsed.type.empty())
        {
            DE_LOG_ERROR(LogCategory::Core, "EntityMaster: missing type");
            return false;
        }
        readString(root, "gltf", parsed.gltf);
        readString(root, "anim", parsed.anim);
        readString(root, "physics", parsed.physics);
        readString(root, "hsm", parsed.hsm);
        out = std::move(parsed);
        return true;
    }

    bool loadEntityMasterFile(const std::filesystem::path& path, EntityMaster& out)
    {
        std::error_code ec;
        if (path.empty() || !std::filesystem::is_regular_file(path, ec) || ec)
            return false;
        const auto sz = std::filesystem::file_size(path, ec);
        if (ec || sz > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::Core, "EntityMaster: unreadable or too large '{}'", path.string());
            return false;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        return parseEntityMaster(ss.str(), out);
    }

    std::filesystem::path resolveContentFile(std::string_view relative)
    {
        if (relative.empty())
            return {};
        const std::filesystem::path rel{ std::string(relative) };
        for (const std::filesystem::path& root : contentRootCandidates())
        {
            if (root.empty())
                continue;
            const std::filesystem::path path = root / rel;
            std::error_code             ec;
            if (std::filesystem::is_regular_file(path, ec) && !ec)
                return path;
        }
        return {};
    }

    std::filesystem::path authoringContentFile(std::string_view relative)
    {
        const std::filesystem::path rel{ std::string(relative) };
        const std::filesystem::path root = authoringContentRoot();
        if (root.empty())
            return rel;
        return root / rel;
    }

    EntityMaster loadEntityMaster(std::string_view virtualPath)
    {
        EntityMaster master;
        const std::filesystem::path path = resolveContentFile(virtualPath);
        if (path.empty() || !loadEntityMasterFile(path, master))
            return {};
        return master;
    }

    EntityMaster loadEntityMasterType(std::string_view typeName)
    {
        if (typeName.empty())
            return {};
        return loadEntityMaster(entityMasterVirtualPath(typeName));
    }
}
