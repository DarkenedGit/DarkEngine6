#include "Character/ShieldView.h"

#include "Assets/AssetManager.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Combat/Shield.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/MathHelper.h"
#include "Math/Quaternion.h"
#include "Render/GpuUpload.h"
#include "Render/MeshGen.h"

#include <memory>

namespace Dark
{

    Entity spawnPlayerShield(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer)
    {
        MeshData mesh;
        if (!CreateBuckler(mesh))
        {
            DE_LOG_ERROR("Shield: buckler mesh failed");
            return {};
        }

        auto mat = std::make_shared<Material>();
        if (!mat->createSolid(assets, 176, 186, 198, 255))
        {
            DE_LOG_ERROR("Shield: buckler material failed");
            return {};
        }
        mat->setMetallicRoughness(0.92f, 0.28f);
        mat = assets.internMaterial(mat, "runtime:/player/buckler-mat");
        if (!mat || mat->id == NULL_ASSET)
        {
            DE_LOG_ERROR("Shield: buckler material was not interned");
            return {};
        }

        AssetRef<Model> model = internAndUploadProceduralModel(renderer, assets, std::move(mesh), mat, "runtime:/player/buckler");
        if (!model || !model->valid())
        {
            DE_LOG_ERROR("Shield: buckler model failed");
            return {};
        }

        Entity e = world.createEntity();
        world.emplace<TagComponent>(e, "Shield");
        world.emplace<TransformComponent>(e);
        ModelComponent mc{};
        mc.modelAssetID = model->id;
        mc.castShadow   = true;
        setModelComponent(world, pins, assets, e, mc);
        return e;
    }

    void destroyPlayerShield(World& world, AssetPinTable& pins, Entity& shield)
    {
        if (!shield.valid() || !world.alive(shield))
        {
            shield = {};
            return;
        }
        onEntityRemoved(world, shield, &pins);
        world.destroyEntity(shield);
        shield = {};
    }

    void placePlayerShield(TransformComponent& shield, const TransformComponent& player, float raiseAlpha)
    {
        const float t = Math::SmoothStep(0.0f, 1.0f, Math::Clamp(raiseAlpha, 0.0f, 1.0f));
        const Combat::ShieldLocalPose pose = Combat::shieldLocalPose(t);
        shield.position = player.position + player.rotation.Rotate(pose.position);
        shield.rotation = player.rotation * pose.rotation;
        shield.rotation.Normalize();
        shield.scale = Math::Vector3f{ 1.0f, 1.0f, 1.0f };
    }

    void placePlayerFlashlight(TransformComponent& light,
                               const Math::Vector3f& camPos,
                               const Math::Vector3f& look,
                               const Math::Vector3f& right,
                               const Math::Vector3f& up)
    {
        light.position = camPos + look * 0.2f + right * 0.15f + up * -0.1f;
        light.rotation = Math::Quaternion::FromLookRotation(look, up);
    }

    Entity spawnPlayerFlashlight(World& world)
    {
        Entity e = world.createEntity();
        world.emplace<TagComponent>(e, "Flashlight");
        world.emplace<TransformComponent>(e);
        auto& light         = world.emplace<LocalLightComponent>(e);
        light.type          = LocalLightType::Spot;
        light.color         = Math::Vector3f{ 1.0f, 0.97f, 0.9f };
        light.intensity     = 1571.0f; // 500*π after Fd/π
        light.range         = 22.0f;
        light.innerConeDeg  = 10.0f;
        light.outerConeDeg  = 22.0f;
        light.enabled       = true;
        return e;
    }

    void destroyPlayerFlashlight(World& world, Entity& light)
    {
        if (!light.valid() || !world.alive(light))
        {
            light = {};
            return;
        }
        world.destroyEntity(light);
        light = {};
    }

} // namespace Dark
