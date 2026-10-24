#include "Character/ThrowableView.h"

#include "Animation/AnimGraphComponent.h"
#include "Assets/AssetManager.h"
#include "Assets/Model.h"
#include "Character/HealthComponent.h"
#include "Core/ContentRoots.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/ThrowableBelt.h"
#include "Gameplay/ThrowableCatalog.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"
#include "Render/GpuUpload.h"
#include "Weapons/ThrowableFlight.h"
#include "Weapons/WeaponLoadoutComponent.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Dark
{
    using Math::Matrix4f;
    using Math::Vector3f;

    namespace
    {
        constexpr float kHandMeters  = 0.22f;
        constexpr float kWorldMeters = 0.36f;

        bool modelFileExists(std::string_view modelPath)
        {
            namespace fs = std::filesystem;
            const fs::path virtualFile{ std::string(modelPath) };
            std::error_code ec;
            if (virtualFile.is_absolute() && fs::is_regular_file(virtualFile, ec) && !ec)
                return true;
            for (const fs::path& root : contentRootCandidates())
            {
                ec.clear();
                if (fs::is_regular_file(root / virtualFile, ec) && !ec)
                    return true;
            }
            return false;
        }

        void warnSkippedThrowableMesh(std::string_view modelPath)
        {
            static std::vector<std::string> logged;
            const std::string key(modelPath);
            for (const std::string& seen : logged)
            {
                if (seen == key)
                    return;
            }
            logged.push_back(key);
            if (key.empty())
                DE_LOG_WARN("Throwable: model missing; skipping mesh");
            else
                DE_LOG_WARN("Throwable: model '{}' missing; skipping mesh", key);
        }

        float fittedScale(const Model& model, float targetMeters)
        {
            if (!(targetMeters > 0.0f) || !model.bounds().IsValid())
                return 1.0f;
            const Vector3f size = model.bounds().Size();
            float          longest = size.x;
            if (size.y > longest)
                longest = size.y;
            if (size.z > longest)
                longest = size.z;
            if (!(longest > 1.0e-4f))
                return 1.0f;
            return targetMeters / longest;
        }

        void destroyPart(World& world, AssetPinTable& pins, Entity e)
        {
            if (!e.valid() || !world.alive(e))
                return;
            if (const ModelComponent* mc = world.get<ModelComponent>(e))
                unpinModelComponent(pins, *mc);
            world.destroyEntity(e);
        }

        void setPartVisible(World& world, Entity part, bool visible)
        {
            if (ModelComponent* mc = world.get<ModelComponent>(part))
                mc->visible = visible;
        }

        bool meshAttached(World& world, Entity part)
        {
            const ModelComponent* mc = world.get<ModelComponent>(part);
            return mc && mc->modelAssetID != NULL_ASSET;
        }

        Vector3f axis(const Matrix4f& m, int row)
        {
            const Math::Vector4f r = m.GetRow(row);
            Vector3f             v{ r.x, r.y, r.z };
            if (v.MagnitudeSqrd() > 1.0e-8f)
                v.Normalize();
            return v;
        }

        void syncOneHand(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, Entity actor, const WeaponLoadoutComponent& wlc)
        {
            WeaponKind kind = WeaponKind::Melee;
            if (wlc.loadout)
                kind = wlc.loadout->activeKind();
            std::string_view handId;
            if (const ThrowableBeltComponent* belt = world.get<ThrowableBeltComponent>(actor))
                handId = belt->belt.hand();
            const ThrowableHandChoice choice = throwableHandChoice(kind, handId);

            ThrowableHandComponent* hand = world.get<ThrowableHandComponent>(actor);
            if (!choice.shown)
            {
                if (hand)
                    setPartVisible(world, hand->mesh, false);
                return;
            }

            if (!hand)
                hand = &world.emplace<ThrowableHandComponent>(actor);

            const HealthComponent*    hp    = world.get<HealthComponent>(actor);
            const TransformComponent* owner = world.get<TransformComponent>(actor);
            const AnimGraphComponent* ag    = world.get<AnimGraphComponent>(actor);
            const Skeleton*           skel  = ag ? ag->graph.skeleton() : nullptr;
            const AnimPose*           pose  = ag ? &ag->graph.player().pose() : nullptr;
            const bool                alive = !hp || hp->health.alive();
            if (hand->joint < 0 && skel)
            {
                for (size_t j = 0; j < skel->joints.size(); ++j)
                {
                    if (skel->joints[j].name == choice.jointName)
                        hand->joint = static_cast<int>(j);
                }
            }

            const bool jointOk = pose && hand->joint >= 0 && static_cast<uint32_t>(hand->joint) < pose->boneCount;
            if (!alive || !owner || !jointOk)
            {
                setPartVisible(world, hand->mesh, false);
                return;
            }

            if (!hand->mesh.valid() || !world.alive(hand->mesh))
            {
                hand->mesh = world.createEntity();
                world.emplace<TagComponent>(hand->mesh, "ThrowableHand");
                world.emplace<TransformComponent>(hand->mesh);
            }

            const bool samePath = hand->modelPath == choice.modelPath;
            const bool haveMesh = meshAttached(world, hand->mesh);
            if (!samePath || !haveMesh)
            {
                if (samePath && !haveMesh)
                {
                    setPartVisible(world, hand->mesh, false);
                    return;
                }
                if (!attachThrowableMesh(world, pins, assets, renderer, hand->mesh, choice.modelPath, true))
                {
                    hand->modelPath = choice.modelPath;
                    setPartVisible(world, hand->mesh, false);
                    return;
                }
                if (const TransformComponent* xf = world.get<TransformComponent>(hand->mesh))
                    hand->scale = xf->scale.x;
                hand->modelPath = choice.modelPath;
            }

            TransformComponent* part = world.get<TransformComponent>(hand->mesh);
            if (!part)
                return;

            const Matrix4f ownerWorld = worldMatrix(*owner);
            const Matrix4f jointWorld = pose->jointWorld[hand->joint] * ownerWorld;
            const Vector3f grip       = jointWorld.GetTranslation();
            const Vector3f forward    = axis(jointWorld, 2);
            part->position            = grip + forward * 0.04f;
            part->rotation            = Math::Quaternion::FromLookRotation(forward, axis(jointWorld, 1));
            part->scale               = Vector3f{ hand->scale, hand->scale, hand->scale };
            setPartVisible(world, hand->mesh, true);
        }
    } // namespace

    ThrowableHandChoice throwableHandChoice(WeaponKind kind, std::string_view handId)
    {
        ThrowableHandChoice choice;
        choice.jointName = kThrowableHandJoint;
        choice.modelPath = throwableVisualModelPath(handId);
        choice.shown     = kind == WeaponKind::Throwable && !choice.modelPath.empty();
        return choice;
    }

    std::string throwableVisualModelPath(std::string_view itemId)
    {
        const ThrowableDef* def = throwableCatalog().find(itemId);
        if (!def || def->model.empty() || def->model == kThrowableUnitCube)
            return {};
        return def->model;
    }

    ThrowableMeshChoice throwableMeshChoice(std::string_view modelPath)
    {
        ThrowableMeshChoice choice;
        choice.allowThrow = true;
        if (modelPath.empty() || modelPath == kThrowableUnitCube)
            return choice;
        choice.attachMesh = modelFileExists(modelPath);
        return choice;
    }

    bool attachThrowableMesh(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, Entity entity, std::string_view modelPath, bool inHand)
    {
        if (!entity.valid() || !world.alive(entity))
            return false;
        const ThrowableMeshChoice mesh = throwableMeshChoice(modelPath);
        if (!mesh.attachMesh)
        {
            warnSkippedThrowableMesh(modelPath);
            if (world.get<ModelComponent>(entity))
            {
                ModelComponent cleared{};
                cleared.visible = false;
                setModelComponent(world, pins, assets, entity, cleared);
            }
            return false;
        }

        AssetRef<Model> model = loadAndUploadModel(renderer, assets, std::string(modelPath));
        if (!model || !model->valid())
        {
            warnSkippedThrowableMesh(modelPath);
            return false;
        }

        if (!world.get<TransformComponent>(entity))
            world.emplace<TransformComponent>(entity);
        if (TransformComponent* xf = world.get<TransformComponent>(entity))
        {
            const float scale = fittedScale(*model, inHand ? kHandMeters : kWorldMeters);
            xf->scale         = Vector3f{ scale, scale, scale };
        }

        ModelComponent mc{};
        mc.modelAssetID = model->id;
        mc.castShadow   = true;
        mc.visible      = true;
        setModelComponent(world, pins, assets, entity, mc);
        return true;
    }

    void syncThrowableHands(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer)
    {
        std::vector<Entity> actors;
        world.each<WeaponLoadoutComponent>([&](Entity e, WeaponLoadoutComponent&) { actors.push_back(e); });
        for (Entity actor : actors)
        {
            const WeaponLoadoutComponent* wlc = world.get<WeaponLoadoutComponent>(actor);
            if (wlc)
                syncOneHand(world, pins, assets, renderer, actor, *wlc);
        }
    }

    void syncThrowableFlightVisuals(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, const ThrowableFlights& flights)
    {
        std::vector<Entity> visuals;
        world.each<ThrowableFlightVisualComponent>([&](Entity e, ThrowableFlightVisualComponent&) { visuals.push_back(e); });

        const int live = flights.liveCount();
        while (static_cast<int>(visuals.size()) > live)
        {
            destroyPart(world, pins, visuals.back());
            visuals.pop_back();
        }
        while (static_cast<int>(visuals.size()) < live)
        {
            const Entity e = world.createEntity();
            world.emplace<TagComponent>(e, "Throwable");
            world.emplace<TransformComponent>(e);
            world.emplace<ThrowableFlightVisualComponent>(e);
            visuals.push_back(e);
        }

        for (int i = 0; i < live; ++i)
        {
            const Entity                     e      = visuals[static_cast<size_t>(i)];
            ThrowableFlightVisualComponent*  visual = world.get<ThrowableFlightVisualComponent>(e);
            TransformComponent*              xf     = world.get<TransformComponent>(e);
            if (!visual || !xf)
                continue;
            Vector3f position;
            if (!flights.livePosition(i, position))
                continue;
            xf->position = position;
            xf->rotation = Math::Quaternion::IDENTITY;

            const std::string_view itemId = flights.liveItemId(i);
            const std::string      path   = throwableVisualModelPath(itemId);
            const bool             same   = visual->modelPath == path;
            const bool             have   = meshAttached(world, e);
            if (!same || !have)
            {
                if (same && !have)
                {
                    xf->scale = Vector3f{ visual->scale, visual->scale, visual->scale };
                    continue;
                }
                if (!attachThrowableMesh(world, pins, assets, renderer, e, path, false))
                {
                    visual->modelPath = path;
                    visual->scale     = 0.3f;
                    xf->scale         = Vector3f{ 0.3f, 0.3f, 0.3f };
                    continue;
                }
                visual->modelPath = path;
                visual->scale     = xf->scale.x;
            }
            else
                xf->scale = Vector3f{ visual->scale, visual->scale, visual->scale };
        }
    }

    void clearThrowablePlayVisuals(World& world, AssetPinTable& pins)
    {
        std::vector<Entity> flights;
        world.each<ThrowableFlightVisualComponent>([&](Entity e, ThrowableFlightVisualComponent&) { flights.push_back(e); });
        for (Entity e : flights)
            destroyPart(world, pins, e);

        std::vector<Entity> owners;
        world.each<ThrowableHandComponent>([&](Entity e, ThrowableHandComponent&) { owners.push_back(e); });
        for (Entity owner : owners)
        {
            if (const ThrowableHandComponent* hand = world.get<ThrowableHandComponent>(owner))
                destroyPart(world, pins, hand->mesh);
            world.remove<ThrowableHandComponent>(owner);
        }
    }

} // namespace Dark
