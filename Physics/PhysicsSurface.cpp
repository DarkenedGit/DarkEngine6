#include "Physics/PhysicsSurface.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"

#include "third_party/nlohmann/json.hpp"

#include <fstream>
#include <sstream>

namespace Dark::Physics
{
    namespace
    {
        using json = nlohmann::json;

        constexpr std::uintmax_t kMaxJsonBytes = 256u * 1024u;
        constexpr uint32_t       kMaxSurfaces  = 1024;

        PhysicsSurface makeBuiltinDefault(uint32_t internId)
        {
            PhysicsSurface s;
            s.id       = "default";
            s.internId = internId;
            s.friction = 0.6f;
            s.density  = 1.0f;
            s.tags.push_back("generic");
            return s;
        }

        bool jsonToFloat(const json& v, float& out)
        {
            if (!v.is_number())
                return false;
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

        bool jsonToU32(const json& v, uint32_t& out)
        {
            if (const auto* u = v.get_ptr<const json::number_unsigned_t*>())
            {
                if (*u > 0xFFFFFFFFull)
                    return false;
                out = static_cast<uint32_t>(*u);
                return true;
            }
            if (const auto* i = v.get_ptr<const json::number_integer_t*>())
            {
                if (*i < 0)
                    return false;
                out = static_cast<uint32_t>(*i);
                return true;
            }
            return false;
        }

        bool readFloat(const json& obj, const char* key, float& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end())
                return false;
            return jsonToFloat(*it, out);
        }

        bool readBool(const json& obj, const char* key, bool& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end() || !it->is_boolean())
                return false;
            if (const auto* b = it->get_ptr<const json::boolean_t*>())
            {
                out = *b;
                return true;
            }
            return false;
        }

