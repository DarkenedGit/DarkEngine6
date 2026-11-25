#include "Play/ShadowCasters.h"

#include "Animation/AnimGraphComponent.h"
#include "Assets/AssetManager.h"
#include "Assets/Model.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/MathHelper.h"
#include "Render/Camera3D.h"
#include "Render/GpuResourceCache.h"
#include "Render/Mesh.h"
#include "Render/ModelDraw.h"
#include "Render/Renderer.h"
#include "Render/ShadowSystem.h"
#include "Render/SkinnedMeshPipeline.h"
#include "Render/SkinningUploadRing.h"
#include "Terrain/FoliagePipeline.h"
#include "Terrain/FoliagePrototypes.h"
#include "Terrain/TerrainGrid.h"

namespace Dark
{
    using namespace Math;

    namespace
    {

        void pushOpaqueStatic(const GpuModel& model, const Matrix4f& worldMat)
        {
            for (const GpuModel::Part& part : model.opaque())
            {
                if (part.skinned || !part.mesh.valid())
                    continue;
                pushShadowCaster(&part.mesh, part.localToRoot * worldMat, 0);
            }
        }

        void pushSkinnedOnly(const GpuModel& model, const AnimPose& pose, const Matrix4f& worldMat, SkinningUploadRing& ring, const SkinnedMeshPipeline& pipeline)
        {
            if (!pipeline.isValid())
                return;
            const uint64_t bones = ring.alloc(pose);
            if (bones == 0)
                return;
            for (const GpuModel::Part& part : model.opaque())
            {
                if (!part.skinned || !part.mesh.valid())
                    continue;
                pushShadowCaster(&part.mesh, part.localToRoot * worldMat, bones);
            }
        }

        void pushSkinnedAndStatic(const GpuModel& model, const AnimPose& pose, const Matrix4f& worldMat, SkinningUploadRing& ring, const SkinnedMeshPipeline& pipeline)
        {
            const uint64_t bones = pipeline.isValid() ? ring.alloc(pose) : 0;
            for (const GpuModel::Part& part : model.opaque())
            {
                if (!part.mesh.valid())
                    continue;
                if (part.skinned)
                {
                    if (bones == 0)
                        continue;
                    pushShadowCaster(&part.mesh, part.localToRoot * worldMat, bones);
                }
                else
                    pushShadowCaster(&part.mesh, part.localToRoot * worldMat, 0);
            }
        }

        // What a kicked job draws on the worker. Join is before endFrame, so main refills this only
        // after the job returns.
        struct WorkerCasters
        {
            const Terrain::TerrainGrid* terrain = nullptr;
            FoliagePipeline*            foliage = nullptr;
            GpuResourceCache*           gpu     = nullptr;
        };
        WorkerCasters g_worker;

        void drawWorkerGround(void* user, ID3D12GraphicsCommandList* cmd, const Frustum3f& frustum)
        {
            const WorkerCasters* worker = static_cast<const WorkerCasters*>(user);
            if (worker && worker->terrain)
                worker->terrain->drawDepth(cmd, &frustum, false);
        }

        void drawWorkerFoliage(void* user, ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj, uint32_t retained)
        {
            const WorkerCasters* worker = static_cast<const WorkerCasters*>(user);
            if (worker && worker->foliage && worker->gpu)
                worker->foliage->drawRetainedDepth(cmd, *worker->gpu, viewProj, retained);
        }

    }

