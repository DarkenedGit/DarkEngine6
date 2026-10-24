#include "Gameplay/ThrowableCatalog.h"

#include "Core/ContentRoots.h"
#include "Core/Log.h"

#include "third_party/nlohmann/json.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Dark
{
    namespace
    {
        using json   = nlohmann::json;
        namespace fs = std::filesystem;

        constexpr std::uintmax_t kMaxJsonBytes         = 64u * 1024u;
        constexpr int            kThrowableFileVersion = 1;

        constexpr std::string_view kRockModel    = "models/NaturePack/glTF/Rock_Medium_1.gltf";
        constexpr std::string_view kBottleModel  = "models/unit_glass.gltf";
        constexpr std::string_view kRockImpact   = "audio/foot_rock.wav";
        constexpr std::string_view kBottleImpact = "audio/splash.wav";

        ThrowableDef builtinRock()
        {
            ThrowableDef def;
            def.id            = "rock";
            def.weight        = 2.0f;
            def.throwDistance = 10.0f;
            def.impactSound   = kRockImpact;
            def.callDistance  = 6.0f;
            def.model         = kRockModel;
            return def;
        }

        ThrowableDef builtinBottle()
        {
            ThrowableDef def;
            def.id            = "bottle";
            def.weight        = 0.5f;
            def.throwDistance = 11.0f;
            def.impactSound   = kBottleImpact;
            def.callDistance  = 16.0f;
            def.model         = kBottleModel;
            return def;
        }

        bool jsonToDouble(const json& v, double& out)
        {
            if (const auto* f = v.get_ptr<const json::number_float_t*>())
            {
                out = *f;
                return std::isfinite(out);
            }
            if (const auto* i = v.get_ptr<const json::number_integer_t*>())
            {
                out = static_cast<double>(*i);
                return true;
            }
            if (const auto* u = v.get_ptr<const json::number_unsigned_t*>())
            {
                out = static_cast<double>(*u);
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

        bool isExactInt(double d, int& out)
        {
            if (!std::isfinite(d) || d < -2147483648.0 || d > 2147483647.0)
                return false;
            const int i = static_cast<int>(d);
            if (static_cast<double>(i) != d)
                return false;
            out = i;
            return true;
        }

        bool readPositive(const json& obj, const char* key, float& out)
        {
            const auto it = obj.find(key);
            if (it == obj.end())
                return false;
            double d = 0.0;
            if (!jsonToDouble(*it, d))
                return false;
            const float f = static_cast<float>(d);
            if (!std::isfinite(f) || !(f > 0.0f))
                return false;
            out = f;
            return true;
        }

        bool requirePositive(const json& obj, const char* key, const std::string& id, const char* label, float& out)
        {
            if (obj.find(key) == obj.end())
            {
                DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: entry '{}' missing {}", id, label);
                return false;
            }
            if (!readPositive(obj, key, out))
            {
                DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: entry '{}' has a bad {}", id, label);
                return false;
            }
            return true;
        }

        bool requireText(const json& obj, const char* key, const std::string& id, const char* label, std::string& out)
        {
            if (obj.find(key) == obj.end())
            {
                DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: entry '{}' missing {}", id, label);
                return false;
            }
            if (!readString(obj, key, out) || out.empty())
            {
                DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: entry '{}' has a bad {}", id, label);
                return false;
            }
            return true;
        }

        const json* parseRoot(std::string_view jsonText, json& root)
        {
            if (jsonText.empty() || jsonText.size() > kMaxJsonBytes)
            {
                DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: empty or oversized json");
                return nullptr;
            }
            root = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
            if (root.is_discarded() || !root.is_object())
            {
                DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: discarded or non-object json");
                return nullptr;
            }
            return &root;
        }

        bool readFileVersion(const json& doc)
        {
            const auto verIt  = doc.find("version");
            double     ver    = 0.0;
            int        verInt = 0;
            if (verIt == doc.end() || !jsonToDouble(*verIt, ver) || !isExactInt(ver, verInt) || verInt != kThrowableFileVersion)
            {
                DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: unsupported version");
                return false;
            }
            return true;
        }

        bool parseEntry(const json& item, ThrowableDef& def)
        {
            if (!item.is_object())
            {
                DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: entry is not an object");
                return false;
            }
            if (item.find("id") == item.end())
            {
                DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: entry missing id");
                return false;
            }
            if (!readString(item, "id", def.id) || def.id.empty())
            {
                DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: entry has a bad id");
                return false;
            }
            if (!requirePositive(item, "weight", def.id, "weight", def.weight))
                return false;
            if (!requirePositive(item, "throwDistance", def.id, "throw distance", def.throwDistance))
                return false;
            if (!requireText(item, "impactSound", def.id, "impact sound", def.impactSound))
                return false;
            if (!requirePositive(item, "callDistance", def.id, "call distance", def.callDistance))
                return false;
            if (!requireText(item, "model", def.id, "model", def.model))
                return false;
            return true;
        }

        enum class ContentFile
        {
            Missing,
            Ok,
            Invalid,
        };

        struct LoadedText
        {
            ContentFile state = ContentFile::Missing;
            fs::path    path;
            std::string text;
        };

        LoadedText readFirst()
        {
            LoadedText out;
            for (const fs::path& root : contentRootCandidates())
            {
                if (root.empty())
                    continue;
                const fs::path  path = root / "items" / "throwables.json";
                std::error_code ec;
                if (!fs::is_regular_file(path, ec) || ec)
                    continue;
                const auto sz = fs::file_size(path, ec);
                if (ec || sz > kMaxJsonBytes)
                {
                    DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: unreadable or oversized '{}'", path.string());
                    out.state = ContentFile::Invalid;
                    out.path  = path;
                    return out;
                }
                std::ifstream in(path, std::ios::binary);
                if (!in)
                {
                    DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: failed to open '{}'", path.string());
                    out.state = ContentFile::Invalid;
                    out.path  = path;
                    return out;
                }
                out.text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                out.state = ContentFile::Ok;
                out.path  = path;
                return out;
            }
            return out;
        }
    } // namespace

    ThrowableCatalog::ThrowableCatalog()
    {
        m_items.push_back(builtinRock());
        m_items.push_back(builtinBottle());
    }

    bool ThrowableCatalog::parse(std::string_view jsonText)
    {
        json        root;
        const json* doc = parseRoot(jsonText, root);
        if (!doc)
            return false;
        if (!readFileVersion(*doc))
            return false;

        const auto itemsIt = doc->find("throwables");
        if (itemsIt == doc->end() || !itemsIt->is_array())
        {
            DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: throwables must be an array");
            return false;
        }

        std::vector<ThrowableDef> parsed;
        parsed.reserve(itemsIt->size());
        for (const json& item : *itemsIt)
        {
            ThrowableDef def;
            if (!parseEntry(item, def))
                return false;
            for (const ThrowableDef& existing : parsed)
            {
                if (existing.id == def.id)
                {
                    DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: duplicate id '{}'", def.id);
                    return false;
                }
            }
            parsed.push_back(std::move(def));
        }
        if (parsed.empty())
        {
            DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: throwables is empty");
            return false;
        }

        m_items = std::move(parsed);
        return true;
    }

    bool ThrowableCatalog::loadFromContent()
    {
        if (m_loaded)
            return m_loadOk;

        const LoadedText file = readFirst();
        bool             ok   = false;
        if (file.state == ContentFile::Missing)
        {
            DE_LOG_WARN(LogCategory::Core, "ThrowableCatalog: items/throwables.json missing; keeping built-in rock and bottle");
            ok = true;
        }
        else if (file.state != ContentFile::Ok)
        {
            DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: load failed; keeping the previous catalog");
            ok = false;
        }
        else
        {
            ThrowableCatalog temp;
            if (!temp.parse(file.text))
            {
                DE_LOG_ERROR(LogCategory::Core, "ThrowableCatalog: load failed; keeping the previous catalog");
                ok = false;
            }
            else
            {
                m_items = std::move(temp.m_items);
                DE_LOG_INFO(LogCategory::Core, "ThrowableCatalog: loaded '{}'", file.path.string());
                ok = true;
            }
        }

        m_loaded = true;
        m_loadOk = ok;
        return ok;
    }

    const ThrowableDef* ThrowableCatalog::find(std::string_view id) const
    {
        for (const ThrowableDef& item : m_items)
        {
            if (item.id == id)
                return &item;
        }
        return nullptr;
    }

    ThrowableCatalog& throwableCatalog()
    {
        static ThrowableCatalog catalog;
        return catalog;
    }

} // namespace Dark
