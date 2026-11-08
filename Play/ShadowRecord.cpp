#include "Play/ShadowRecord.h"

#include "Animation/AnimGraphComponent.h"
#include "Assets/AssetManager.h"
#include "Assets/Model.h"
#include "Core/JobSystem.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/FrameRecord.h"
#include "Render/Camera3D.h"
#include "Render/CullResourceSet.h"
#include "Render/DrawList.h"
#include "Render/FoliagePipeline.h"
#include "Render/FoliagePrototypes.h"
#include "Render/GpuResourceCache.h"
#include "Render/Mesh.h"
#include "Render/ModelDraw.h"
#include "Render/Renderer.h"
#include "Render/ShadowSystem.h"
#include "Render/SkinnedInstanceBuffer.h"
#include "Render/SkinnedMeshPipeline.h"
#include "Render/SkinningUploadRing.h"
#include "Terrain/TerrainGrid.h"

#include <vector>

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

    namespace
    {

        struct ShadowCaster
        {
            const Mesh* mesh  = nullptr;
            Matrix4f    world = Matrix4f::IDENTITY;
            uint64_t    bones = 0;
        };

        struct CascadeRecord
        {
            Matrix4f  viewProj = Matrix4f::IDENTITY;
            Frustum3f frustum;
            uint32_t  viewIndex   = 0;
            uint32_t  skinView    = 0;
            uint32_t  casterBegin = 0;
            uint32_t  groundEnd   = 0;
            uint32_t  casterEnd   = 0;
            uint32_t  foliage     = ~0u;
            bool      recordStatic = false;
            bool      recordSkin   = false;
            bool      drawTerrain  = false;
        };

        struct ShadowJob
        {
            bool                       kicked = false;
            ID3D12GraphicsCommandList* list   = nullptr;
            ID3D12CommandAllocator*    alloc  = nullptr;
            Renderer*                  renderer = nullptr;
            ShadowSystem*              shadows  = nullptr;
            DrawList*                  drawList = nullptr;
            SkinnedInstanceBuffer*     skinned  = nullptr;
            const Terrain::TerrainGrid* terrain = nullptr;
            FoliagePipeline*           foliage  = nullptr;
            GpuResourceCache*          gpu      = nullptr;
            const SkinnedMeshPipeline* skinnedShadow = nullptr;
            bool                       skinUploadOk  = false;
            uint32_t                   cascadeBegin  = 0;
            uint32_t                   cascadeEnd    = 0;
            uint32_t                   skinDepthCount = 0;
            CullViewDesc               skinDepth[kMaxStaticViews]{};
            std::vector<ShadowCaster>  casters;
            std::vector<CascadeRecord> cascades;
        };

        // Join is before endFrame, so main refills this only after the job returns.
        ShadowJob g_job;
        uint32_t  g_kickedFrame = 0;
        std::vector<ShadowCaster>* g_capture = nullptr;

        void pushCaster(const Mesh* mesh, const Matrix4f& world, uint64_t bones)
        {
            if (!g_capture || !mesh)
                return;
            ShadowCaster caster;
            caster.mesh  = mesh;
            caster.world = world;
            caster.bones = bones;
            g_capture->push_back(caster);
        }

        void pushOpaqueStatic(const GpuModel& model, const Matrix4f& worldMat)
        {
            for (const GpuModel::Part& part : model.opaque())
            {
                if (part.skinned || !part.mesh.valid())
                    continue;
                pushCaster(&part.mesh, part.localToRoot * worldMat, 0);
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
                pushCaster(&part.mesh, part.localToRoot * worldMat, bones);
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
                    pushCaster(&part.mesh, part.localToRoot * worldMat, bones);
                }
                else
                    pushCaster(&part.mesh, part.localToRoot * worldMat, 0);
            }
        }

        void collectModelCasters(ShadowCasterHost& host, const Frustum3f& frustum, bool recordThisView)
        {
            if (!host.world || !host.assets || !host.gpu || !host.shadows)
                return;
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
                // ensureModel already ran on main. A missing GPU model is skipped, not created here.
                GpuModel* gm = host.gpu->model(model->id);
                if (!gm || !gm->hasOpaque())
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

        uint32_t sealCascadeFoliage(ShadowCasterHost& host, const Frustum3f& frustum)
        {
            if (!host.foliageActive || !host.foliage || !host.renderer || !host.assets || !host.foliagePrototypes)
                return ~0u;
            if (!host.terrain || !host.terrain->valid() || !host.foliageDensity || !host.camera)
                return ~0u;
            if (!host.foliage->beginDepthView(*host.renderer, *host.assets, *host.foliagePrototypes, *host.terrain, host.foliageRecords, *host.foliageDensity, *host.camera, frustum, nullptr))
                return ~0u;
            return host.foliage->sealDepthView();
        }

        void replayCasters(const ShadowJob& job, ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj, uint32_t begin, uint32_t end)
        {
            bool staticBound = false;
            bool skinBound   = false;
            for (uint32_t i = begin; i < end && i < job.casters.size(); ++i)
            {
                const ShadowCaster& caster = job.casters[i];
                if (!caster.mesh || !caster.mesh->valid())
                    continue;
                const Matrix4f wvp = caster.world * viewProj;
                if (caster.bones != 0)
                {
                    if (!job.skinnedShadow || !job.skinnedShadow->isValid())
                        continue;
                    if (!skinBound)
                    {
                        job.skinnedShadow->bind(cmd);
                        skinBound   = true;
                        staticBound = false;
                    }
                    job.skinnedShadow->setBoneCbv(cmd, static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(caster.bones));
                    job.skinnedShadow->setWvp(cmd, wvp.m_afEntry);
                }
                else
                {
                    if (!staticBound)
                    {
                        job.shadows->pipeline().bind(cmd);
                        staticBound = true;
                        skinBound   = false;
                    }
                    job.shadows->pipeline().setWvp(cmd, wvp.m_afEntry);
                }
                caster.mesh->draw(cmd);
            }
        }

        JobStatus shadowJobFn(void* user)
        {
            ShadowJob* job = static_cast<ShadowJob*>(user);
            if (!job)
                return JobStatus::Failed;

            JobStatus status = JobStatus::Failed;
            if (!job->list || !job->alloc || !job->renderer || !job->shadows)
            {
                job->kicked = false;
                return status;
            }

            // This job is the only Reset. beginShadowList on main would Reset the same allocator.
            const HRESULT allocHr = job->alloc->Reset();
            if (FAILED(allocHr))
            {
                DE_LOG_ERROR(LogCategory::Render, "Shadow worker allocator Reset failed (HRESULT 0x{:08X})", static_cast<unsigned>(allocHr));
                job->kicked = false;
                return status;
            }
            const HRESULT listHr = job->list->Reset(job->alloc, nullptr);
            if (FAILED(listHr))
            {
                DE_LOG_ERROR(LogCategory::Render, "Shadow worker list Reset failed (HRESULT 0x{:08X})", static_cast<unsigned>(listHr));
                job->kicked = false;
                return status;
            }
            job->renderer->noteShadowListReset();

            ID3D12GraphicsCommandList* cmd = job->list;
            if (job->drawList && job->drawList->gpuCullActive() && job->cascadeEnd > job->cascadeBegin)
            {
                const RenderPassKind depthPass = RenderPassKind::Depth;
                job->drawList->dispatchCull(cmd, &depthPass, false, CullResourceSet::Depth, job->cascadeBegin, job->cascadeEnd);
            }
            if (job->skinUploadOk && job->skinned && job->skinDepthCount != 0)
                job->skinned->dispatchPreparedCull(cmd, job->skinDepth, job->skinDepthCount, CullResourceSet::Depth);

            job->shadows->beginCapture(cmd, false);
            for (uint32_t i = 0; i < job->cascades.size(); ++i)
            {
                const CascadeRecord& rec = job->cascades[i];
                job->shadows->beginCascade(cmd, static_cast<int>(i), false);
                if (rec.drawTerrain && job->terrain)
                    job->terrain->drawDepth(cmd, &rec.frustum, false);
                else
                    replayCasters(*job, cmd, rec.viewProj, rec.casterBegin, rec.groundEnd);
                if (rec.recordStatic && job->drawList)
                {
                    float lightVp[16];
                    rec.viewProj.store(lightVp);
                    job->drawList->recordDepth(cmd, ViewId{ static_cast<uint8_t>(rec.viewIndex) }, lightVp);
                    job->shadows->pipeline().bind(cmd);
                }
                if (rec.recordSkin && job->skinned)
                {
                    float skinVp[16];
                    rec.viewProj.store(skinVp);
                    job->skinned->recordDepth(cmd, rec.skinView, skinVp);
                    job->shadows->pipeline().bind(cmd);
                }
                replayCasters(*job, cmd, rec.viewProj, rec.groundEnd, rec.casterEnd);
                if (rec.foliage != ~0u && job->foliage && job->gpu)
                    job->foliage->drawRetainedDepth(cmd, *job->gpu, rec.viewProj, rec.foliage);
            }
            job->shadows->endCapture(cmd);

            const HRESULT closeHr = cmd->Close();
            if (FAILED(closeHr))
            {
                // Leave the list open so endFrame's abandoned Close still runs.
                DE_LOG_ERROR(LogCategory::Render, "Shadow worker list Close failed (HRESULT 0x{:08X})", static_cast<unsigned>(closeHr));
                job->kicked = false;
                return status;
            }
            job->renderer->adoptClosedShadowList();
            job->kicked = false;
            return JobStatus::Ok;
        }

        bool depthSetsReady(const ShadowCasterHost& host)
        {
            if (host.drawList && !host.drawList->depthResourceSetReady())
                return false;
            if (host.foliage && host.foliage->cullReady() && !host.foliage->depthResourceSetReady())
                return false;
            if (host.instanceSkinned && host.skinned && host.skinned->cullReady() && !host.skinned->depthResourceSetReady())
                return false;
            return true;
        }

        void logMissingDepthSet()
        {
            static bool logged = false;
            if (logged)
                return;
            logged = true;
            DE_LOG_WARN(LogCategory::Render, "shadowWorker ignored; depth views have no resource set of their own");
        }

        void dispatchCameraSide(ShadowCasterHost& host, ID3D12GraphicsCommandList* mainCmd, uint32_t cascadeBegin, uint32_t cascadeEnd, bool skinUploadOk)
        {
            if (!mainCmd)
                return;
            if (host.drawList && host.drawList->gpuCullActive())
            {
                const RenderPassKind depthPass  = RenderPassKind::Depth;
                const RenderPassKind cameraPass = RenderPassKind::GBuffer;
                host.drawList->dispatchCull(mainCmd, &depthPass, false, CullResourceSet::Camera, cascadeEnd, kMaxStaticViews);
                host.drawList->dispatchCull(mainCmd, &cameraPass, true, CullResourceSet::Camera);
            }
            if (!skinUploadOk || !host.instanceSkinned || !host.skinned || !host.drawList)
                return;
            CullViewDesc exported[kMaxStaticViews];
            const uint32_t count = host.drawList->exportGpuViews(exported, kMaxStaticViews);
            CullViewDesc mainViews[kMaxStaticViews];
            uint32_t mainCount = 0;
            for (uint32_t i = 0; i < count; ++i)
            {
                const bool cascade = exported[i].useDepthBatch != 0 && exported[i].viewIndex >= cascadeBegin && exported[i].viewIndex < cascadeEnd;
                if (cascade)
                    continue;
                if (mainCount < kMaxStaticViews)
                    mainViews[mainCount++] = exported[i];
            }
            if (mainCount != 0)
                host.skinned->dispatchPreparedCull(mainCmd, mainViews, mainCount, CullResourceSet::Camera);
        }

        void fillSkinDepth(ShadowCasterHost& host, uint32_t cascadeBegin, uint32_t cascadeEnd)
        {
            g_job.skinDepthCount = 0;
            if (!host.drawList || !host.instanceSkinned || !host.skinned)
                return;
            CullViewDesc exported[kMaxStaticViews];
            const uint32_t count = host.drawList->exportGpuViews(exported, kMaxStaticViews);
            for (uint32_t i = 0; i < count; ++i)
            {
                if (exported[i].useDepthBatch == 0)
                    continue;
                if (exported[i].viewIndex < cascadeBegin || exported[i].viewIndex >= cascadeEnd)
                    continue;
                if (g_job.skinDepthCount >= kMaxStaticViews)
                    break;
                g_job.skinDepth[g_job.skinDepthCount++] = exported[i];
            }
        }

        // True when the caller must not also record on the main list.
        bool kickShadowWorker(ShadowCasterHost& host, ID3D12GraphicsCommandList* mainCmd, uint8_t& shadowViewIndex)
        {
            if (!shadowWorker() || !host.jobs || !host.renderer || !host.renderer->shadowSeparateList())
                return false;
            if (!host.shadows || !host.shadows->isValid() || !host.shadows->enabled())
                return false;
            if (!depthSetsReady(host))
            {
                logMissingDepthSet();
                return false;
            }

            const uint32_t frame = host.renderer->frameIndex();
            // A rejected kick leaves kicked set. The next frame may record; a live job may not.
            const bool liveJob = host.jobs->renderBusy(0) || host.jobs->renderBusy(1);
            if (g_job.kicked && g_kickedFrame != frame && !liveJob)
                g_job.kicked = false;
            if (g_job.kicked || host.jobs->renderBusy(frame))
                return true;

            host.jobs->start();
            if (!host.jobs->frameIsPublished(frame))
            {
                if (!host.jobs->publishFrame(frame))
                    return true;
            }

            ID3D12GraphicsCommandList* list  = host.renderer->shadowCommandList();
            ID3D12CommandAllocator*    alloc = host.renderer->shadowCommandAllocator();
            if (!list || !alloc)
                return false;

            if (host.drawList)
                host.drawList->ensureCompiled();
            if (host.skinned)
                host.skinned->ensureCompiled();

            bool skinUploadOk = false;
            if (host.instanceSkinned && host.skinned && host.skinned->cullReady())
                skinUploadOk = host.skinned->uploadCullFrame();
            if (host.foliage)
                host.foliage->clearRetainedDepth();

            g_job.casters.clear();
            g_job.cascades.clear();
            g_job.list          = list;
            g_job.alloc         = alloc;
            g_job.renderer      = host.renderer;
            g_job.shadows       = host.shadows;
            g_job.drawList      = host.drawList;
            g_job.skinned       = host.skinned;
            g_job.terrain       = host.terrain;
            g_job.foliage       = host.foliage;
            g_job.gpu           = host.gpu;
            g_job.skinnedShadow = host.skinnedShadow;
            g_job.skinUploadOk  = skinUploadOk;
            g_job.cascadeBegin  = shadowViewIndex;
            g_capture           = &g_job.casters;

            const int cascadeCount = host.shadows->cascadeCount();
            for (int i = 0; i < cascadeCount; ++i)
            {
                const Matrix4f& viewProj = host.shadows->cascade(i).viewProj;
                const Frustum3f frustum(viewProj);
                CascadeRecord rec;
                rec.viewProj     = viewProj;
                rec.frustum      = frustum;
                rec.drawTerrain  = host.drawTerrain && host.terrain != nullptr;
                rec.casterBegin  = static_cast<uint32_t>(g_job.casters.size());
                if (!rec.drawTerrain && host.allowGround && host.ground && host.ground->valid() && frustum.Intersects(host.groundBounds))
                    pushCaster(host.ground, Matrix4f::IDENTITY, 0);
                rec.groundEnd    = static_cast<uint32_t>(g_job.casters.size());
                rec.recordStatic = host.instanceStatic && host.drawList && shadowViewIndex < kMaxStaticViews;
                rec.viewIndex    = shadowViewIndex;
                rec.skinView     = shadowViewIndex;
                rec.recordSkin   = host.instanceSkinned && host.skinned != nullptr;
                if (rec.recordStatic)
                {
                    if (!host.drawList->gpuCullActive())
                    {
                        float lightVp[16];
                        viewProj.store(lightVp);
                        host.drawList->buildView(frustum, lightVp, lightVp, RenderPassKind::Depth, ViewId{ shadowViewIndex });
                    }
                    ++shadowViewIndex;
                }
                if (host.drawPrimitives)
                    host.drawPrimitives(host.user, nullptr, viewProj, frustum, rec.recordStatic);
                collectModelCasters(host, frustum, rec.recordStatic);
                rec.casterEnd = static_cast<uint32_t>(g_job.casters.size());
                rec.foliage   = sealCascadeFoliage(host, frustum);
                g_job.cascades.push_back(rec);
            }
            g_capture          = nullptr;
            g_job.cascadeEnd   = shadowViewIndex;
            if (host.drawList && g_job.cascadeEnd > g_job.cascadeBegin)
                host.drawList->armResourceSet(g_job.cascadeBegin, g_job.cascadeEnd, CullResourceSet::Depth);
            fillSkinDepth(host, g_job.cascadeBegin, g_job.cascadeEnd);

            g_job.kicked      = true;
            g_kickedFrame     = frame;
            PassDesc pass{};
            pass.name            = "DirectionalShadows";
            pass.order           = 0;
            pass.writes          = AccessListShadow;
            pass.recordOffThread = true;
            pass.record          = &shadowJobFn;
            pass.user            = &g_job;
            if (!registerFramePass(pass))
            {
                if (host.jobs->renderBusy(frame))
                {
                    g_job.kicked = false;
                    DE_LOG_ERROR(LogCategory::Render, "DirectionalShadows pass rejected while the slot is busy");
                    return true;
                }
                shadowJobFn(&g_job);
            }
            else
            {
                // The frame is already published. Start the worker before the rest of the main list.
                kickFramePasses(*host.jobs, frame);
            }
            dispatchCameraSide(host, mainCmd, g_job.cascadeBegin, g_job.cascadeEnd, skinUploadOk);
            return true;
        }

    }

    bool capturingShadowCasters()
    {
        return g_capture != nullptr;
    }

    void pushShadowCaster(const Mesh* mesh, const Matrix4f& world)
    {
        pushCaster(mesh, world, 0);
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
        if (kickShadowWorker(host, mainCmd, shadowViewIndex))
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
