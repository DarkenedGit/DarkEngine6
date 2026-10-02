#pragma once

#include "Math/Vector3f.h"

#include <cstdint>

namespace Dark::AI
{
    // A lone wolf outside this distance circles. Inside it, or with a pack nearby, it closes from the front.
    inline constexpr float kWolfCloseNotice     = 4.5f;
    inline constexpr float kWolfPackRadius      = 12.0f;
    inline constexpr float kWolfBackOffDistance = 3.5f;
    inline constexpr float kWolfStageRadius     = 6.5f;
    inline constexpr float kWolfBackOffSeconds  = 0.85f;
    inline constexpr float kWolfBackOffArrive   = 1.2f;
    inline constexpr float kWolfStageArrive     = 2.2f;
    inline constexpr float kWolfFlankSeconds    = 6.0f;

    enum class WolfApproachSide : uint8_t
    {
        Left = 0,
        Right,
        Behind
    };

    enum class WolfStalk : uint8_t
    {
        Inactive = 0,
        BackOff,
        Flank,
        Commit
    };

    struct WolfNotice
    {
        Math::Vector3f wolf{};
        Math::Vector3f player{};
        Math::Vector3f playerForward{ 0.0f, 0.0f, 1.0f };
        float          horizDistance = 0.0f;
        int            packCount     = 1;
        uint32_t       salt          = 0;
    };

    struct WolfApproachPlan
    {
        bool             direct = false;
        WolfApproachSide side   = WolfApproachSide::Behind;
        Math::Vector3f   backOff{};
        Math::Vector3f   staging{};
    };

    // Local +Z flattened. A zero look falls back to world +Z so the stage point stays defined.
    Math::Vector3f wolfPlanarForward(const Math::Vector3f& forward);

    // Point on the player's left, right, or behind, in the horizontal plane.
    Math::Vector3f wolfStagePoint(const Math::Vector3f& player, const Math::Vector3f& playerForward, WolfApproachSide side);

    // salt % 3 picks the side and is meant to stay fixed for one notice.
    WolfApproachPlan planWolfApproach(const WolfNotice& notice);

} // namespace Dark::AI
