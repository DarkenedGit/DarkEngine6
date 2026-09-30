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
        bool                  fixedRotation = false;
        float                 density        = 1.0f;
        float                 friction       = 0.6f;
        float                 restitution    = 0.0f;
        float                 linearDamping  = 0.0f;
        float                 angularDamping = 0.0f;
        float                 gravityScale   = 1.0f;
    };

    struct PhysicsHeightFieldDesc
    {
        const float*   heights     = nullptr;      // the grid of heights
        uint32_t       countX      = 0;            // grid points along X
        uint32_t       countZ      = 0;            // grid points along Z
        float          cellSize    = 1.0f;         // metres between grid points
        float          heightScale = 1.0f;         // multiply each height by this
        Math::Vector3f origin{ 0.0f, 0.0f, 0.0f }; // world position of grid point (0,0)
        float          friction = 0.8f;            // how grippy the ground is
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
        PhysicsBodyId createHeightField(const PhysicsHeightFieldDesc& desc);
        bool          createBody(World& world, Entity e, const PhysicsBodyDesc& desc);
        void          destroyBody(PhysicsBodyId id);
        void          destroyBody(Entity e);
        void          destroyBody(World& world, Entity e);

        bool          bodyValid(PhysicsBodyId id) const;
        bool          isBound(Entity e) const;
        PhysicsBodyId bodyOf(Entity e) const;
        uint32_t      boundCount() const { return static_cast<uint32_t>(m_bound.size()); }
        bool          getBodyPose(PhysicsBodyId id, Math::Vector3f& position, Math::Quaternion& rotation) const;
        bool          setBodyPose(PhysicsBodyId id, const Math::Vector3f& position, const Math::Quaternion& rotation);
        void          clearBodyVelocity(PhysicsBodyId id);
        // Kinematic bodies only. Sets the velocity that reaches `position` over `dt` so the
        // next step sweeps into dynamic bodies and pushes them. A teleport does not.
        bool          moveKinematicTo(PhysicsBodyId id, const Math::Vector3f& position, const Math::Quaternion& rotation, float dt);

        // Capsule is relative to `origin`. `bottom` and `top` are local Y of the outer ends.
        // Returns the fraction of `translation` before a static or kinematic shape. Dynamic
        // bodies, sensors, and `ignore` are skipped so the kinematic sweep can push them.
        struct MoverCast
        {
            Math::Vector3f origin{0.0f, 0.0f, 0.0f};
            float          radius      = 0.45f;
            float          bottom      = 0.0f;
            float          top         = 1.8f;
            Math::Vector3f translation{0.0f, 0.0f, 0.0f};
            PhysicsBodyId  ignore      = kNullPhysicsBody;
            PhysicsBodyId  ignore2     = kNullPhysicsBody;
            bool           staticOnly  = false;
        };
        float clipMover(const MoverCast& cast) const;

        struct GroundProbe
        {
            Math::Vector3f from{0.0f, 0.0f, 0.0f};
            float          radius     = 0.3f;
            float          maxDrop    = 50.0f;
            PhysicsBodyId  ignore     = kNullPhysicsBody;
            PhysicsBodyId  ignore2    = kNullPhysicsBody;
            bool           staticOnly = false;
        };
        bool probeGround(const GroundProbe& probe, float& outSurfaceY) const;

        // Edit mode keeps every bound body on its Transform. Play pushes static and kinematic
        // bodies, steps, then copies dynamic poses back so the mesh follows the solver.
        void pushPoses(World& world, bool includeDynamic, Entity skip = {});
        void writeDynamicPoses(World& world) const;

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

        struct HeightFieldSlot
        {
            PhysicsBodyId body = kNullPhysicsBody;
            void*         data = nullptr;
        };

        void resetState();
        void unbindDead(World& world);
        void unbindIndex(size_t index);
        int  findBound(Entity e) const;

        PhysicsWorldId   m_id      = kNullPhysicsWorld;
        PhysicsWorldDesc m_desc{};
        float            m_accum = 0.0f;
        std::vector<BoundBody> m_bound;
        std::vector<HeightFieldSlot> m_heightFields;
    };
} // namespace Dark::Physics
