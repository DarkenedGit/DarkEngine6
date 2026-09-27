#include "Physics/PhysicsBind.h"
#include "Assets/Model.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Physics/CollisionCook.h"
#include "Physics/CollisionShape.h"
#include "Physics/PhysicsComponent.h"
#include "Physics/PhysicsSurface.h"
#include "Physics/PhysicsWorld.h"

#include <cmath>

namespace Dark::Physics
{
    namespace
    {
        Math::Vector3f scaledBoundsSize(const Model* model, const Math::Vector3f& entityScale)
        {
            Math::Vector3f size(1.0f, 1.0f, 1.0f);
            if (model && model->bounds().IsValid())
                size = model->bounds().Size();
            size.x = std::fabs(size.x * entityScale.x);
            size.y = std::fabs(size.y * entityScale.y);
            size.z = std::fabs(size.z * entityScale.z);
            if (size.x < 1.0e-3f)
                size.x = 1.0e-3f;
            if (size.y < 1.0e-3f)
                size.y = 1.0e-3f;
            if (size.z < 1.0e-3f)
                size.z = 1.0e-3f;
            return size;
        }

        bool cookBox(const Math::Vector3f& fullSize, CollisionShape& out)
        {
            CollisionCookDesc desc;
            desc.kind  = CollisionCookKind::Cube;
            desc.scale = fullSize; // cook treats scale as a multiplier on the 1 m unit cube
            return cookCollisionShape(desc, out);
        }

        bool cookForSettings(const PhysicsComponent& settings, const TransformComponent& xf, const Model* model, CollisionShape& out)
        {
            CollisionCookDesc desc;
            desc.scale  = xf.scale;
            desc.sensor = settings.sensor;
            switch (settings.shape)
            {
            case PhysicsShapeKind::Sphere:
                desc.kind = CollisionCookKind::Sphere;
                return cookCollisionShape(desc, out);
            case PhysicsShapeKind::Capsule:
            {
                desc.kind = CollisionCookKind::Pawn;
                const Math::Vector3f size = scaledBoundsSize(model, xf.scale);
                Math::AABox3f        box  = Math::AABox3f::Empty();
                box.ExpandToInclude(Math::Vector3f(0.0f, 0.0f, 0.0f));
                box.ExpandToInclude(size);
                desc.bounds = box;
                return cookCollisionShape(desc, out);
            }
            case PhysicsShapeKind::Model:
                if (model)
                {
                    desc.kind  = CollisionCookKind::Model;
                    desc.model = model;
                    if (cookCollisionShape(desc, out))
                        return true;
                    DE_LOG_INFO(LogCategory::Collision, "PhysicsBind: model has no collision parts; using bounds box");
                }
                return cookBox(scaledBoundsSize(model, xf.scale), out);
            case PhysicsShapeKind::Box:
            default:
                desc.kind = CollisionCookKind::Cube;
                return cookCollisionShape(desc, out);
            }
        }
    } // namespace

    bool bindPhysicsEntity(PhysicsWorld& phys, World& world, Entity entity, const Model* model, const PhysicsSurfaceCatalog* surfaces)
    {
        if (!world.alive(entity))
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsBind: dead entity");
            return false;
        }
        const PhysicsComponent* settings = world.get<PhysicsComponent>(entity);
        if (!settings)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsBind: missing PhysicsComponent");
            return false;
        }
        if (!settings->enabled)
        {
            if (phys.isBound(entity))
                phys.destroyBody(world, entity);
            return true;
        }
        const TransformComponent* xf = world.get<TransformComponent>(entity);
        if (!xf)
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsBind: missing TransformComponent");
            return false;
        }

        CollisionShape shape;
        if (!cookForSettings(*settings, *xf, model, shape))
        {
            DE_LOG_ERROR(LogCategory::Collision, "PhysicsBind: shape cook failed");
            return false;
        }

        PhysicsBodyDesc desc;
        desc.mode           = settings->mode == PhysicsBodyMode::None ? PhysicsBodyMode::Static : settings->mode;
        desc.shape          = &shape;
        desc.sensor         = settings->sensor;
        desc.density        = settings->density;
        desc.friction       = settings->friction;
        desc.restitution    = settings->restitution;
        desc.linearDamping  = settings->linearDamping;
        desc.angularDamping = settings->angularDamping;
        desc.gravityScale   = settings->gravityScale;
        desc.fixedRotation  = settings->fixedRotation;
        if (surfaces && !settings->surface.empty())
            desc.surfaceId = surfaces->idOf(settings->surface);

        if (!phys.createBody(world, entity, desc))
            return false;
        if (PhysicsBodyComponent* body = world.get<PhysicsBodyComponent>(entity))
            body->bakedScale = xf->scale;
        return true;
    }
} // namespace Dark::Physics
