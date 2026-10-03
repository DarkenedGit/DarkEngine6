#include "Save/SaveJson.h"

#include "Core/Log.h"

#include <cmath>
#include <limits>

namespace Dark::Save
{
    bool readJsonNumber(const nlohmann::ordered_json& node, double& out)
    {
        if (const auto* number = node.get_ptr<const double*>())
        {
            if (!std::isfinite(*number))
                return false;
            out = *number;
            return true;
        }
        if (const auto* number = node.get_ptr<const int64_t*>())
        {
            out = static_cast<double>(*number);
            return true;
        }
        if (const auto* number = node.get_ptr<const uint64_t*>())
        {
            out = static_cast<double>(*number);
            return true;
        }
        return false;
    }

    namespace
    {
        void noteBadFloat(int* counter)
        {
            DE_LOG_WARN("Save: non-finite float replaced with 0");
            if (counter)
                ++(*counter);
        }
    } // namespace

    SaveWriter::SaveWriter(nlohmann::ordered_json& obj, const std::unordered_map<uint32_t, uint64_t>* entityToPid) :
        m_obj(&obj),
        m_toPid(entityToPid)
    {
    }

    void SaveWriter::f32(const char* key, float value)
    {
        f64(key, static_cast<double>(value));
    }

    void SaveWriter::f64(const char* key, double value)
    {
        if (!m_obj || !key)
        {
            m_ok = false;
            return;
        }
        if (!std::isfinite(value))
        {
            noteBadFloat(m_sanitized);
            value = 0.0;
        }
        (*m_obj)[key] = value;
    }

    void SaveWriter::i32(const char* key, int value)
    {
        if (!m_obj || !key)
        {
            m_ok = false;
            return;
        }
        (*m_obj)[key] = value;
    }

    void SaveWriter::boolean(const char* key, bool value)
    {
        if (!m_obj || !key)
        {
            m_ok = false;
            return;
        }
        (*m_obj)[key] = value;
    }

    void SaveWriter::string(const char* key, std::string_view value)
    {
        if (!m_obj || !key)
        {
            m_ok = false;
            return;
        }
        (*m_obj)[key] = std::string(value);
    }

    void SaveWriter::vec3(const char* key, const Math::Vector3f& value)
    {
        if (!m_obj || !key)
        {
            m_ok = false;
            return;
        }
        float x = value.x;
        float y = value.y;
        float z = value.z;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        {
            noteBadFloat(m_sanitized);
            if (!std::isfinite(x))
                x = 0.0f;
            if (!std::isfinite(y))
                y = 0.0f;
            if (!std::isfinite(z))
                z = 0.0f;
        }
        (*m_obj)[key] = nlohmann::ordered_json::array({ x, y, z });
    }

