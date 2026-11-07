#include "Play/ShadowRecord.h"

#include "Animation/AnimGraphComponent.h"
#include "Assets/AssetManager.h"
#include "Assets/Model.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Render/Camera3D.h"
#include "Render/DrawList.h"
#include "Render/FoliagePipeline.h"
#include "Render/FoliagePrototypes.h"
#include "Render/GpuResourceCache.h"
#include "Render/Mesh.h"
#include "Render/ModelDraw.h"
#include "Render/Renderer.h"
#include "Render/ShadowSystem.h"
#include "Render/SkinnedInstanceBuffer.h"
#include "Terrain/TerrainGrid.h"

namespace Dark
{
    using namespace Math;

    namespace
    {

        void dispatchFrameCull(ShadowCasterHost& host, ID3D12GraphicsCommandList* mainCmd, ID3D12GraphicsCommandList* depthCmd, bool separate)
        {
            if (!host.drawList || !host.drawList->gpuCullActive())
                return;

            if (!separate)
            {
                host.drawList->dispatchCull(mainCmd);
                if (host.instanceSkinned && host.skinned)
                {
                    CullViewDesc skinViews[kMaxStaticViews];
                    const uint32_t skinViewCount = host.drawList->exportGpuViews(skinViews, kMaxStaticViews);
                    host.skinned->dispatchCull(mainCmd, skinViews, skinViewCount);
                }
                return;
            }

            // Depth first. The camera dispatch continues the args/index state that dispatch stored.
            // endFrame executes the shadow list before the main list, so the GPU sees that order.
            const RenderPassKind depthPass  = RenderPassKind::Depth;
            const RenderPassKind cameraPass = RenderPassKind::GBuffer;
            host.drawList->dispatchCull(depthCmd, &depthPass, false);
            host.drawList->dispatchCull(mainCmd, &cameraPass, true);

            if (!host.instanceSkinned || !host.skinned)
                return;

            CullViewDesc exported[kMaxStaticViews];
            const uint32_t exportedCount = host.drawList->exportGpuViews(exported, kMaxStaticViews);
            CullViewDesc depthViews[kMaxStaticViews];
            CullViewDesc cameraViews[kMaxStaticViews];
            uint32_t depthCount  = 0;
            uint32_t cameraCount = 0;
            for (uint32_t i = 0; i < exportedCount; ++i)
            {
                if (exported[i].useDepthBatch != 0)
                    depthViews[depthCount++] = exported[i];
                else
                    cameraViews[cameraCount++] = exported[i];
            }
            if (depthCount == 0 && cameraCount == 0)
                return;
            if (!host.skinned->uploadCullFrame())
                return;
            if (depthCount != 0)
                host.skinned->dispatchPreparedCull(depthCmd, depthViews, depthCount);
            if (cameraCount != 0)
                host.skinned->dispatchPreparedCull(mainCmd, cameraViews, cameraCount);
        }

    }

    void ensureDirectionalShadowModels(ShadowCasterHost& host)
    {
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
            const auto model = host.assets->getAs<Model>(mc.modelAssetID);
            if (!model || !model->valid())
                return;
            host.gpu->ensureModel(model);
        });
    }

    void recordShadowCasterView(
        ShadowCasterHost& host,
        ID3D12GraphicsCommandList* cmd,
        const Matrix4f& lightViewProj,
        const Frustum3f& frustum,
        const Sphere3f* foliageSphere,
        bool beginFoliage,
        bool profileFoliageCull,
        uint8_t& shadowViewIndex)
    {
        if (!cmd)
            return;

        if (host.drawTerrain && host.terrain)
            host.terrain->drawDepth(cmd, &frustum);
        else if (host.allowGround && host.ground && host.ground->valid() && frustum.Intersects(host.groundBounds))
            host.ground->draw(cmd);

        const bool recordThisView = host.instanceStatic && host.drawList && shadowViewIndex < kMaxStaticViews;
        const uint32_t skinView   = shadowViewIndex;
        if (recordThisView)
        {
            float lightVp[16];
            lightViewProj.store(lightVp);
            const ViewId view{ shadowViewIndex };
            if (!host.drawList->gpuCullActive())
                host.drawList->buildView(frustum, lightVp, lightVp, RenderPassKind::Depth, view);
            host.drawList->recordDepth(cmd, view, lightVp);
            ++shadowViewIndex;
            if (host.shadows)
                host.shadows->pipeline().bind(cmd);
        }
        if (host.instanceSkinned && host.skinned)
        {
            float skinVp[16];
            lightViewProj.store(skinVp);
            host.skinned->recordDepth(cmd, skinView, skinVp);
            if (host.shadows)
                host.shadows->pipeline().bind(cmd);
        }
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
                const auto model = host.assets->getAs<Model>(mc.modelAssetID);
                if (!xf || !model || !model->valid())
                    return;
                if (host.rejectTinyScale && xf->scale.MagnitudeSqrd() < 1.0e-12f)
                    return;
                const Matrix4f worldMat = worldMatrix(*xf);
                if (model->bounds().IsValid() && !frustum.Intersects(model->bounds().Transformed(worldMat)))
                    return;
                if (!host.gpu->ensureModel(model))
                    return;
                const bool accepted = recordThisView && host.acceptedModel && host.acceptedModel->count(e.id()) != 0;
                const bool skinnedListed = host.instanceSkinned && host.acceptedSkinned && host.acceptedSkinned->count(e.id()) != 0;
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
            if (beginFoliage)
                host.foliage->beginDepthView(*host.renderer, *host.assets, *host.foliagePrototypes, *host.terrain, host.foliageRecords, *host.foliageDensity, *host.camera, frustum, foliageSphere);
            host.foliage->drawPreparedDepth(cmd, lightViewProj, profileFoliageCull);
        }
    }

    void recordDirectionalShadows(ShadowCasterHost& host, ID3D12GraphicsCommandList* mainCmd, uint8_t& shadowViewIndex)
    {
        if (!host.renderer || !mainCmd)
            return;

        ID3D12GraphicsCommandList* depthCmd = mainCmd;
        bool separate = false;
        if (host.renderer->shadowSeparateList())
        {
            if (ID3D12GraphicsCommandList* shadowCmd = host.renderer->beginShadowList())
            {
                depthCmd = shadowCmd;
                separate = true;
            }
        }

        dispatchFrameCull(host, mainCmd, depthCmd, separate);

        if (host.shadows && host.shadows->isValid() && host.shadows->enabled())
        {
            host.shadows->beginCapture(depthCmd);
            for (int i = 0; i < host.shadows->cascadeCount(); ++i)
            {
                host.shadows->beginCascade(depthCmd, i);
                const Frustum3f casterFrustum(host.shadows->cascade(i).viewProj);
                recordShadowCasterView(host, depthCmd, host.shadows->cascade(i).viewProj, casterFrustum, nullptr, true, !separate, shadowViewIndex);
            }
            host.shadows->endCapture(depthCmd);
        }
        else if (host.shadows && host.shadows->isValid())
        {
            host.shadows->endCapture(depthCmd);
        }

        if (separate)
            host.renderer->closeShadowList();
    }

} // namespace Dark
