#pragma once

#include "Assets/AssetHandle.h"
#include "Math/AABox3f.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Dark::Physics
{
    enum class CollisionGeom : uint8_t
    {
        None = 0,
        Sphere,
        Capsule,
        Box,
        Hull,
        Mesh,
        HeightField,
        Compound,
    };

    enum class CollisionBodyHint : uint8_t
    {
        Static = 0,
        Kinematic,
        Dynamic,
    };

    enum class PhysicsLayer : uint64_t
    {
        World      = 1ull << 0,
        Player     = 1ull << 1,
        Npc        = 1ull << 2,
        Projectile = 1ull << 3,
        Trigger    = 1ull << 4,
        Debris     = 1ull << 5,
        Pickup     = 1ull << 6,
    };

    struct CollisionPart
    {
        CollisionGeom    geom = CollisionGeom::Box;
        Math::Vector3f   localPos{0.0f, 0.0f, 0.0f};
        Math::Quaternion localRot{};
        Math::Vector3f   localScale{1.0f, 1.0f, 1.0f};
        float            radius = 0.5f;
        Math::Vector3f   halfExtents{0.5f, 0.5f, 0.5f};
        Math::Vector3f   capsuleA{0.0f, 0.0f, 0.0f};
        Math::Vector3f   capsuleB{0.0f, 0.9f, 0.0f};
        uint32_t         surfaceId = 0;
        bool             sensor    = false;
        uint64_t         categoryBits = 0;
        uint64_t         maskBits     = 0;
        int              maxHullVerts = 32;
        std::string      nodeName;
        std::vector<Math::Vector3f> hullPoints;
        std::vector<Math::Vector3f> meshPositions;
        std::vector<uint32_t>       meshIndices;
    };

    class CollisionShape : public Asset
    {
    public:
        CollisionShape();

        bool valid() const { return !m_parts.empty(); }
        const std::vector<CollisionPart>& parts() const { return m_parts; }
        const Math::AABox3f& bounds() const { return m_bounds; }
        const std::filesystem::path& sourcePath() const { return m_sourcePath; }
        CollisionBodyHint bodyHint() const { return m_bodyHint; }

        void clear();
        void addPart(CollisionPart part);
        void setBodyHint(CollisionBodyHint hint) { m_bodyHint = hint; }
        void setSourcePath(std::filesystem::path path) { m_sourcePath = std::move(path); }

    private:
        void expandBoundsFromPart(const CollisionPart& part);

        std::vector<CollisionPart> m_parts;
        Math::AABox3f              m_bounds    = Math::AABox3f::Empty();
        std::filesystem::path      m_sourcePath;
        CollisionBodyHint          m_bodyHint = CollisionBodyHint::Static;
    };
} // namespace Dark::Physics
