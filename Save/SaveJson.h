#pragma once

#include "ECS/Entity.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Save/SaveTypes.h"

#include "third_party/nlohmann/json.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Dark::Save
{
    class SaveWriter
    {
    public:
        SaveWriter(nlohmann::ordered_json& obj, const std::unordered_map<uint32_t, uint64_t>* entityToPid);

        void f32(const char* key, float value);
        void f64(const char* key, double value);
        void i32(const char* key, int value);
        void boolean(const char* key, bool value);
        void string(const char* key, std::string_view value);
        void vec3(const char* key, const Math::Vector3f& value);
        void quat(const char* key, const Math::Quaternion& value);
        void entity(const char* key, Entity value);
        void nullValue(const char* key);

        bool arrayObjects(const char* key, int count, void (*writeOne)(SaveWriter& item, int index, void* user), void* user);

        bool ok() const { return m_ok; }
        int  sanitized() const { return m_sanitized ? *m_sanitized : 0; }
        void shareSanitize(int* counter) { m_sanitized = counter; }

    private:
        nlohmann::ordered_json*                       m_obj = nullptr;
        const std::unordered_map<uint32_t, uint64_t>* m_toPid = nullptr;
        int*                                          m_sanitized = nullptr;
        bool                                          m_ok        = true;
    };

    class SaveReader
    {
    public:
        SaveReader(const nlohmann::ordered_json& obj, const std::unordered_map<uint64_t, Entity>* pidToEntity);

        // Missing key keeps `io` and returns true. A present value of the wrong type or out of range returns false.
        bool f32(const char* key, float& io, float lo, float hi);
        bool f64(const char* key, double& io, double lo, double hi);
        bool i32(const char* key, int& io, int lo, int hi);
        bool boolean(const char* key, bool& io);
        bool string(const char* key, std::string& io, int maxBytes);
        bool vec3(const char* key, Math::Vector3f& io, float absMax);
        bool quat(const char* key, Math::Quaternion& io);
        bool entityRef(const char* key, Entity& out);
        bool arrayObjects(const char* key, int maxCount, int& outCount, void (*readOne)(SaveReader& item, int index, void* user), void* user);

        bool failed() const { return m_failed; }
        int  unresolvedRefs() const { return m_unresolved; }

    private:
        const nlohmann::ordered_json*                 m_obj = nullptr;
        const std::unordered_map<uint64_t, Entity>*   m_toEntity = nullptr;
        bool                                          m_failed     = false;
        int                                           m_unresolved = 0;
    };

    bool parseDocument(std::string_view text, nlohmann::ordered_json& out, SaveResult& error);

    // Finite double, or an integer JSON number. False leaves `out` unchanged.
    bool readJsonNumber(const nlohmann::ordered_json& node, double& out);

} // namespace Dark::Save
