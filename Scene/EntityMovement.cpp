#include "Scene/EntityMovement.h"

#include "Core/Log.h"
#include "Scene/EntityMaster.h"

#include "third_party/nlohmann/json.hpp"

#include <cmath>
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

        // False when the key is absent or the value is not a usable number. `dest` changes only on success.
        bool takeFloat(const json& obj, const char* key, float& dest, bool positive)
        {
            const auto it = obj.find(key);
            if (it == obj.end())
                return false;
            float v = 0.0f;
            if (!jsonToFloat(*it, v) || !std::isfinite(v) || v < 0.0f || (positive && !(v > 0.0f)))
            {
                DE_LOG_WARN(LogCategory::Core, "EntityMovement: ignoring '{}'", key);
                return false;
            }
            dest = v;
            return true;
        }

        float heightFromSpeed(float speed, float gravity)
        {
            if (!(gravity > 0.0f) || !std::isfinite(speed) || speed < 0.0f)
                return 0.0f;
            return (speed * speed) / (2.0f * gravity);
        }

        void applyHeight(bool heightSet, float height, float& speed, float& storedHeight, float gravity)
        {
            if (heightSet && gravity > 0.0f)
            {
                speed        = std::sqrt(2.0f * gravity * height);
                storedHeight = height;
                return;
            }
            storedHeight = heightFromSpeed(speed, gravity);
        }
    } // namespace

    std::string entityMovementVirtualPath(std::string_view typeName)
    {
        return std::string("entities/") + std::string(typeName) + ".entity.movement.json";
    }

    bool parseEntityMovement(std::string_view jsonText, EntityMovement& inOut)
    {
        if (jsonText.empty() || jsonText.size() > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::Core, "EntityMovement: empty or oversized json");
            return false;
        }
        const json root = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            DE_LOG_ERROR(LogCategory::Core, "EntityMovement: discarded or non-object json");
            return false;
        }

        const auto verIt = root.find("version");
        if (verIt != root.end())
        {
            float ver = 0.0f;
            if (!jsonToFloat(*verIt, ver) || static_cast<int>(ver) != 1)
            {
                DE_LOG_ERROR(LogCategory::Core, "EntityMovement: unsupported version");
                return false;
            }
        }

        EntityMovement parsed = inOut;
        if (!readString(root, "type", parsed.type) || parsed.type.empty())
        {
            DE_LOG_ERROR(LogCategory::Core, "EntityMovement: missing type");
            return false;
        }

        takeFloat(root, "gravity", parsed.gravity, true);
        takeFloat(root, "walkSpeed", parsed.walkSpeed, false);
        takeFloat(root, "runSpeed", parsed.runSpeed, false);
        takeFloat(root, "acceleration", parsed.acceleration, false);
        takeFloat(root, "leapForwardSpeed", parsed.leapForwardSpeed, false);
        takeFloat(root, "leapForwardSpeedMax", parsed.leapForwardSpeedMax, false);

        takeFloat(root, "jumpSpeed", parsed.jumpSpeed, false);
        float      jumpHeight    = 0.0f;
        const bool jumpHeightSet = takeFloat(root, "jumpHeight", jumpHeight, false);
        applyHeight(jumpHeightSet, jumpHeight, parsed.jumpSpeed, parsed.jumpHeight, parsed.gravity);

        takeFloat(root, "doubleJumpSpeed", parsed.doubleJumpSpeed, false);
        float      doubleHeight    = 0.0f;
        const bool doubleHeightSet = takeFloat(root, "doubleJumpHeight", doubleHeight, false);
        applyHeight(doubleHeightSet, doubleHeight, parsed.doubleJumpSpeed, parsed.doubleJumpHeight, parsed.gravity);

        inOut = std::move(parsed);
        return true;
    }

    bool loadEntityMovementFile(const std::filesystem::path& path, EntityMovement& inOut)
    {
        std::error_code ec;
        if (path.empty() || !std::filesystem::is_regular_file(path, ec) || ec)
            return false;
        const auto sz = std::filesystem::file_size(path, ec);
        if (ec || sz > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::Core, "EntityMovement: unreadable or too large '{}'", path.string());
            return false;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        return parseEntityMovement(ss.str(), inOut);
    }

    bool loadEntityMovementType(std::string_view typeName, EntityMovement& inOut)
    {
        if (typeName.empty())
            return false;
        const std::filesystem::path path = resolveContentFile(entityMovementVirtualPath(typeName));
        if (path.empty())
            return false;
        EntityMovement parsed = inOut;
        if (!loadEntityMovementFile(path, parsed))
            return false;
        if (parsed.type != typeName)
        {
            DE_LOG_ERROR(LogCategory::Core, "EntityMovement: '{}' type is '{}'", path.string(), parsed.type);
            return false;
        }
        DE_LOG_INFO(LogCategory::Core, "EntityMovement: {} walk {} run {} accel {} jump {}", parsed.type, parsed.walkSpeed, parsed.runSpeed, parsed.acceleration, parsed.jumpSpeed);
        inOut = std::move(parsed);
        return true;
    }

} // namespace Dark