    void SaveWriter::quat(const char* key, const Math::Quaternion& value)
    {
        if (!m_obj || !key)
        {
            m_ok = false;
            return;
        }
        float w = value.w;
        float x = value.x;
        float y = value.y;
        float z = value.z;
        if (!std::isfinite(w) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        {
            noteBadFloat(m_sanitized);
            w = 1.0f;
            x = 0.0f;
            y = 0.0f;
            z = 0.0f;
        }
        (*m_obj)[key] = nlohmann::ordered_json::array({ w, x, y, z });
    }

    void SaveWriter::entity(const char* key, Entity value)
    {
        if (!m_obj || !key)
        {
            m_ok = false;
            return;
        }
        if (!value.valid() || !m_toPid)
        {
            nullValue(key);
            return;
        }
        const auto it = m_toPid->find(value.id());
        if (it == m_toPid->end())
        {
            nullValue(key);
            return;
        }
        (*m_obj)[key] = toHex16(it->second);
    }

    void SaveWriter::nullValue(const char* key)
    {
        if (!m_obj || !key)
        {
            m_ok = false;
            return;
        }
        (*m_obj)[key] = nullptr;
    }

    bool SaveWriter::arrayObjects(const char* key, int count, void (*writeOne)(SaveWriter& item, int index, void* user), void* user)
    {
        if (!m_obj || !key || !writeOne || count < 0)
        {
            m_ok = false;
            return false;
        }
        nlohmann::ordered_json arr = nlohmann::ordered_json::array();
        for (int i = 0; i < count; ++i)
        {
            nlohmann::ordered_json item = nlohmann::ordered_json::object();
            SaveWriter             child(item, m_toPid);
            child.m_sanitized = m_sanitized;
            writeOne(child, i, user);
            if (!child.m_ok)
                m_ok = false;
            arr.push_back(std::move(item));
        }
        (*m_obj)[key] = std::move(arr);
        return m_ok;
    }

    SaveReader::SaveReader(const nlohmann::ordered_json& obj, const std::unordered_map<uint64_t, Entity>* pidToEntity) :
        m_obj(&obj),
        m_toEntity(pidToEntity)
    {
        if (!obj.is_object())
            m_failed = true;
    }

    bool SaveReader::f32(const char* key, float& io, float lo, float hi)
    {
        double wide = static_cast<double>(io);
        if (!f64(key, wide, static_cast<double>(lo), static_cast<double>(hi)))
            return false;
        if (wide < static_cast<double>(std::numeric_limits<float>::lowest()) || wide > static_cast<double>(std::numeric_limits<float>::max()))
        {
            m_failed = true;
            return false;
        }
        io = static_cast<float>(wide);
        return true;
    }

    bool SaveReader::f64(const char* key, double& io, double lo, double hi)
    {
        if (m_failed || !m_obj || !key)
            return false;
        const auto it = m_obj->find(key);
        if (it == m_obj->end())
            return true;
        double value = 0.0;
        if (!readJsonNumber(*it, value) || value < lo || value > hi)
        {
            m_failed = true;
            return false;
        }
        io = value;
        return true;
    }

    bool SaveReader::i32(const char* key, int& io, int lo, int hi)
    {
        if (m_failed || !m_obj || !key)
            return false;
        const auto it = m_obj->find(key);
        if (it == m_obj->end())
            return true;
        double value = 0.0;
        if (!readJsonNumber(*it, value))
        {
            m_failed = true;
            return false;
        }
        const double truncated = std::trunc(value);
        if (truncated != value || truncated < static_cast<double>(lo) || truncated > static_cast<double>(hi))
        {
            m_failed = true;
            return false;
        }
        io = static_cast<int>(truncated);
        return true;
    }

    bool SaveReader::boolean(const char* key, bool& io)
    {
        if (m_failed || !m_obj || !key)
            return false;
        const auto it = m_obj->find(key);
        if (it == m_obj->end())
            return true;
        const auto* flag = it->get_ptr<const bool*>();
        if (!flag)
        {
            m_failed = true;
            return false;
        }
        io = *flag;
        return true;
    }

    bool SaveReader::string(const char* key, std::string& io, int maxBytes)
    {
        if (m_failed || !m_obj || !key)
            return false;
        const auto it = m_obj->find(key);
        if (it == m_obj->end())
            return true;
        const auto* text = it->get_ptr<const std::string*>();
        if (!text || static_cast<int>(text->size()) > maxBytes)
        {
            m_failed = true;
            return false;
        }
        io = *text;
        return true;
    }

    bool SaveReader::vec3(const char* key, Math::Vector3f& io, float absMax)
    {
        if (m_failed || !m_obj || !key)
            return false;
        const auto it = m_obj->find(key);
        if (it == m_obj->end())
            return true;
        if (!it->is_array() || it->size() != 3)
        {
            m_failed = true;
            return false;
        }
        float next[3] = {};
        for (int i = 0; i < 3; ++i)
        {
            double value = 0.0;
            const auto el = it->begin() + i;
            if (!readJsonNumber(*el, value) || value < static_cast<double>(-absMax) || value > static_cast<double>(absMax))
            {
                m_failed = true;
                return false;
            }
            next[i] = static_cast<float>(value);
        }
        io.x = next[0];
        io.y = next[1];
        io.z = next[2];
        return true;
    }

    bool SaveReader::quat(const char* key, Math::Quaternion& io)
    {
        if (m_failed || !m_obj || !key)
            return false;
        const auto it = m_obj->find(key);
        if (it == m_obj->end())
            return true;
        if (!it->is_array() || it->size() != 4)
        {
            m_failed = true;
            return false;
        }
        float next[4] = {};
        const float absMax = kMaxAbsPosition;
        for (int i = 0; i < 4; ++i)
        {
            double value = 0.0;
            const auto el = it->begin() + i;
            if (!readJsonNumber(*el, value) || value < static_cast<double>(-absMax) || value > static_cast<double>(absMax))
            {
                m_failed = true;
                return false;
            }
            next[i] = static_cast<float>(value);
        }
        const float lengthSq = next[0] * next[0] + next[1] * next[1] + next[2] * next[2] + next[3] * next[3];
        if (!(lengthSq > 1.0e-8f))
        {
            m_failed = true;
            return false;
        }
        Math::Quaternion q(next[0], next[1], next[2], next[3]);
        q.Normalize();
        if (!std::isfinite(q.w) || !std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z))
        {
            m_failed = true;
            return false;
        }
        io = q;
        return true;
    }

