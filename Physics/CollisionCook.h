#pragma once

#include "Physics/CollisionShape.h"
#include "Physics/PhysicsSurface.h"
#include "Math/AABox3f.h"
#include "Math/Vector3f.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{
    class AssetManager;
    class Model;
}

namespace Dark::Physics
{
    inline constexpr float kPawnCapsuleRadius  = 0.45f;
    inline constexpr float kPawnDefaultHeight  = 1.8f;
    inline constexpr int   kDefaultMaxHullVerts = 32;

    enum class CollisionCookKind : uint8_t
    {
        Cube = 0,
        Sphere,
        Pawn,
        Model,
    };

    struct CollisionSidecarPart
    {
        std::string node;
        std::string geom; // "auto"|"box"|"sphere"|"capsule"|"hull"|"mesh"
        std::string surface;
        std::string category;
        bool        sensor   = false;
        int         maxVerts = kDefaultMaxHullVerts;
    };

    struct CollisionSidecar
    {
        int                              version = 1;
        std::string                      body    = "static";
        std::vector<CollisionSidecarPart> parts;
    };

    struct CollisionCookDesc
    {
        CollisionCookKind            kind  = CollisionCookKind::Model;
        Math::Vector3f               scale{1.0f, 1.0f, 1.0f};
        Math::AABox3f                bounds = Math::AABox3f::Empty();
        const Model*                 model  = nullptr;
        std::string                  sidecarJson;
        bool                         loadSidecarFile = true;
        std::string                  surfaceName;
        bool                         sensor   = false;
        const PhysicsSurfaceCatalog* surfaces = nullptr;
    };

    // nlohmann::json::parse(text, nullptr, false). On failure `out` is unchanged.
    bool parseCollisionSidecar(std::string_view jsonText, CollisionSidecar& out);

    bool cookCollisionShape(const CollisionCookDesc& desc, CollisionShape& out);

    AssetRef<CollisionShape> internCollisionShape(AssetManager& assets, AssetRef<CollisionShape> shape, const std::string& cacheKey = {});
} // namespace Dark::Physics
