#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Dark
{

    // One locomotion record per entity type. Speeds are m/s and heights are meters.
    // A successful parse keeps any field the json omits, so callers seed C++ defaults first.
    // jumpHeight, when present, replaces jumpSpeed after gravity is known: speed = sqrt(2 * g * h).
    // doubleJumpHeight uses the same rule. When a height key is absent, the height is filled from its speed.
    struct EntityMovement
    {
        int         version = 1;
        std::string type;
        float       walkSpeed           = 0.0f;
        float       runSpeed            = 0.0f;
        float       acceleration        = 0.0f; // m/s^2 toward walk and run. 0 snaps.
        float       gravity             = 24.0f;
        float       jumpSpeed           = 0.0f;
        float       jumpHeight          = 0.0f;
        float       doubleJumpSpeed     = 0.0f;
        float       doubleJumpHeight    = 0.0f;
        float       leapForwardSpeed    = 0.0f;
        float       leapForwardSpeedMax = 0.0f;
    };

    // "player" -> "entities/player.entity.movement.json"
    std::string entityMovementVirtualPath(std::string_view typeName);

    // nlohmann::json::parse(..., false). On failure `inOut` is unchanged.
    bool parseEntityMovement(std::string_view jsonText, EntityMovement& inOut);
    bool loadEntityMovementFile(const std::filesystem::path& path, EntityMovement& inOut);

    // Missing file, bad json, or a type that does not match `typeName` leaves `inOut` unchanged.
    bool loadEntityMovementType(std::string_view typeName, EntityMovement& inOut);

} // namespace Dark
