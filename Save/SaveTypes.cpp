#include "Save/SaveTypes.h"

#include <format>

namespace Dark::Save
{

    const char* toString(SaveResult result)
    {
        switch (result)
        {
        case SaveResult::Ok:                return "Ok";
        case SaveResult::NotHost:           return "NotHost";
        case SaveResult::Busy:              return "Busy";
        case SaveResult::UnsafeMoment:      return "UnsafeMoment";
        case SaveResult::NoSavesDir:        return "NoSavesDir";
        case SaveResult::PathInvalid:       return "PathInvalid";
        case SaveResult::IoOpenFailed:      return "IoOpenFailed";
        case SaveResult::IoWriteFailed:     return "IoWriteFailed";
        case SaveResult::IoFlushFailed:     return "IoFlushFailed";
        case SaveResult::IoRenameFailed:    return "IoRenameFailed";
        case SaveResult::IoReadFailed:      return "IoReadFailed";
        case SaveResult::TooLarge:          return "TooLarge";
        case SaveResult::ParseFailed:       return "ParseFailed";
        case SaveResult::NotObject:         return "NotObject";
        case SaveResult::BadFormat:         return "BadFormat";
        case SaveResult::ChecksumMismatch:  return "ChecksumMismatch";
        case SaveResult::SchemaTooNew:      return "SchemaTooNew";
        case SaveResult::MigrationFailed:   return "MigrationFailed";
        case SaveResult::SceneMismatch:     return "SceneMismatch";
        case SaveResult::SceneLoadFailed:   return "SceneLoadFailed";
        case SaveResult::ComponentInvalid:  return "ComponentInvalid";
        case SaveResult::ComponentTooNew:   return "ComponentTooNew";
        case SaveResult::DuplicateId:       return "DuplicateId";
        case SaveResult::LimitExceeded:     return "LimitExceeded";
        case SaveResult::SpawnFailed:       return "SpawnFailed";
        }
        return "Unknown";
    }

    const char* toString(SaveKind kind)
    {
        switch (kind)
        {
        case SaveKind::Manual: return "manual";
        case SaveKind::Quick:  return "quick";
        case SaveKind::Auto:   return "auto";
        }
        return "manual";
    }

    bool tryParseKind(std::string_view text, SaveKind& out)
    {
        if (text == "manual" || text == "save")
        {
            out = SaveKind::Manual;
            return true;
        }
        if (text == "quick")
        {
            out = SaveKind::Quick;
            return true;
        }
        if (text == "auto")
        {
            out = SaveKind::Auto;
            return true;
        }
        return false;
    }

    uint64_t fnv1a64(const void* data, size_t size)
    {
        constexpr uint64_t kOffset = 14695981039346656037ull;
        constexpr uint64_t kPrime  = 1099511628211ull;
        uint64_t           hash    = kOffset;
        const auto*        bytes   = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= kPrime;
        }
        return hash;
    }

    std::string toHex16(uint64_t value)
    {
        return std::format("{:016x}", value);
    }

    bool fromHex16(std::string_view text, uint64_t& out)
    {
        if (text.size() != 16)
            return false;
        uint64_t value = 0;
        for (char c : text)
        {
            uint64_t nibble = 0;
            if (c >= '0' && c <= '9')
                nibble = static_cast<uint64_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                nibble = static_cast<uint64_t>(c - 'a' + 10);
            else
                return false;
            value = (value << 4) | nibble;
        }
        out = value;
        return true;
    }

} // namespace Dark::Save
