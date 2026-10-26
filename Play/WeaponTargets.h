#pragma once

#include "Combat/HitVolumes.h"
#include "ECS/Entity.h"
#include "Math/Vector3f.h"
#include "Weapons/Weapon.h"

#include <vector>

namespace Dark
{

    class World;

    struct WeaponTarget
    {
        Entity         entity{};
        Math::Vector3f center{};
        Math::Vector3f halfExtents{ 1.0f, 1.0f, 1.0f };
        bool           alive       = false;
        bool           hasCapsules = false;
        Combat::HitSegment segments[Combat::HitVolumesComponent::kSlotCount]{};
    };

    // Hittable bodies for one weapon query. Terrain callbacks stay with the host.
    class WeaponTargetList
    {
    public:
        void gather(World& world, Entity ignore);
        void bind(WeaponWorldQuery& query);

        const std::vector<WeaponTarget>& targets() const { return m_targets; }
        Entity                           entityAt(int index) const;

    private:
        std::vector<WeaponTarget> m_targets;
    };

} // namespace Dark
