#include "Physics/CollisionShape.h"

namespace Dark::Physics
{
    CollisionShape::CollisionShape()
    {
        type = AssetType::CollisionShape;
    }

    void CollisionShape::clear()
    {
        m_parts.clear();
        m_bounds    = Math::AABox3f::Empty();
        m_sourcePath.clear();
        m_bodyHint = CollisionBodyHint::Static;
        id         = NULL_ASSET;
        type       = AssetType::CollisionShape;
    }

    void CollisionShape::addPart(CollisionPart part)
    {
        expandBoundsFromPart(part);
        m_parts.push_back(std::move(part));
    }

    void CollisionShape::expandBoundsFromPart(const CollisionPart& part)
    {
        auto includePoint = [this](const Math::Vector3f& p) { m_bounds.ExpandToInclude(p); };
        auto includeSphere = [&](const Math::Vector3f& c, float r) {
            includePoint(Math::Vector3f(c.x - r, c.y - r, c.z - r));
            includePoint(Math::Vector3f(c.x + r, c.y + r, c.z + r));
        };

        switch (part.geom)
        {
        case CollisionGeom::Sphere:
            includeSphere(part.localPos, part.radius);
            break;
        case CollisionGeom::Box:
            includePoint(part.localPos - part.halfExtents);
            includePoint(part.localPos + part.halfExtents);
            break;
        case CollisionGeom::Capsule:
            includeSphere(part.localPos + part.capsuleA, part.radius);
            includeSphere(part.localPos + part.capsuleB, part.radius);
            break;
        case CollisionGeom::Hull:
            for (const Math::Vector3f& p : part.hullPoints)
                includePoint(p);
            break;
        case CollisionGeom::Mesh:
            for (const Math::Vector3f& p : part.meshPositions)
                includePoint(p);
            break;
        default:
            break;
        }
    }
} // namespace Dark::Physics
