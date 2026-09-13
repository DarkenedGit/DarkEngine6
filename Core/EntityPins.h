#pragma once

#include "Assets/AssetHandle.h"
#include "ECS/Components.h"
#include "ECS/Entity.h"

namespace Dark
{

    class AssetManager;
    class AssetPinTable;
    class World;

    void pinMeshComponent(AssetPinTable& pins, AssetManager& assets, const MeshComponent& mc);
    void unpinMeshComponent(AssetPinTable& pins, const MeshComponent& mc);
    void pinModelComponent(AssetPinTable& pins, AssetManager& assets, const ModelComponent& mc);
    void unpinModelComponent(AssetPinTable& pins, const ModelComponent& mc);

    void setMeshComponent(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, MeshComponent next);
    void setMeshMaterial(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, AssetID matId);

    // Unpin Mesh/Model (Sound after E7). Never calls world.destroyEntity.
    void onEntityRemoved(World& world, Entity e, AssetPinTable* pins);

} // namespace Dark