        bool readString(const json& obj, const char* key, std::string& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end() || !it->is_string())
                return false;
            if (const auto* s = it->get_ptr<const json::string_t*>())
            {
                out = *s;
                return true;
            }
            return false;
        }

        int hexNibble(char c)
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }

        bool parseHexColor(std::string_view text, uint32_t& out)
        {
            if (text.size() >= 1 && text[0] == '#')
                text.remove_prefix(1);
            else if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
                text.remove_prefix(2);
            if (text.size() != 6)
                return false;
            uint32_t rgb = 0;
            for (char c : text)
            {
                const int n = hexNibble(c);
                if (n < 0)
                    return false;
                rgb = (rgb << 4) | static_cast<uint32_t>(n);
            }
            out = rgb;
            return true;
        }

        bool readCustomColor(const json& obj, uint32_t& out)
        {
            const auto it = obj.find("customColor");
            if (it == obj.end())
                return false;
            if (it->is_string())
            {
                if (const auto* s = it->get_ptr<const json::string_t*>())
                    return parseHexColor(*s, out);
                return false;
            }
            return jsonToU32(*it, out);
        }

        bool readTags(const json& obj, std::vector<std::string>& out)
        {
            const auto it = obj.find("tags");
            if (it == obj.end() || !it->is_array())
                return false;
            std::vector<std::string> tags;
            for (const json& item : *it)
            {
                if (!item.is_string())
                    continue;
                if (const auto* s = item.get_ptr<const json::string_t*>())
                {
                    if (!s->empty())
                        tags.push_back(*s);
                }
            }
            out = std::move(tags);
            return true;
        }

        bool readTangentVelocity(const json& obj, Math::Vector3f& out)
        {
            const auto it = obj.find("tangentVelocity");
            if (it == obj.end() || !it->is_array() || it->size() != 3)
                return false;
            float xyz[3] = {0.0f, 0.0f, 0.0f};
            int   i      = 0;
            for (const json& item : *it)
            {
                if (!jsonToFloat(item, xyz[i]))
                    return false;
                ++i;
            }
            out = Math::Vector3f(xyz[0], xyz[1], xyz[2]);
            return true;
        }

        bool parseSurfaceObject(const json& obj, PhysicsSurface& out)
        {
            out = PhysicsSurface{};
            if (!obj.is_object())
                return false;
            if (!readString(obj, "id", out.id) || out.id.empty())
                return false;
            readFloat(obj, "friction", out.friction);
            readFloat(obj, "restitution", out.restitution);
            readFloat(obj, "density", out.density);
            readFloat(obj, "rolling", out.rolling);
            readBool(obj, "sensor", out.sensor);
            readBool(obj, "climbable", out.climbable);
            readFloat(obj, "damagePerSecond", out.damagePerSecond);
            readString(obj, "footstep", out.footstep);
            readTags(obj, out.tags);
            readCustomColor(obj, out.customColor);
            readTangentVelocity(obj, out.tangentVelocity);
            return true;
        }

        bool parseSurfaces(std::string_view jsonText, std::vector<PhysicsSurface>& parsed)
        {
            parsed.clear();
            if (jsonText.empty())
            {
                DE_LOG_ERROR(LogCategory::Collision, "PhysicsSurface: empty json");
                return false;
            }
            if (jsonText.size() > kMaxJsonBytes)
            {
                DE_LOG_ERROR(LogCategory::Collision, "PhysicsSurface: json exceeds {} bytes", static_cast<uint32_t>(kMaxJsonBytes));
                return false;
            }

            const json root = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
            if (root.is_discarded() || !root.is_object())
            {
                DE_LOG_ERROR(LogCategory::Collision, "PhysicsSurface: discarded or non-object json");
                return false;
            }

            const auto verIt = root.find("version");
            if (verIt != root.end())
            {
                float ver = 0.0f;
                if (!jsonToFloat(*verIt, ver) || static_cast<int>(ver) != 1)
                {
                    DE_LOG_ERROR(LogCategory::Collision, "PhysicsSurface: unsupported version");
                    return false;
                }
            }

            const auto surfacesIt = root.find("surfaces");
            if (surfacesIt == root.end() || !surfacesIt->is_array())
            {
                DE_LOG_ERROR(LogCategory::Collision, "PhysicsSurface: missing surfaces array");
                return false;
            }
            if (surfacesIt->size() > kMaxSurfaces)
            {
                DE_LOG_ERROR(LogCategory::Collision, "PhysicsSurface: too many surfaces ({})", surfacesIt->size());
                return false;
            }

            parsed.reserve(surfacesIt->size() + 1);
            for (const json& item : *surfacesIt)
            {
                PhysicsSurface s;
                if (!parseSurfaceObject(item, s))
                {
                    DE_LOG_WARN(LogCategory::Collision, "PhysicsSurface: skipping entry without a valid id");
                    continue;
                }
                for (const PhysicsSurface& existing : parsed)
                {
                    if (existing.id == s.id)
                    {
                        DE_LOG_ERROR(LogCategory::Collision, "PhysicsSurface: duplicate id '{}'", s.id);
                        parsed.clear();
                        return false;
                    }
                }
                s.internId = static_cast<uint32_t>(parsed.size()) + 1;
                parsed.push_back(std::move(s));
            }

            bool hasDefault = false;
            for (const PhysicsSurface& s : parsed)
            {
                if (s.id == "default")
                {
                    hasDefault = true;
                    break;
                }
            }
            if (!hasDefault)
            {
                if (parsed.size() >= kMaxSurfaces)
                {
                    DE_LOG_ERROR(LogCategory::Collision, "PhysicsSurface: no room to inject default");
                    parsed.clear();
                    return false;
                }
                parsed.push_back(makeBuiltinDefault(static_cast<uint32_t>(parsed.size()) + 1));
                DE_LOG_WARN(LogCategory::Collision, "PhysicsSurface: injected builtin default");
            }
            return true;
        }
    } // namespace

    bool PhysicsSurfaceCatalog::parse(std::string_view jsonText)
    {
        std::vector<PhysicsSurface> parsed;
        if (!parseSurfaces(jsonText, parsed))
            return false;
        m_surfaces = std::move(parsed);
        return true;
    }

    bool PhysicsSurfaceCatalog::loadFile(const std::filesystem::path& path)
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsSurface: not a file '{}'", path.string());
            return false;
        }
        const auto sz = std::filesystem::file_size(path, ec);
        if (ec || sz > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsSurface: unreadable or too large '{}'", path.string());
            return false;
        }

        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsSurface: failed to open '{}'", path.string());
            return false;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        return parse(ss.str());
    }

    bool PhysicsSurfaceCatalog::loadFromContent()
    {
        for (const std::filesystem::path& root : contentRootCandidates())
        {
            if (root.empty())
                continue;
            const std::filesystem::path path = root / "physics" / "surfaces.json";
            std::error_code             ec;
            if (std::filesystem::is_regular_file(path, ec) && !ec)
                return loadFile(path);
        }
        DE_LOG_ERROR(LogCategory::Collision, "PhysicsSurface: physics/surfaces.json not found in content roots");
        return false;
    }

    void PhysicsSurfaceCatalog::clear()
    {
        m_surfaces.clear();
    }

    uint32_t PhysicsSurfaceCatalog::idOf(std::string_view name) const
    {
        if (const PhysicsSurface* s = find(name))
            return s->internId;
        return kNullPhysicsSurfaceId;
    }

    const PhysicsSurface* PhysicsSurfaceCatalog::find(std::string_view name) const
    {
        for (const PhysicsSurface& s : m_surfaces)
        {
            if (s.id == name)
                return &s;
        }
        return nullptr;
    }

    const PhysicsSurface* PhysicsSurfaceCatalog::find(uint32_t internId) const
    {
        if (internId == kNullPhysicsSurfaceId || internId > m_surfaces.size())
            return nullptr;
        const PhysicsSurface& s = m_surfaces[internId - 1];
        if (s.internId != internId)
            return nullptr;
        return &s;
    }

    const PhysicsSurface* PhysicsSurfaceCatalog::findByUserMaterialId(uint64_t userMaterialId) const
    {
        if (userMaterialId == 0 || userMaterialId > 0xFFFFFFFFull)
            return nullptr;
        return find(static_cast<uint32_t>(userMaterialId));
    }

    const PhysicsSurface& PhysicsSurfaceCatalog::getDefault() const
    {
        if (const PhysicsSurface* s = find("default"))
            return *s;
        static const PhysicsSurface kFallback = makeBuiltinDefault(1);
        return kFallback;
    }
} // namespace Dark::Physics
