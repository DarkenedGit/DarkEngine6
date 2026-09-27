#pragma once

#include "Physics/PhysicsIds.h"
#include "Physics/PhysicsBodyComponent.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Assets/MeshData.h"
#include "ECS/Entity.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

#include <vector>

namespace Dark
{
    class World;
}

namespace Dark::Physics
{
    class CollisionShape;

    struct PhysicsWorldDesc
    {
        bool  enabled     = false; // create/step/bind; pawn movers are host flags
        float gravity     = 24.0f; // magnitude m/s^2; world vector is (0, -gravity, 0)
        float timeStep    = 1.0f / 60.0f;
        int   subSteps    = 4;
        int   maxSteps    = 5;
        bool  enableSleep = true;
        bool  debugDraw   = false;
    };

    // Primitive box for tests and the hello-world stack.
    struct PhysicsBoxDesc
    {
        Math::Vector3f   position{0.0f, 0.0f, 0.0f};
        Math::Quaternion rotation{};
        Math::Vector3f   halfExtents{0.5f, 0.5f, 0.5f};
        Math::Vector3f   scale{1.0f, 1.0f, 1.0f};
        bool             dynamic  = false;
        float            density  = 1.0f;
        float            friction = 0.3f;
    };

    struct PhysicsBodyDesc
    {
        PhysicsBodyMode       mode       = PhysicsBodyMode::Static;
        const CollisionShape* shape      = nullptr;
        AssetID               shapeAsset = NULL_ASSET;
        uint32_t              surfaceId  = 0;
        bool                  sensor     = false;
        float                 density    = 1.0f;
        float                 friction   = 0.6f;
    };

    class PhysicsWorld
    {
    public:
        PhysicsWorld() = default;
        ~PhysicsWorld();

        PhysicsWorld(const PhysicsWorld&)            = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;
        PhysicsWorld(PhysicsWorld&& other) noexcept;
        PhysicsWorld& operator=(PhysicsWorld&& other) noexcept;

        bool create(const PhysicsWorldDesc& desc);
        void destroy();
        bool valid() const;
        bool enabled() const;

        // If !enabled, returns true without stepping. Hitch clamp matches Sandbox2D (accum = 0 at maxSteps).
        bool step(float dt);
        // Unbinds bodies whose entities are no longer alive, then step(dt).
        bool step(float dt, World& world);

        PhysicsBodyId createBox(const PhysicsBoxDesc& desc);
        bool          createBody(World& world, Entity e, const PhysicsBodyDesc& desc);
        void          destroyBody(PhysicsBodyId id);
        void          destroyBody(Entity e);
        void          destroyBody(World& world, Entity e);

        bool          bodyValid(PhysicsBodyId id) const;
        bool          isBound(Entity e) const;
        PhysicsBodyId bodyOf(Entity e) const;
        uint32_t      boundCount() const { return static_cast<uint32_t>(m_bound.size()); }
        bool          getBodyPose(PhysicsBodyId id, Math::Vector3f& position, Math::Quaternion& rotation) const;

        Math::Vector3f gravity() const;

        bool debugOverlay() const { return m_desc.debugDraw; }
        void setDebugOverlay(bool on) { m_desc.debugDraw = on; }
        // Fills `out` with shape wireframes (and contacts). Hosts call this when the overlay is on.
        void debugDraw(LineMeshData& out) const;

    private:
        struct BoundBody
        {
            Entity        entity{};
            PhysicsBodyId body = kNullPhysicsBody;
        };

        void resetState();
        void unbindDead(World& world);
        void unbindIndex(size_t index);
        int  findBound(Entity e) const;

        PhysicsWorldId   m_id      = kNullPhysicsWorld;
        PhysicsWorldDesc m_desc{};
        float            m_accum = 0.0f;
        std::vector<BoundBody> m_bound;
    };
} // namespace Dark::Physics
