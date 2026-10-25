#include "Core/Json.h"

#include "Core/Log.h"

#include <cmath>

namespace Dark
{
    namespace
    {
        bool rejectType(const char* where, const char* key, const nlohmann::json& value, const char* expected, std::string* error)
        {
            const std::string message = std::string("\"") + key + "\" is a " + value.type_name() + ", expected " + expected;
            if (where != nullptr && where[0] != '\0')
                DE_LOG_ERROR("{}: {}", where, message);
            else
                DE_LOG_ERROR("{}", message);
            if (error != nullptr)
                *error = message;
            return false;
        }
    } // namespace

    bool jsonStringOr(const nlohmann::json& object, const char* key, std::string& out, const std::string& fallback, const char* where, std::string* error)
    {
        if (!object.contains(key))
        {
            out = fallback;
            return true;
        }
        const nlohmann::json& value = object[key];
        if (!value.is_string())
        {
            out = fallback;
            return rejectType(where, key, value, "a string", error);
        }
        out = value.get<std::string>();
        return true;
    }

    bool jsonBoolOr(const nlohmann::json& object, const char* key, bool& out, bool fallback, const char* where, std::string* error)
    {
        if (!object.contains(key))
        {
            out = fallback;
            return true;
        }
        const nlohmann::json& value = object[key];
        if (!value.is_boolean())
        {
            out = fallback;
            return rejectType(where, key, value, "a bool", error);
        }
        out = value.get<bool>();
        return true;
    }

    bool jsonIntOr(const nlohmann::json& object, const char* key, int& out, int fallback, const char* where, std::string* error)
    {
        if (!object.contains(key))
        {
            out = fallback;
            return true;
        }
        const nlohmann::json& value = object[key];
        if (!value.is_number())
        {
            out = fallback;
            return rejectType(where, key, value, "a number", error);
        }
        const double number = value.get<double>();
        if (!std::isfinite(number))
        {
            out = fallback;
            return rejectType(where, key, value, "a finite number", error);
        }
        out = static_cast<int>(number);
        return true;
    }

    bool jsonDoubleOr(const nlohmann::json& object, const char* key, double& out, double fallback, const char* where, std::string* error)
    {
        if (!object.contains(key))
        {
            out = fallback;
            return true;
        }
        const nlohmann::json& value = object[key];
        if (!value.is_number())
        {
            out = fallback;
            return rejectType(where, key, value, "a number", error);
        }
        const double number = value.get<double>();
        if (!std::isfinite(number))
        {
            out = fallback;
            return rejectType(where, key, value, "a finite number", error);
        }
        out = number;
        return true;
    }

    bool jsonFloatOr(const nlohmann::json& object, const char* key, float& out, float fallback, const char* where, std::string* error)
    {
        double number = fallback;
        if (!jsonDoubleOr(object, key, number, fallback, where, error))
        {
            out = fallback;
            return false;
        }
        out = static_cast<float>(number);
        return true;
    }

} // namespace Dark
