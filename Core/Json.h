#pragma once

#include "third_party/nlohmann/json.hpp"

#include <string>

namespace Dark
{

    // nlohmann value()/get<T>() aborts on a type mismatch when exceptions are disabled.
    // A missing key writes fallback and returns true. A wrong type logs
    // `{where}: "{key}" is a {type}, expected ...`, writes fallback, stores that detail
    // in `error` when it is non-null, and returns false.
    bool jsonStringOr(const nlohmann::json& object, const char* key, std::string& out, const std::string& fallback, const char* where,
                      std::string* error = nullptr);
    bool jsonBoolOr(const nlohmann::json& object, const char* key, bool& out, bool fallback, const char* where, std::string* error = nullptr);
    bool jsonIntOr(const nlohmann::json& object, const char* key, int& out, int fallback, const char* where, std::string* error = nullptr);

} // namespace Dark
