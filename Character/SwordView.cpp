#include "Character/SwordView.h"
#include "Animation/AnimGraphComponent.h"
#include "Assets/AssetManager.h"
#include "Assets/Model.h"
#include "Character/HealthComponent.h"
#include "Core/EntityPins.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Render/GpuUpload.h"

namespace Dark
{
    using Math::Matrix4f;
    using Math::Vector3f;

    namespace
    {
        const Vector3f kBladeSize{ 0.035f, 0.08f, 0.9f };
        const Vector3f kGuardSize{ 0.24f, 0.04f, 0.04f };

        Entity makePart(World& world, AssetPinTable& pins, AssetManager& assets, AssetID cube, const Vector3f& size)
        {
            const Entity e = world.createEntity();
            world.emplace<TransformComponent>(e, Vector3f{}, Math::Quaternion::IDENTITY, size);
            ModelComponent mc{};
            mc.modelAssetID = cube;
            mc.castShadow   = true;
            setModelComponent(world, pins, assets, e, mc);
            return e;
        }

        void destroyPart(World& world, AssetPinTable& pins, Entity e)
        {
            if (!e.valid() || !world.alive(e))
                return;
            if (const ModelComponent* mc = world.get<ModelComponent>(e))
                unpinModelComponent(pins, *mc);
            world.destroyEntity(e);
        }

        Vector3f axis(const Matrix4f& m, int row)
        {
            const Math::Vector4f r = m.GetRow(row);
            Vector3f             v{ r.x, r.y, r.z };
            if (v.MagnitudeSqrd() > 1.0e-8f)
                v.Normalize();
            return v;
        }

        void setPartVisible(World& world, Entity part, bool visible)
        {
            if (ModelComponent* mc = world.get<ModelComponent>(part))
                mc->visible = visible;
        }
    } // namespace

    void equipSword(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, Entity owner)
    {
        if (!owner.valid() || !world.alive(owner) || world.has<SwordComponent>(owner))
            return;
        AssetRef<Model> cube = loadAndUploadModel(renderer, assets, "models/unit_cube.gltf");
        if (!cube)
            return;
        SwordComponent sword{};
        sword.blade = makePart(world, pins, assets, cube->id, kBladeSize);
        sword.guard = makePart(world, pins, assets, cube->id, kGuardSize);
        world.emplace<SwordComponent>(owner, sword);
    }

    void syncSwordVisuals(World& world)
    {
        world.each<SwordComponent>([&](Entity e, SwordComponent& sword) {
            TransformComponent*       blade = world.get<TransformComponent>(sword.blade);
            TransformComponent*       guard = world.get<TransformComponent>(sword.guard);
            const TransformComponent* owner = world.get<TransformComponent>(e);
            const AnimGraphComponent* ag    = world.get<AnimGraphComponent>(e);
            const HealthComponent*    hp    = world.get<HealthComponent>(e);
            if (!blade || !guard)
                return;

            const Skeleton* skel = ag ? ag->graph.skeleton() : nullptr;
            if (sword.hand < 0 && skel)
            {
                for (size_t j = 0; j < skel->joints.size(); ++j)
                {
                    if (skel->joints[j].name == "R_Hand")
                        sword.hand = static_cast<int>(j);
                }
            }

            const AnimPose* pose = ag ? &ag->graph.player().pose() : nullptr;
            const bool      show = sword.shown && owner && pose && (!hp || hp->health.alive()) && sword.hand >= 0
                && static_cast<uint32_t>(sword.hand) < pose->boneCount;
            // A stowed sword keeps its real size. Render walks skip ModelComponent::visible == false
            // before upload, so hiding does not depend on a zero scale.
            setPartVisible(world, sword.blade, show);
            setPartVisible(world, sword.guard, show);
            if (!show)
                return;

            const Matrix4f ownerWorld = Matrix4f::ScaleMatrixXYZ(owner->scale.x, owner->scale.y, owner->scale.z) * owner->rotation.ToMatrix4()
                * Matrix4f::TranslationMatrix(owner->position.x, owner->position.y, owner->position.z);
            const Matrix4f hand    = pose->jointWorld[sword.hand] * ownerWorld;
            const Vector3f grip    = hand.GetTranslation();
            const Vector3f forward = axis(hand, 2);
            const auto     rot     = Math::Quaternion::FromLookRotation(forward, axis(hand, 1));

            blade->position = grip + forward * (0.06f + kBladeSize.z * 0.5f);
            blade->rotation = rot;
            blade->scale    = kBladeSize;
            guard->position = grip + forward * 0.06f;
            guard->rotation = rot;
            guard->scale    = kGuardSize;
        });
    }

    void removeSword(World& world, AssetPinTable& pins, Entity owner)
    {
        const SwordComponent* sword = world.get<SwordComponent>(owner);
        if (!sword)
            return;
        destroyPart(world, pins, sword->blade);
        destroyPart(world, pins, sword->guard);
        world.remove<SwordComponent>(owner);
    }

} // namespace Dark