    bool SaveReader::entityRef(const char* key, Entity& out)
    {
        if (m_failed || !m_obj || !key)
            return false;
        const auto it = m_obj->find(key);
        if (it == m_obj->end())
            return true;
        if (it->is_null())
        {
            out = Entity{};
            return true;
        }
        const auto* text = it->get_ptr<const std::string*>();
        uint64_t    pid  = 0;
        if (!text || !fromHex16(*text, pid))
        {
            m_failed = true;
            return false;
        }
        if (!m_toEntity)
        {
            out = Entity{};
            ++m_unresolved;
            return true;
        }
        const auto found = m_toEntity->find(pid);
        if (found == m_toEntity->end())
        {
            out = Entity{};
            ++m_unresolved;
            return true;
        }
        out = found->second;
        return true;
    }

    bool SaveReader::arrayObjects(const char* key, int maxCount, int& outCount, void (*readOne)(SaveReader& item, int index, void* user), void* user)
    {
        if (m_failed || !m_obj || !key || !readOne || maxCount < 0)
        {
            m_failed = true;
            return false;
        }
        const auto it = m_obj->find(key);
        if (it == m_obj->end())
        {
            outCount = -1;
            return true;
        }
        if (!it->is_array() || it->size() > static_cast<size_t>(maxCount))
        {
            m_failed = true;
            return false;
        }
        outCount = static_cast<int>(it->size());
        for (int i = 0; i < outCount; ++i)
        {
            const auto& el = *(it->begin() + i);
            if (!el.is_object())
            {
                m_failed = true;
                return false;
            }
            SaveReader child(el, m_toEntity);
            readOne(child, i, user);
            m_unresolved += child.m_unresolved;
            if (child.m_failed)
            {
                m_failed = true;
                return false;
            }
        }
        return true;
    }

    bool parseDocument(std::string_view text, nlohmann::ordered_json& out, SaveResult& error)
    {
        bool tooDeep = false;
        auto callback = [&tooDeep](int depth, nlohmann::ordered_json::parse_event_t event, nlohmann::ordered_json&) {
            (void)event;
            if (depth > kMaxJsonDepth)
            {
                tooDeep = true;
                return false;
            }
            return true;
        };
        out = nlohmann::ordered_json::parse(text, callback, false);
        if (tooDeep)
        {
            error = SaveResult::LimitExceeded;
            return false;
        }
        if (out.is_discarded())
        {
            error = SaveResult::ParseFailed;
            return false;
        }
        if (!out.is_object())
        {
            error = SaveResult::NotObject;
            return false;
        }
        error = SaveResult::Ok;
        return true;
    }

} // namespace Dark::Save
