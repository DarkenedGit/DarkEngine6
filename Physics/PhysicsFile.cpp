#include "Physics/PhysicsFile.h"
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

        constexpr std::uintmax_t kMaxJsonBytes = 64u * 1024u;

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
            if (it == obj.end())
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
            if (it == obj.end())
                return false;
            if (const auto* s = it->get_ptr<const json::string_t*>())
            {
                out = *s;
                return true;
            }
            return false;
        }

        bool finiteNonNegative(float v)
        {
            return v == v && v >= 0.0f && v <= 1.0e8f;
        }

        bool finiteAny(float v)
        {
            return v == v && v >= -1.0e6f && v <= 1.0e6f;
        }

        std::string lowerAscii(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (unsigned char c : text)
            {
                if (c >= 'A' && c <= 'Z')
                    out.push_back(static_cast<char>(c - 'A' + 'a'));
                else
                    out.push_back(static_cast<char>(c));
            }
            return out;
        }

        bool parseBody(std::string_view text, PhysicsBodyMode& out)
        {
            const std::string s = lowerAscii(text);
            if (s == "static")
                out = PhysicsBodyMode::Static;
            else if (s == "kinematic")
                out = PhysicsBodyMode::Kinematic;
            else if (s == "dynamic")
                out = PhysicsBodyMode::Dynamic;
            else
                return false;
            return true;
        }

        bool parseShape(std::string_view text, PhysicsShapeKind& out)
        {
            const std::string s = lowerAscii(text);
            if (s == "box")
                out = PhysicsShapeKind::Box;
            else if (s == "sphere")
                out = PhysicsShapeKind::Sphere;
            else if (s == "capsule")
                out = PhysicsShapeKind::Capsule;
            else if (s == "model")
                out = PhysicsShapeKind::Model;
            else
                return false;
            return true;
        }

        const char* bodyName(PhysicsBodyMode mode)
        {
            switch (mode)
            {
            case PhysicsBodyMode::Kinematic:
            case PhysicsBodyMode::Mover:
                return "kinematic";
            case PhysicsBodyMode::Dynamic:
                return "dynamic";
            case PhysicsBodyMode::Static:
            case PhysicsBodyMode::None:
            default:
                return "static";
            }
        }

        const char* shapeName(PhysicsShapeKind shape)
        {
            switch (shape)
            {
            case PhysicsShapeKind::Sphere:
                return "sphere";
            case PhysicsShapeKind::Capsule:
                return "capsule";
            case PhysicsShapeKind::Model:
                return "model";
            case PhysicsShapeKind::Box:
            default:
                return "box";
            }
        }

        std::filesystem::path withPhysicsName(const std::filesystem::path& modelPath)
        {
            std::filesystem::path file = modelPath.filename();
            file.replace_filename(modelPath.stem().string() + ".physics.json");
            return file;
        }
    } // namespace

    std::filesystem::path physicsSidecarRelative(const std::filesystem::path& modelSource)
    {
        std::filesystem::path tail;
        bool                  seenContent = false;
        for (const std::filesystem::path& part : modelSource)
        {
            if (!seenContent)
            {
                if (part == "content")
                    seenContent = true;
                continue;
            }
            tail /= part;
        }
        if (!seenContent || tail.empty())
            return withPhysicsName(modelSource);
        tail.replace_filename(modelSource.stem().string() + ".physics.json");
        return tail;
    }

    std::filesystem::path physicsSidecarBesideModel(const std::filesystem::path& modelSource)
    {
        if (modelSource.empty())
            return {};
        std::filesystem::path beside = modelSource;
        beside.replace_filename(modelSource.stem().string() + ".physics.json");
        return beside;
    }

    std::filesystem::path physicsSidecarAuthoringPath(const std::filesystem::path& modelSource)
    {
        const std::filesystem::path relative = physicsSidecarRelative(modelSource);
        const std::filesystem::path root     = authoringContentRoot();
        if (!root.empty() && relative.has_parent_path())
            return root / relative;
        if (!root.empty() && !relative.empty())
        {
            // Filename only: still prefer the authoring models folder when we can see one.
            return root / "models" / relative;
        }
        return physicsSidecarBesideModel(modelSource);
    }

    bool parsePhysicsSettings(std::string_view jsonText, PhysicsComponent& out)
    {
        if (jsonText.empty() || jsonText.size() > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsFile: empty or oversized json");
            return false;
        }

        const json root = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsFile: discarded or non-object json");
            return false;
        }

        const auto verIt = root.find("version");
        if (verIt != root.end())
        {
            float ver = 0.0f;
            if (!jsonToFloat(*verIt, ver) || static_cast<int>(ver) != 1)
            {
                DE_LOG_ERROR(LogCategory::Collision, "PhysicsFile: unsupported version");
                return false;
            }
        }

        PhysicsComponent parsed;
        readBool(root, "enabled", parsed.enabled);

        std::string body;
        if (readString(root, "body", body))
        {
            if (!parseBody(body, parsed.mode))
            {
                DE_LOG_ERROR(LogCategory::Collision, "PhysicsFile: unknown body '{}'", body);
                return false;
            }
        }

        std::string shape;
        if (readString(root, "shape", shape))
        {
            if (!parseShape(shape, parsed.shape))
            {
                DE_LOG_ERROR(LogCategory::Collision, "PhysicsFile: unknown shape '{}'", shape);
                return false;
            }
        }

        readFloat(root, "density", parsed.density);
        readFloat(root, "friction", parsed.friction);
        readFloat(root, "restitution", parsed.restitution);
        readFloat(root, "linearDamping", parsed.linearDamping);
        readFloat(root, "angularDamping", parsed.angularDamping);
        readFloat(root, "gravityScale", parsed.gravityScale);
        readBool(root, "sensor", parsed.sensor);
        readBool(root, "fixedRotation", parsed.fixedRotation);
        readString(root, "surface", parsed.surface);

        if (!finiteNonNegative(parsed.density) || !finiteNonNegative(parsed.friction) || !finiteNonNegative(parsed.restitution)
            || !finiteNonNegative(parsed.linearDamping) || !finiteNonNegative(parsed.angularDamping) || !finiteAny(parsed.gravityScale))
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsFile: non-finite physics number");
            return false;
        }
        if (parsed.surface.size() > 64)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsFile: surface name too long");
            return false;
        }

        out = std::move(parsed);
        return true;
    }

    std::string writePhysicsSettings(const PhysicsComponent& settings)
    {
        json root;
        root["version"]        = 1;
        root["enabled"]        = settings.enabled;
        root["body"]           = bodyName(settings.mode);
        root["shape"]          = shapeName(settings.shape);
        root["density"]        = settings.density;
        root["friction"]       = settings.friction;
        root["restitution"]    = settings.restitution;
        root["linearDamping"]  = settings.linearDamping;
        root["angularDamping"] = settings.angularDamping;
        root["gravityScale"]   = settings.gravityScale;
        root["sensor"]         = settings.sensor;
        root["fixedRotation"]  = settings.fixedRotation;
        root["surface"]        = settings.surface;
        return root.dump(2);
    }

    bool loadPhysicsSettingsFile(const std::filesystem::path& path, PhysicsComponent& out)
    {
        std::error_code ec;
        if (path.empty() || !std::filesystem::is_regular_file(path, ec) || ec)
            return false;
        const auto sz = std::filesystem::file_size(path, ec);
        if (ec || sz > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsFile: unreadable or too large '{}'", path.string());
            return false;
        }

        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsFile: failed to open '{}'", path.string());
            return false;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        if (!parsePhysicsSettings(ss.str(), out))
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsFile: parse failed '{}'", path.string());
            return false;
        }
        return true;
    }

    bool savePhysicsSettingsFile(const std::filesystem::path& path, const PhysicsComponent& settings)
    {
        if (path.empty())
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsFile: empty save path");
            return false;
        }
        std::error_code ec;
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsFile: cannot create '{}' ({})", path.parent_path().string(), ec.message());
            return false;
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsFile: failed to write '{}'", path.string());
            return false;
        }
        out << writePhysicsSettings(settings) << '\n';
        if (!out)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsFile: write failed '{}'", path.string());
            return false;
        }
        return true;
    }

    bool loadPhysicsSettingsForModel(const std::filesystem::path& modelSource, PhysicsComponent& out)
    {
        if (modelSource.empty())
            return false;
        const std::filesystem::path authored = physicsSidecarAuthoringPath(modelSource);
        if (loadPhysicsSettingsFile(authored, out))
            return true;
        const std::filesystem::path beside = physicsSidecarBesideModel(modelSource);
        if (beside != authored && loadPhysicsSettingsFile(beside, out))
            return true;
        return false;
    }
} // namespace Dark::Physics
