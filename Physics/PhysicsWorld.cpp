#include "Physics/PhysicsWorld.h"
#include "Physics/CollisionShape.h"
#include "Physics/PhysicsMath.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/AABox3f.h"

#include <box3d/box3d.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace Dark::Physics
{
    void installAssertHook();
    void attachPhysicsDebugCallbacks(b3WorldDef& def);
    void debugDrawPhysicsWorld(b3WorldId worldId, LineMeshData& out);

    namespace
    {
        static_assert(sizeof(PhysicsWorldId) == sizeof(uint32_t));
        static_assert(sizeof(PhysicsBodyId) == sizeof(uint64_t));
        static_assert(sizeof(PhysicsShapeId) == sizeof(uint64_t));
        static_assert(sizeof(b3WorldId) == sizeof(uint32_t));
        static_assert(sizeof(b3BodyId) == sizeof(uint64_t));
        static_assert(sizeof(b3ShapeId) == sizeof(uint64_t));

        Math::Vector3f fromB3(const b3Vec3& v)
        {
            return Math::Vector3f(v.x, v.y, v.z);
        }

        b3Vec3 toB3(const Math::Vector3f& v)
        {
            return b3Vec3{v.x, v.y, v.z};
        }

        b3Quat toB3(const Math::Quaternion& q)
        {
            return b3Quat{{q.x, q.y, q.z}, q.w};
        }

        Math::Quaternion fromB3(const b3Quat& q)
        {
            return Math::Quaternion(q.s, q.v.x, q.v.y, q.v.z);
        }

        Math::Vector3f fromB3Pos(const b3Pos& p)
        {
            return Math::Vector3f(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
        }

        b3Pos toB3Pos(const Math::Vector3f& v)
        {
            return b3Pos{v.x, v.y, v.z};
        }

        b3WorldId loadWorld(PhysicsWorldId id)
        {
            return b3LoadWorldId(id);
        }

        b3BodyId loadBody(PhysicsBodyId id)
        {
            return b3LoadBodyId(id);
        }

        b3BodyType bodyTypeFromMode(PhysicsBodyMode mode)
        {
            switch (mode)
            {
            case PhysicsBodyMode::Dynamic:
                return b3_dynamicBody;
            case PhysicsBodyMode::Kinematic:
            case PhysicsBodyMode::Mover:
                return b3_kinematicBody;
            case PhysicsBodyMode::Static:
            default:
                return b3_staticBody;
            }
        }

        PhysicsBodyMode modeFromHint(CollisionBodyHint hint)
        {
            switch (hint)
            {
            case CollisionBodyHint::Dynamic:
                return PhysicsBodyMode::Dynamic;
            case CollisionBodyHint::Kinematic:
                return PhysicsBodyMode::Kinematic;
            default:
                return PhysicsBodyMode::Static;
            }
        }

        void fillShapeDef(b3ShapeDef& shapeDef, const CollisionPart& part, const PhysicsBodyDesc& desc, bool dynamic)
        {
            shapeDef = b3DefaultShapeDef();
            const bool sensor = part.sensor || desc.sensor;
            shapeDef.isSensor = sensor;
            shapeDef.density  = (dynamic && !sensor) ? desc.density : 0.0f;
            shapeDef.baseMaterial.friction       = desc.friction;
            shapeDef.baseMaterial.restitution    = desc.restitution;
            shapeDef.baseMaterial.userMaterialId = part.surfaceId != 0 ? part.surfaceId : desc.surfaceId;
            if (part.categoryBits != 0)
                shapeDef.filter.categoryBits = part.categoryBits;
            if (part.maskBits != 0)
                shapeDef.filter.maskBits = part.maskBits;
        }

        bool createBoxOnBody(b3BodyId bodyId, const CollisionPart& part, const b3ShapeDef& shapeDef)
        {
            const Math::Vector3f hx = part.halfExtents;
            if (hx.x <= 0.0f || hx.y <= 0.0f || hx.z <= 0.0f)
                return false;
            b3Transform xf;
            xf.p                  = toB3(part.localPos);
            xf.q                  = toB3(part.localRot);
            const b3BoxHull hull  = b3MakeTransformedBoxHull(hx.x, hx.y, hx.z, xf);
            const b3ShapeId sid   = b3CreateHullShape(bodyId, &shapeDef, &hull.base);
            return b3Shape_IsValid(sid);
        }

        bool createSphereOnBody(b3BodyId bodyId, const CollisionPart& part, const b3ShapeDef& shapeDef)
        {
            if (part.radius <= 0.0f)
                return false;
            b3Sphere sphere;
            sphere.center       = toB3(part.localPos);
            sphere.radius       = part.radius;
            const b3ShapeId sid = b3CreateSphereShape(bodyId, &shapeDef, &sphere);
            return b3Shape_IsValid(sid);
        }

        bool createCapsuleOnBody(b3BodyId bodyId, const CollisionPart& part, const b3ShapeDef& shapeDef)
        {
            if (part.radius <= 0.0f)
                return false;
            Math::Quaternion rot = part.localRot;
            rot.Normalize();
            const Math::Vector3f a = part.localPos + rot.Rotate(part.capsuleA);
            const Math::Vector3f b = part.localPos + rot.Rotate(part.capsuleB);
            b3Capsule cap;
            cap.center1         = toB3(a);
            cap.center2         = toB3(b);
            cap.radius          = part.radius;
            const b3ShapeId sid = b3CreateCapsuleShape(bodyId, &shapeDef, &cap);
            return b3Shape_IsValid(sid);
        }

        bool createHullOnBody(b3BodyId bodyId, const CollisionPart& part, const b3ShapeDef& shapeDef)
        {
            if (part.hullPoints.size() < 4)
                return false;
            std::vector<b3Vec3> pts;
            pts.reserve(part.hullPoints.size());
            for (const Math::Vector3f& p : part.hullPoints)
                pts.push_back(toB3(p));
            int maxVerts = part.maxHullVerts;
            if (maxVerts < 4)
                maxVerts = 4;
            b3HullData* hull = b3CreateHull(pts.data(), static_cast<int>(pts.size()), maxVerts);
            if (!hull)
                return false;
            const b3ShapeId sid = b3CreateHullShape(bodyId, &shapeDef, hull);
            b3DestroyHull(hull);
            return b3Shape_IsValid(sid);
        }

        bool createAabbBoxFallback(b3BodyId bodyId, const std::vector<Math::Vector3f>& pts, const b3ShapeDef& shapeDef)
        {
            Math::AABox3f box = Math::AABox3f::Empty();
            for (const Math::Vector3f& p : pts)
                box.ExpandToInclude(p);
            if (!box.IsValid())
                return false;
            CollisionPart fallback;
            fallback.geom        = CollisionGeom::Box;
            fallback.localPos    = box.Center();
            fallback.halfExtents = box.Extents();
            if (fallback.halfExtents.x < 1.0e-4f)
                fallback.halfExtents.x = 1.0e-4f;
            if (fallback.halfExtents.y < 1.0e-4f)
                fallback.halfExtents.y = 1.0e-4f;
            if (fallback.halfExtents.z < 1.0e-4f)
                fallback.halfExtents.z = 1.0e-4f;
            return createBoxOnBody(bodyId, fallback, shapeDef);
        }

        bool attachPart(b3BodyId bodyId, const CollisionPart& part, const PhysicsBodyDesc& desc, bool dynamic)
        {
            b3ShapeDef shapeDef;
            fillShapeDef(shapeDef, part, desc, dynamic);
            switch (part.geom)
            {
            case CollisionGeom::Box:
                return createBoxOnBody(bodyId, part, shapeDef);
            case CollisionGeom::Sphere:
                return createSphereOnBody(bodyId, part, shapeDef);
            case CollisionGeom::Capsule:
                return createCapsuleOnBody(bodyId, part, shapeDef);
            case CollisionGeom::Hull:
                if (createHullOnBody(bodyId, part, shapeDef))
                    return true;
                DE_LOG_WARN(LogCategory::Collision, "PhysicsWorld: hull create failed; falling back to AABB box");
                return createAabbBoxFallback(bodyId, part.hullPoints, shapeDef);
            case CollisionGeom::Mesh:
            case CollisionGeom::HeightField:
            case CollisionGeom::Compound:
            case CollisionGeom::None:
            default:
                DE_LOG_WARN(LogCategory::Collision, "PhysicsWorld: skipping unsupported collision geom {}", static_cast<unsigned>(part.geom));
                return false;
            }
        }
    } // namespace

    PhysicsWorld::~PhysicsWorld()
    {
        destroy();
    }

    PhysicsWorld::PhysicsWorld(PhysicsWorld&& other) noexcept
        : m_id(other.m_id)
        , m_desc(other.m_desc)
        , m_accum(other.m_accum)
        , m_bound(std::move(other.m_bound))
    {
        other.resetState();
    }

    PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&& other) noexcept
    {
        if (this == &other)
            return *this;
        destroy();
        m_id    = other.m_id;
        m_desc  = other.m_desc;
        m_accum = other.m_accum;
        m_bound = std::move(other.m_bound);
        other.resetState();
        return *this;
    }

    void PhysicsWorld::resetState()
    {
        m_id    = kNullPhysicsWorld;
        m_desc  = {};
        m_accum = 0.0f;
        m_bound.clear();
    }

    bool PhysicsWorld::create(const PhysicsWorldDesc& desc)
    {
        installAssertHook();
        destroy();

        if (desc.timeStep <= 0.0f || desc.subSteps < 1 || desc.maxSteps < 1)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsWorld: invalid step settings (dt={}, sub={}, max={})", desc.timeStep, desc.subSteps, desc.maxSteps);
            return false;
        }

        b3WorldDef def       = b3DefaultWorldDef();
        def.gravity          = {0.0f, -desc.gravity, 0.0f};
        def.workerCount      = 1;
        def.enableSleep      = desc.enableSleep;
        def.enableContinuous = true;
        attachPhysicsDebugCallbacks(def);

        const b3WorldId worldId = b3CreateWorld(&def);
        if (!b3World_IsValid(worldId))
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsWorld: b3CreateWorld failed");
            return false;
        }

        m_id    = b3StoreWorldId(worldId);
        m_desc  = desc;
        m_accum = 0.0f;
        DE_LOG_INFO(LogCategory::Collision, "PhysicsWorld: created (enabled={}, gravity={})", desc.enabled, desc.gravity);
        return true;
    }

    void PhysicsWorld::destroy()
    {
        if (m_id == kNullPhysicsWorld)
            return;
        const b3WorldId worldId = loadWorld(m_id);
        if (b3World_IsValid(worldId))
            b3DestroyWorld(worldId);
        resetState();
    }

    bool PhysicsWorld::valid() const
    {
        if (m_id == kNullPhysicsWorld)
            return false;
        return b3World_IsValid(loadWorld(m_id));
    }

    bool PhysicsWorld::enabled() const
    {
        return m_desc.enabled;
    }

    bool PhysicsWorld::step(float dt)
    {
        if (!m_desc.enabled)
            return true;
        if (!valid())
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsWorld: step on invalid world");
            return false;
        }
        if (dt <= 0.0f)
            return true;

        m_accum += dt;
        int steps = 0;
        while (m_accum >= m_desc.timeStep && steps < m_desc.maxSteps)
        {
            b3World_Step(loadWorld(m_id), m_desc.timeStep, m_desc.subSteps);
            m_accum -= m_desc.timeStep;
            ++steps;
        }
        if (steps == m_desc.maxSteps)
            m_accum = 0.0f;
        return true;
    }

    bool PhysicsWorld::step(float dt, World& world)
    {
        unbindDead(world);
        return step(dt);
    }

    PhysicsBodyId PhysicsWorld::createBox(const PhysicsBoxDesc& desc)
    {
        if (!valid())
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsWorld: createBox on invalid world");
            return kNullPhysicsBody;
        }

        const Math::Vector3f hx = bakeBoxHalfExtents(desc.halfExtents, desc.scale);
        if (hx.x <= 0.0f || hx.y <= 0.0f || hx.z <= 0.0f)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsWorld: createBox half-extents must be positive ({}, {}, {})", hx.x, hx.y, hx.z);
            return kNullPhysicsBody;
        }

        Math::Quaternion rot = desc.rotation;
        rot.Normalize();

        b3BodyDef bodyDef = b3DefaultBodyDef();
        bodyDef.type      = desc.dynamic ? b3_dynamicBody : b3_staticBody;
        bodyDef.position  = toB3Pos(desc.position);
        bodyDef.rotation  = toB3(rot);

        const b3BodyId bodyId = b3CreateBody(loadWorld(m_id), &bodyDef);
        if (!b3Body_IsValid(bodyId))
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsWorld: b3CreateBody failed");
            return kNullPhysicsBody;
        }

        b3BoxHull  hull     = b3MakeBoxHull(hx.x, hx.y, hx.z);
        b3ShapeDef shapeDef = b3DefaultShapeDef();
        shapeDef.density    = desc.dynamic ? desc.density : 0.0f;
        shapeDef.baseMaterial.friction = desc.friction;

        const b3ShapeId shapeId = b3CreateHullShape(bodyId, &shapeDef, &hull.base);
        if (!b3Shape_IsValid(shapeId))
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsWorld: b3CreateHullShape failed");
            b3DestroyBody(bodyId);
            return kNullPhysicsBody;
        }

        return b3StoreBodyId(bodyId);
    }

    bool PhysicsWorld::createBody(World& world, Entity e, const PhysicsBodyDesc& desc)
    {
        if (!valid())
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsWorld: createBody on invalid world");
            return false;
        }
        if (!world.alive(e))
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsWorld: createBody on dead entity");
            return false;
        }
        const TransformComponent* xf = world.get<TransformComponent>(e);
        if (!xf)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsWorld: createBody requires TransformComponent");
            return false;
        }
        if (!desc.shape || !desc.shape->valid())
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsWorld: createBody missing CollisionShape");
            return false;
        }

        if (isBound(e))
            destroyBody(world, e);

        PhysicsBodyMode mode = desc.mode;
        if (mode == PhysicsBodyMode::None)
            mode = modeFromHint(desc.shape->bodyHint());

        Math::Quaternion rot = xf->rotation;
        rot.Normalize();

        b3BodyDef bodyDef = b3DefaultBodyDef();
        bodyDef.type           = bodyTypeFromMode(mode);
        bodyDef.position       = toB3Pos(xf->position);
        bodyDef.rotation       = toB3(rot);
        bodyDef.linearDamping  = desc.linearDamping;
        bodyDef.angularDamping = desc.angularDamping;
        bodyDef.gravityScale   = desc.gravityScale;
        bodyDef.userData       = reinterpret_cast<void*>(static_cast<uintptr_t>(e.id()));
        if (desc.fixedRotation)
        {
            bodyDef.motionLocks.angularX = true;
            bodyDef.motionLocks.angularY = true;
            bodyDef.motionLocks.angularZ = true;
        }

        const b3BodyId bodyId = b3CreateBody(loadWorld(m_id), &bodyDef);
        if (!b3Body_IsValid(bodyId))
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsWorld: b3CreateBody failed");
            return false;
        }

        const bool dynamic = bodyDef.type == b3_dynamicBody;
        int        attached = 0;
        for (const CollisionPart& part : desc.shape->parts())
        {
            if (attachPart(bodyId, part, desc, dynamic))
                ++attached;
        }
        if (attached == 0)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsWorld: createBody attached no shapes");
            b3DestroyBody(bodyId);
            return false;
        }

        const PhysicsBodyId stored = b3StoreBodyId(bodyId);
        PhysicsBodyComponent comp;
        comp.mode       = mode;
        comp.body       = stored;
        comp.shapeAsset = desc.shapeAsset;
        comp.surfaceId  = desc.surfaceId;
        comp.sensor     = desc.sensor;
        comp.valid      = true;
        world.emplace<PhysicsBodyComponent>(e, comp);

        BoundBody bound;
        bound.entity = e;
        bound.body   = stored;
        m_bound.push_back(bound);
        return true;
    }

    void PhysicsWorld::destroyBody(PhysicsBodyId id)
    {
        if (!bodyValid(id))
            return;
        b3DestroyBody(loadBody(id));
        for (size_t i = 0; i < m_bound.size(); ++i)
        {
            if (m_bound[i].body == id)
            {
                m_bound[i] = m_bound.back();
                m_bound.pop_back();
                break;
            }
        }
    }

    void PhysicsWorld::destroyBody(Entity e)
    {
        const int idx = findBound(e);
        if (idx < 0)
            return;
        unbindIndex(static_cast<size_t>(idx));
    }

    void PhysicsWorld::destroyBody(World& world, Entity e)
    {
        destroyBody(e);
        if (!world.alive(e))
            return;
        if (PhysicsBodyComponent* c = world.get<PhysicsBodyComponent>(e))
        {
            c->body  = kNullPhysicsBody;
            c->valid = false;
        }
    }

    bool PhysicsWorld::bodyValid(PhysicsBodyId id) const
    {
        if (id == kNullPhysicsBody || !valid())
            return false;
        return b3Body_IsValid(loadBody(id));
    }

    bool PhysicsWorld::isBound(Entity e) const
    {
        return findBound(e) >= 0;
    }

    PhysicsBodyId PhysicsWorld::bodyOf(Entity e) const
    {
        const int idx = findBound(e);
        if (idx < 0)
            return kNullPhysicsBody;
        return m_bound[static_cast<size_t>(idx)].body;
    }

    bool PhysicsWorld::getBodyPose(PhysicsBodyId id, Math::Vector3f& position, Math::Quaternion& rotation) const
    {
        if (!bodyValid(id))
            return false;
        const b3BodyId bodyId = loadBody(id);
        position              = fromB3Pos(b3Body_GetPosition(bodyId));
        rotation              = fromB3(b3Body_GetRotation(bodyId));
        return true;
    }

    bool PhysicsWorld::setBodyPose(PhysicsBodyId id, const Math::Vector3f& position, const Math::Quaternion& rotation)
    {
        if (!bodyValid(id))
            return false;
        Math::Quaternion rot = rotation;
        const float      mag = rot.x * rot.x + rot.y * rot.y + rot.z * rot.z + rot.w * rot.w;
        if (mag < 1.0e-8f)
            rot = Math::Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
        else
            rot.Normalize();
        b3Body_SetTransform(loadBody(id), toB3Pos(position), toB3(rot));
        return true;
    }

    void PhysicsWorld::clearBodyVelocity(PhysicsBodyId id)
    {
        if (!bodyValid(id))
            return;
        const b3BodyId body = loadBody(id);
        b3Body_SetLinearVelocity(body, b3Vec3{0.0f, 0.0f, 0.0f});
        b3Body_SetAngularVelocity(body, b3Vec3{0.0f, 0.0f, 0.0f});
    }

    bool PhysicsWorld::moveKinematicTo(PhysicsBodyId id, const Math::Vector3f& position, const Math::Quaternion& rotation, float dt)
    {
        if (!bodyValid(id) || dt <= 1.0e-6f)
            return false;
        const b3BodyId body = loadBody(id);
        if (b3Body_GetType(body) != b3_kinematicBody)
            return false;

        Math::Quaternion rot = rotation;
        const float      mag = rot.x * rot.x + rot.y * rot.y + rot.z * rot.z + rot.w * rot.w;
        if (mag < 1.0e-8f)
            rot = Math::Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
        else
            rot.Normalize();

        b3WorldTransform target;
        target.p = toB3Pos(position);
        target.q = toB3(rot);
        b3Body_SetTargetTransform(body, target, dt, true);
        return true;
    }

    namespace
    {
        struct MoverIgnore
        {
            PhysicsBodyId id = kNullPhysicsBody;
        };

        bool acceptMoverShape(b3ShapeId shape, void* context)
        {
            if (!b3Shape_IsValid(shape) || b3Shape_IsSensor(shape))
                return false;
            // Dynamics are pushed by the kinematic player sweep. Stopping on them
            // keeps the player from ever touching the body.
            if (b3Body_GetType(b3Shape_GetBody(shape)) == b3_dynamicBody)
                return false;
            const auto* ignore = static_cast<const MoverIgnore*>(context);
            if (!ignore || ignore->id == kNullPhysicsBody)
                return true;
            return b3StoreBodyId(b3Shape_GetBody(shape)) != ignore->id;
        }
    }

    float PhysicsWorld::clipMover(const MoverCast& cast) const
    {
        if (!valid())
            return 1.0f;
        if (cast.radius <= 1.0e-4f)
            return 1.0f;
        const float lenSq = cast.translation.x * cast.translation.x + cast.translation.y * cast.translation.y + cast.translation.z * cast.translation.z;
        if (lenSq < 1.0e-10f)
            return 1.0f;

        float bottom = cast.bottom;
        float top    = cast.top;
        if (top < bottom)
        {
            const float swap = bottom;
            bottom           = top;
            top              = swap;
        }
        const float minSpan = bottom + 2.0f * cast.radius;
        if (top < minSpan)
            top = minSpan;

        b3Capsule capsule;
        capsule.center1 = b3Vec3{0.0f, bottom + cast.radius, 0.0f};
        capsule.center2 = b3Vec3{0.0f, top - cast.radius, 0.0f};
        capsule.radius  = cast.radius;

        MoverIgnore ignore;
        ignore.id = cast.ignore;
        const float fraction = b3World_CastMover(loadWorld(m_id), toB3Pos(cast.origin), &capsule, toB3(cast.translation), b3DefaultQueryFilter(),
                                                 &acceptMoverShape, &ignore);
        if (fraction < 0.0f)
            return 0.0f;
        if (fraction > 1.0f)
            return 1.0f;
        return fraction;
    }

    void PhysicsWorld::pushPoses(World& world, bool includeDynamic, Entity skip)
    {
        if (!valid())
            return;
        for (const BoundBody& bound : m_bound)
        {
            if (!world.alive(bound.entity))
                continue;
            if (skip.valid() && bound.entity.id() == skip.id())
                continue;
            const PhysicsBodyComponent* body = world.get<PhysicsBodyComponent>(bound.entity);
            const TransformComponent*   xf   = world.get<TransformComponent>(bound.entity);
            if (!body || !body->valid || !xf)
                continue;
            const bool dynamic = body->mode == PhysicsBodyMode::Dynamic;
            if (dynamic && !includeDynamic)
                continue;
            if (!setBodyPose(bound.body, xf->position, xf->rotation))
                continue;
            const b3BodyId id = loadBody(bound.body);
            b3Body_SetLinearVelocity(id, b3Vec3{0.0f, 0.0f, 0.0f});
            b3Body_SetAngularVelocity(id, b3Vec3{0.0f, 0.0f, 0.0f});
            if (dynamic)
                b3Body_SetAwake(id, true);
        }
    }

    void PhysicsWorld::writeDynamicPoses(World& world) const
    {
        if (!valid())
            return;
        for (const BoundBody& bound : m_bound)
        {
            if (!world.alive(bound.entity))
                continue;
            const PhysicsBodyComponent* body = world.get<PhysicsBodyComponent>(bound.entity);
            if (!body || !body->valid || body->mode != PhysicsBodyMode::Dynamic)
                continue;
            TransformComponent* xf = world.get<TransformComponent>(bound.entity);
            if (!xf)
                continue;
            Math::Vector3f   pos;
            Math::Quaternion rot;
            if (!getBodyPose(bound.body, pos, rot))
                continue;
            xf->position = pos;
            xf->rotation = rot;
        }
    }

    Math::Vector3f PhysicsWorld::gravity() const
    {
        if (!valid())
            return Math::Vector3f(0.0f, -m_desc.gravity, 0.0f);
        return fromB3(b3World_GetGravity(loadWorld(m_id)));
    }

    void PhysicsWorld::debugDraw(LineMeshData& out) const
    {
        out.positions.clear();
        out.indices.clear();
        if (!valid())
            return;
        debugDrawPhysicsWorld(loadWorld(m_id), out);
    }

    void PhysicsWorld::unbindDead(World& world)
    {
        for (size_t i = m_bound.size(); i > 0; --i)
        {
            if (!world.alive(m_bound[i - 1].entity))
                unbindIndex(i - 1);
        }
    }

    void PhysicsWorld::unbindIndex(size_t index)
    {
        if (index >= m_bound.size())
            return;
        const PhysicsBodyId id = m_bound[index].body;
        if (bodyValid(id))
            b3DestroyBody(loadBody(id));
        m_bound[index] = m_bound.back();
        m_bound.pop_back();
    }

    int PhysicsWorld::findBound(Entity e) const
    {
        for (size_t i = 0; i < m_bound.size(); ++i)
        {
            if (m_bound[i].entity == e)
                return static_cast<int>(i);
        }
        return -1;
    }
} // namespace Dark::Physics
