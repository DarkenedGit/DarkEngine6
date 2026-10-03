#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Dark::Save
{
    inline constexpr int      kSchema           = 1;
    inline constexpr uint64_t kMaxFileBytes     = 16ull * 1024ull * 1024ull;
    inline constexpr int      kMaxJsonDepth     = 32;
    inline constexpr int      kMaxEntities      = 16384;
    inline constexpr int      kMaxStringBytes   = 256;
    inline constexpr int      kQuickRing        = 3;
    inline constexpr int      kAutoRing         = 5;
    inline constexpr int      kMaxDisplayBytes  = 64;
    inline constexpr float    kMaxAbsPosition   = 1.0e6f;
    inline constexpr float    kMaxTimerSeconds  = 3600.0f;

    enum class SaveKind : uint8_t
    {
        Manual = 0,
        Quick,
        Auto
    };

    enum class SaveResult : uint8_t
    {
        Ok = 0,
        NotHost,
        Busy,
        UnsafeMoment,
        NoSavesDir,
        PathInvalid,
        IoOpenFailed,
        IoWriteFailed,
        IoFlushFailed,
        IoRenameFailed,
        IoReadFailed,
        TooLarge,
        ParseFailed,
        NotObject,
        BadFormat,
        ChecksumMismatch,
        SchemaTooNew,
        MigrationFailed,
        SceneMismatch,
        SceneLoadFailed,
        ComponentInvalid,
        ComponentTooNew,
        DuplicateId,
        LimitExceeded,
        SpawnFailed
    };

    const char* toString(SaveResult result);
    const char* toString(SaveKind kind);
    bool        tryParseKind(std::string_view text, SaveKind& out);

    uint64_t    fnv1a64(const void* data, size_t size);
    std::string toHex16(uint64_t value);
    bool        fromHex16(std::string_view text, uint64_t& out);

} // namespace Dark::Save
