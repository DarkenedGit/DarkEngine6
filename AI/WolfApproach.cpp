#include "AI/WolfApproach.h"

namespace Dark::AI
{
    namespace
    {
        Math::Vector3f sideDirection(const Math::Vector3f& planarForward, WolfApproachSide side)
        {
            // Right-handed: right = world up cross forward. Forward +Z gives right +X.
            const Math::Vector3f right{ planarForward.z, 0.0f, -planarForward.x };
            if (side == WolfApproachSide::Right)
                return right;
            if (side == WolfApproachSide::Left)
                return Math::Vector3f{ -right.x, 0.0f, -right.z };
            return Math::Vector3f{ -planarForward.x, 0.0f, -planarForward.z };
        }
    }

    Math::Vector3f wolfPlanarForward(const Math::Vector3f& forward)
    {
        Math::Vector3f flat{ forward.x, 0.0f, forward.z };
        if (flat.MagnitudeSqrd() <= 1.0e-8f)
            return Math::Vector3f{ 0.0f, 0.0f, 1.0f };
        flat.Normalize();
        return flat;
    }

    Math::Vector3f wolfStagePoint(const Math::Vector3f& player, const Math::Vector3f& playerForward, WolfApproachSide side)
    {
        const Math::Vector3f dir = sideDirection(wolfPlanarForward(playerForward), side);
        return Math::Vector3f{ player.x + dir.x * kWolfStageRadius, 0.0f, player.z + dir.z * kWolfStageRadius };
    }

    WolfApproachPlan planWolfApproach(const WolfNotice& notice)
    {
        WolfApproachPlan plan;
        const uint32_t   pick = notice.salt % 3u;
        plan.side             = pick == 0u ? WolfApproachSide::Left : pick == 1u ? WolfApproachSide::Right : WolfApproachSide::Behind;

        const Math::Vector3f forward = wolfPlanarForward(notice.playerForward);
        plan.staging                 = wolfStagePoint(notice.player, forward, plan.side);

        Math::Vector3f away{ notice.wolf.x - notice.player.x, 0.0f, notice.wolf.z - notice.player.z };
        if (away.MagnitudeSqrd() < 1.0e-6f)
            away = Math::Vector3f{ -forward.x, 0.0f, -forward.z };
        else
            away.Normalize();
        plan.backOff = Math::Vector3f{ notice.wolf.x + away.x * kWolfBackOffDistance, 0.0f, notice.wolf.z + away.z * kWolfBackOffDistance };
        plan.direct  = notice.horizDistance <= kWolfCloseNotice || notice.packCount >= 2;
        return plan;
    }

} // namespace Dark::AI
