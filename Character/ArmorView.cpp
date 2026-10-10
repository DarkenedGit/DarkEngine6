#include "Character/ArmorView.h"
#include "Assets/AssetManager.h"
#include "Assets/Model.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Physics/PhysicsBind.h"
#include "Physics/PhysicsComponent.h"
#include "Physics/PhysicsWorld.h"
#include "Render/GpuUpload.h"

namespace Dark
{

    void equipHunterArmor(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, Entity hunter)
    {
        equipArmorPieces(world, pins, assets, renderer, hunter, Combat::makeHunterArmor());
    }

    void equipArmorPieces(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, Entity owner,
                          Combat::ArmorPiecesComponent armor)
    {
        if (!owner.valid() || !world.alive(owner))
            return;
        AssetRef<Model> cube = loadAndUploadModel(renderer, assets, "models/unit_cube.gltf");

        for (int i = 0; cube && i < armor.count; ++i)
        {
            Combat::ArmorPiece& piece = armor.pieces[i];
            piece.visual = world.createEntity();
            world.emplace<TransformComponent>(piece.visual, Math::Vector3f{}, Math::Quaternion::IDENTITY, piece.halfExtents * 2.0f);
            ModelComponent mc{};
            mc.modelAssetID = cube->id;
            mc.castShadow   = true;
            setModelComponent(world, pins, assets, piece.visual, mc);
        }
        world.emplace<Combat::ArmorPiecesComponent>(owner, armor);
        syncArmorVisuals(world);
    }

    void syncArmorVisuals(World& world)
    {
        world.each<Combat::ArmorPiecesComponent>([&](Entity e, Combat::ArmorPiecesComponent& armor) {
            const TransformComponent* owner = world.get<TransformComponent>(e);
            if (!owner)
                return;
            for (int i = 0; i < armor.count; ++i)
            {
                const Combat::ArmorPiece& piece = armor.pieces[i];
                TransformComponent*       xf    = piece.attached && piece.visual.valid() ? world.get<TransformComponent>(piece.visual) : nullptr;
                if (!xf)
                    continue;
                xf->position = Combat::armorPieceWorldPos(piece, owner->position, owner->rotation);
                xf->rotation = owner->rotation;
            }
        });
    }

    void knockOffArmor(World& world, Physics::PhysicsWorld& physics, Entity owner, const Combat::ArmorBreak& broke)
    {
        Combat::ArmorPiecesComponent* armor = world.get<Combat::ArmorPiecesComponent>(owner);
        if (!armor || broke.index < 0 || !physics.valid())
            return;
        Combat::ArmorPiece& piece = armor->pieces[broke.index];
        if (!piece.visual.valid() || !world.alive(piece.visual))
            return;

        PhysicsComponent phys;
        phys.mode  = PhysicsBodyMode::Dynamic;
        phys.shape = PhysicsShapeKind::Box;
        world.emplace<PhysicsComponent>(piece.visual, phys);
        if (!Physics::bindPhysicsEntity(physics, world, piece.visual))
            return;

        const Math::Vector3f push{ broke.push.x * 4.0f, 3.0f, broke.push.z * 4.0f };
        physics.setBodyVelocity(physics.bodyOf(piece.visual), push, Math::Vector3f{ 6.0f, 2.0f, 4.0f });
        if (const ItemDef* def = findItemDef(piece.name))
        {
            world.emplace<ItemPickupComponent>(piece.visual, ItemPickupComponent{ { def, 1 } });
            piece.visual = {};
        }
        DE_LOG_INFO("Armor: {} knocked off", piece.name);
    }

    void removeArmor(World& world, AssetPinTable& pins, Physics::PhysicsWorld& physics, Entity owner)
    {
        Combat::ArmorPiecesComponent* armor = world.get<Combat::ArmorPiecesComponent>(owner);
        if (!armor)
            return;
        for (int i = 0; i < armor->count; ++i)
        {
            const Entity visual = armor->pieces[i].visual;
            if (!visual.valid() || !world.alive(visual))
                continue;
            physics.destroyBody(world, visual);
            if (const ModelComponent* mc = world.get<ModelComponent>(visual))
                unpinModelComponent(pins, *mc);
            world.destroyEntity(visual);
        }
        world.remove<Combat::ArmorPiecesComponent>(owner);
    }

} // namespace Dark