    void GameplayShadowCasters::prepareDirectional()
    {
        const ShadowCasterHost& host = m_host;
        if (!host.world || !host.assets || !host.gpu)
            return;
        host.world->each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (!mc.visible || !mc.castShadow)
                return;
            if (host.skipModel && host.skipModel(host.user, e))
                return;
            const TransformComponent* xf = host.world->get<TransformComponent>(e);
            if (!xf)
                return;
            if (host.rejectTinyScale && xf->scale.MagnitudeSqrd() < 1.0e-12f)
                return;
            const Model* model = host.assets->find<Model>(mc.modelAssetID);
            if (!model || !model->valid())
                return;
            host.gpu->retainModel(*host.assets, *model);
        });
    }

    void GameplayShadowCasters::drawGround(ID3D12GraphicsCommandList* cmd, const Frustum3f& frustum)
    {
        const ShadowCasterHost& host = m_host;
        if (host.drawTerrain && host.terrain)
            host.terrain->drawDepth(cmd, &frustum);
        else if (host.allowGround && host.ground && host.ground->valid() && frustum.Intersects(host.groundBounds))
            host.ground->draw(cmd);
    }

    void GameplayShadowCasters::drawCasters(ID3D12GraphicsCommandList* cmd, const ShadowCasterView& view)
    {
        const ShadowCasterHost& host           = m_host;
        const Matrix4f&         lightViewProj  = *view.viewProj;
        const Frustum3f&        frustum        = *view.frustum;
        const bool              recordThisView = view.instanced;
        if (host.drawPrimitives)
            host.drawPrimitives(host.user, cmd, lightViewProj, frustum, recordThisView);

        if (host.world && host.assets && host.gpu && host.shadows)
        {
            host.world->each<ModelComponent>([&](Entity e, ModelComponent& mc) {
                if (!mc.visible || !mc.castShadow)
                    return;
                if (host.skipModel && host.skipModel(host.user, e))
                    return;
                const TransformComponent* xf = host.world->get<TransformComponent>(e);
                const Model* model = host.assets->find<Model>(mc.modelAssetID);
                if (!xf || !model || !model->valid())
                    return;
                if (host.rejectTinyScale && xf->scale.MagnitudeSqrd() < 1.0e-12f)
                    return;
                const Matrix4f worldMat = worldMatrix(*xf);
                if (model->bounds().IsValid() && !frustum.Intersects(model->bounds().Transformed(worldMat)))
                    return;
                if (!host.gpu->retainModel(*host.assets, *model))
                    return;
                const bool accepted = recordThisView && host.acceptedModel && host.acceptedModel->contains(e.id());
                const bool skinnedListed = host.instanceSkinned && host.acceptedSkinned && host.acceptedSkinned->contains(e.id());
                if (model->skinned())
                {
                    const AnimPose* pose = skinnedPose(*model, host.world->get<AnimGraphComponent>(e));
                    if (accepted && skinnedListed)
                        return;
                    if (accepted)
                    {
                        if (pose && host.skinnedShadow && host.skinRing)
                            drawSkinnedModelDepthSkinnedOnly(cmd, *host.gpu, host.shadows->pipeline(), lightViewProj, *host.skinnedShadow, *host.skinRing, *model, *pose, worldMat);
                        return;
                    }
                    if (skinnedListed)
                    {
                        drawModelDepth(cmd, *host.gpu, host.shadows->pipeline(), lightViewProj, *model, worldMat);
                        return;
                    }
                    if (pose && host.skinnedShadow && host.skinRing)
                        drawSkinnedModelDepth(cmd, *host.gpu, host.shadows->pipeline(), lightViewProj, *host.skinnedShadow, *host.skinRing, *model, *pose, worldMat);
                    else
                        drawModelDepth(cmd, *host.gpu, host.shadows->pipeline(), lightViewProj, *model, worldMat);
                }
                else if (!accepted)
                    drawModelDepth(cmd, *host.gpu, host.shadows->pipeline(), lightViewProj, *model, worldMat);
            });
        }

        if (host.foliageActive && host.foliage && host.renderer && host.assets && host.foliagePrototypes && host.terrain && host.terrain->valid() && host.foliageDensity && host.camera)
        {
            if (view.beginFoliage)
            {
                // A point light culls foliage once against its range sphere and redraws that list on each face.
                Sphere3f        pointSphere;
                const Sphere3f* foliageSphere = nullptr;
                if (view.light && host.world)
                {
                    const LocalLightComponent* light = host.world->get<LocalLightComponent>(*view.light);
                    const TransformComponent*  xf    = host.world->get<TransformComponent>(*view.light);
                    if (light && xf && light->type == LocalLightType::Point)
                    {
                        pointSphere   = Sphere3f(xf->position, Min(light->range, 80.0f));
                        foliageSphere = &pointSphere;
                    }
                }
                host.foliage->beginDepthView(*host.renderer, *host.assets, *host.foliagePrototypes, *host.terrain, host.foliageRecords, *host.foliageDensity, *host.camera, frustum, foliageSphere);
            }
            host.foliage->drawPreparedDepth(cmd, lightViewProj);
        }
    }

    bool GameplayShadowCasters::workerReady() const
    {
        const ShadowCasterHost& host = m_host;
        return !(host.foliage && host.foliage->cullReady() && !host.foliage->depthResourceSetReady());
    }

    void GameplayShadowCasters::beginWorkerCapture()
    {
        if (m_host.foliage)
            m_host.foliage->clearRetainedDepth();
    }

    bool GameplayShadowCasters::groundOnWorker() const
    {
        return m_host.drawTerrain && m_host.terrain != nullptr;
    }

    void GameplayShadowCasters::captureGround(const Frustum3f& frustum)
    {
        const ShadowCasterHost& host = m_host;
        if (host.allowGround && host.ground && host.ground->valid() && frustum.Intersects(host.groundBounds))
            pushShadowCaster(host.ground, Matrix4f::IDENTITY, 0);
    }

    void GameplayShadowCasters::captureCasters(const ShadowCasterView& view)
    {
        const ShadowCasterHost& host           = m_host;
        const Frustum3f&        frustum        = *view.frustum;
        const bool              recordThisView = view.instanced;
        if (host.drawPrimitives)
            host.drawPrimitives(host.user, nullptr, *view.viewProj, frustum, recordThisView);

        if (!host.world || !host.assets || !host.gpu || !host.shadows)
            return;
        host.world->each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (!mc.visible || !mc.castShadow)
                return;
            if (host.skipModel && host.skipModel(host.user, e))
                return;
            const TransformComponent* xf = host.world->get<TransformComponent>(e);
            const Model* model = host.assets->find<Model>(mc.modelAssetID);
            if (!xf || !model || !model->valid())
                return;
            if (host.rejectTinyScale && xf->scale.MagnitudeSqrd() < 1.0e-12f)
                return;
            const Matrix4f worldMat = worldMatrix(*xf);
            if (model->bounds().IsValid() && !frustum.Intersects(model->bounds().Transformed(worldMat)))
                return;
            // prepareDirectional already ran on main. A missing GPU model is skipped, not created here.
            GpuModel* gm = host.gpu->model(model->id);
            if (!gm || !gm->hasOpaque())
                return;
            const bool accepted = recordThisView && host.acceptedModel && host.acceptedModel->contains(e.id());
            const bool skinnedListed = host.instanceSkinned && host.acceptedSkinned && host.acceptedSkinned->contains(e.id());
            if (model->skinned())
            {
                const AnimPose* pose = skinnedPose(*model, host.world->get<AnimGraphComponent>(e));
                if (accepted && skinnedListed)
                    return;
                if (accepted)
                {
                    if (pose && host.skinnedShadow && host.skinRing)
                        pushSkinnedOnly(*gm, *pose, worldMat, *host.skinRing, *host.skinnedShadow);
                    return;
                }
                if (skinnedListed)
                {
                    pushOpaqueStatic(*gm, worldMat);
                    return;
                }
                if (pose && host.skinnedShadow && host.skinRing)
                    pushSkinnedAndStatic(*gm, *pose, worldMat, *host.skinRing, *host.skinnedShadow);
                else
                    pushOpaqueStatic(*gm, worldMat);
            }
            else if (!accepted)
                pushOpaqueStatic(*gm, worldMat);
        });
    }

    uint32_t GameplayShadowCasters::retainFoliage(const Frustum3f& frustum)
    {
        const ShadowCasterHost& host = m_host;
        if (!host.foliageActive || !host.foliage || !host.renderer || !host.assets || !host.foliagePrototypes)
            return kNoRetainedFoliage;
        if (!host.terrain || !host.terrain->valid() || !host.foliageDensity || !host.camera)
            return kNoRetainedFoliage;
        if (!host.foliage->beginDepthView(*host.renderer, *host.assets, *host.foliagePrototypes, *host.terrain, host.foliageRecords, *host.foliageDensity, *host.camera, frustum, nullptr))
            return kNoRetainedFoliage;
        return host.foliage->sealDepthView();
    }

    ShadowWorkerDraws GameplayShadowCasters::workerDraws()
    {
        g_worker.terrain = m_host.terrain;
        g_worker.foliage = m_host.foliage;
        g_worker.gpu     = m_host.gpu;
        ShadowWorkerDraws draws;
        draws.ground  = &drawWorkerGround;
        draws.foliage = &drawWorkerFoliage;
        draws.user    = &g_worker;
        return draws;
    }

    ShadowFrame shadowFrameOf(const ShadowCasterHost& host, IShadowCasterSource* casters)
    {
        ShadowFrame frame;
        frame.renderer        = host.renderer;
        frame.jobs            = host.jobs;
        frame.drawList        = host.drawList;
        frame.skinned         = host.skinned;
        frame.instanceStatic  = host.instanceStatic;
        frame.instanceSkinned = host.instanceSkinned;
        frame.casters         = casters;
        return frame;
    }

} // namespace Dark
