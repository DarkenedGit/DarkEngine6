#include "Render/ShadowFeature.h"

#include "Core/JobSystem.h"
#include "Core/Log.h"
#include "Render/CullResourceSet.h"
#include "Render/DrawList.h"
#include "Render/FrameRecord.h"
#include "Render/LocalShadowSystem.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"
#include "Render/ShadowInputs.h"
#include "Render/ShadowSystem.h"
#include "Render/SkinnedInstanceBuffer.h"
#include "Render/SkinnedMeshPipeline.h"

#include <vector>

namespace Dark
{
    using namespace Math;

    namespace
    {

        // Every view's cull on the main list: the camera view feeds the G-buffer, the depth views the
        // shadow passes after it.
        void dispatchFrameCull(const ShadowFrame& host, ID3D12GraphicsCommandList* mainCmd)
        {
            if (!host.drawList || !host.drawList->gpuCullActive())
                return;
            host.drawList->dispatchCull(mainCmd);
            if (host.instanceSkinned && host.skinned)
            {
                CullViewDesc   skinViews[kMaxStaticViews];
                const uint32_t skinViewCount = host.drawList->exportGpuViews(skinViews, kMaxStaticViews);
                host.skinned->dispatchCull(mainCmd, skinViews, skinViewCount);
            }
        }

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
            uint32_t  viewIndex    = 0;
            uint32_t  skinView     = 0;
            uint32_t  casterBegin  = 0;
            uint32_t  groundEnd    = 0;
            uint32_t  casterEnd    = 0;
            uint32_t  foliage      = IShadowCasterSource::kNoRetainedFoliage;
            bool      recordStatic = false;
            bool      recordSkin   = false;
            bool      drawTerrain  = false;
        };

        struct ShadowJob
        {
            bool                       kicked        = false;
            ID3D12GraphicsCommandList* list          = nullptr;
            ID3D12CommandAllocator*    alloc         = nullptr;
            Renderer*                  renderer      = nullptr;
            ShadowSystem*              shadows       = nullptr;
            DrawList*                  drawList      = nullptr;
            SkinnedInstanceBuffer*     skinned       = nullptr;
            const SkinnedMeshPipeline* skinnedShadow = nullptr;
            ShadowWorkerDraws          worker{};
            bool                       skinUploadOk   = false;
            uint32_t                   cascadeBegin   = 0;
            uint32_t                   cascadeEnd     = 0;
            uint32_t                   skinDepthCount = 0;
            CullViewDesc               skinDepth[kMaxStaticViews]{};
            std::vector<ShadowCaster>  casters;
            std::vector<CascadeRecord> cascades;
        };

        // Join is before endFrame, so main refills this only after the job returns.
        ShadowJob                  g_job;
        uint32_t                   g_kickedFrame = 0;
        std::vector<ShadowCaster>* g_capture     = nullptr;

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

            // This job is the only Reset of the shadow allocator.
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
                if (rec.drawTerrain && job->worker.ground)
                    job->worker.ground(job->worker.user, cmd, rec.frustum);
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
                if (rec.foliage != IShadowCasterSource::kNoRetainedFoliage && job->worker.foliage)
                    job->worker.foliage(job->worker.user, cmd, rec.viewProj, rec.foliage);
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

        bool depthSetsReady(const ShadowFrame& host)
        {
            if (host.drawList && !host.drawList->depthResourceSetReady())
                return false;
            if (host.casters && !host.casters->workerReady())
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

        void dispatchCameraSide(const ShadowFrame& host, ID3D12GraphicsCommandList* mainCmd, uint32_t cascadeBegin, uint32_t cascadeEnd, bool skinUploadOk)
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
            CullViewDesc   exported[kMaxStaticViews];
            const uint32_t count = host.drawList->exportGpuViews(exported, kMaxStaticViews);
            CullViewDesc   mainViews[kMaxStaticViews];
            uint32_t       mainCount = 0;
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

        void fillSkinDepth(const ShadowFrame& host, uint32_t cascadeBegin, uint32_t cascadeEnd)
        {
            g_job.skinDepthCount = 0;
            if (!host.drawList || !host.instanceSkinned || !host.skinned)
                return;
            CullViewDesc   exported[kMaxStaticViews];
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

    } // namespace

    bool capturingShadowCasters()
    {
        return g_capture != nullptr;
    }

    void pushShadowCaster(const Mesh* mesh, const Matrix4f& world, uint64_t bones)
    {
        if (!g_capture || !mesh)
            return;
        ShadowCaster caster;
        caster.mesh  = mesh;
        caster.world = world;
        caster.bones = bones;
        g_capture->push_back(caster);
    }

    ShadowFeature::ShadowFeature() :
        m_cull(*this, EntryKind::Cull),
        m_directional(*this, EntryKind::Directional),
        m_local(*this, EntryKind::Local)
    {
    }

    void ShadowFeature::setFrame(const ShadowFrame& frame)
    {
        m_frame        = frame;
        m_viewIndex    = 1;
        m_culled       = false;
        m_workerKicked = false;
    }

    const char* ShadowFeature::Entry::name() const
    {
        switch (m_kind)
        {
        case EntryKind::Cull:
            return "FrameCull";
        case EntryKind::Local:
            return "LocalShadows";
        default:
            return "Shadows";
        }
    }

    PassId ShadowFeature::Entry::pass() const
    {
        switch (m_kind)
        {
        case EntryKind::Cull:
            return PassId::FrameCull;
        case EntryKind::Local:
            return PassId::LocalShadows;
        default:
            return PassId::Shadows;
        }
    }

    void ShadowFeature::Entry::addPasses(FeatureBuilder& b, const FrameView&)
    {
        ShadowFeature& owner = m_owner;
        if (m_kind == EntryKind::Cull)
        {
            addFrameCullPass(b, [&owner](Rg::PassContext& c, const Rg::NoData&) { owner.recordFrameCull(c.cmd()); });
            return;
        }
        if (m_kind == EntryKind::Directional)
        {
            addShadowPass(b, [&owner](Rg::PassContext& c, const ShadowTargets&) { owner.recordDirectional(c.cmd()); });
            return;
        }
        const LocalShadowSystem* local = owner.m_frame.localShadows;
        // No pass on a frame without faces or without the array.
        if (!local || local->faceCountThisFrame() <= 0 || !local->isValid())
            return;
        addLocalShadowPass(b, [&owner](Rg::PassContext& c, const ShadowTargets&) { owner.recordLocal(c.cmd()); });
    }

    // True when the caller must not also record on the main list.
    bool ShadowFeature::kickWorker(ID3D12GraphicsCommandList* mainCmd)
    {
        const ShadowFrame&   host    = m_frame;
        IShadowCasterSource* casters = host.casters;
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
        if (casters)
            casters->beginWorkerCapture();

        g_job.casters.clear();
        g_job.cascades.clear();
        g_job.list          = list;
        g_job.alloc         = alloc;
        g_job.renderer      = host.renderer;
        g_job.shadows       = host.shadows;
        g_job.drawList      = host.drawList;
        g_job.skinned       = host.skinned;
        g_job.worker        = casters ? casters->workerDraws() : ShadowWorkerDraws{};
        g_job.skinnedShadow = host.skinnedShadow;
        g_job.skinUploadOk  = skinUploadOk;
        g_job.cascadeBegin  = m_viewIndex;
        g_capture           = &g_job.casters;

        const int cascadeCount = host.shadows->cascadeCount();
        for (int i = 0; i < cascadeCount; ++i)
        {
            const Matrix4f& viewProj = host.shadows->cascade(i).viewProj;
            const Frustum3f frustum(viewProj);
            CascadeRecord   rec;
            rec.viewProj    = viewProj;
            rec.frustum     = frustum;
            rec.drawTerrain = casters && casters->groundOnWorker();
            rec.casterBegin = static_cast<uint32_t>(g_job.casters.size());
            if (!rec.drawTerrain && casters)
                casters->captureGround(frustum);
            rec.groundEnd    = static_cast<uint32_t>(g_job.casters.size());
            rec.recordStatic = host.instanceStatic && host.drawList && m_viewIndex < kMaxStaticViews;
            rec.viewIndex    = m_viewIndex;
            rec.skinView     = m_viewIndex;
            rec.recordSkin   = host.instanceSkinned && host.skinned != nullptr;
            if (rec.recordStatic)
            {
                if (!host.drawList->gpuCullActive())
                {
                    float lightVp[16];
                    viewProj.store(lightVp);
                    host.drawList->buildView(frustum, lightVp, lightVp, RenderPassKind::Depth, ViewId{ m_viewIndex });
                }
                ++m_viewIndex;
            }
            if (casters)
            {
                ShadowCasterView view;
                view.viewProj  = &viewProj;
                view.frustum   = &frustum;
                view.instanced = rec.recordStatic;
                casters->captureCasters(view);
            }
            rec.casterEnd = static_cast<uint32_t>(g_job.casters.size());
            rec.foliage   = casters ? casters->retainFoliage(frustum) : IShadowCasterSource::kNoRetainedFoliage;
            g_job.cascades.push_back(rec);
        }
        g_capture        = nullptr;
        g_job.cascadeEnd = m_viewIndex;
        if (host.drawList && g_job.cascadeEnd > g_job.cascadeBegin)
            host.drawList->armResourceSet(g_job.cascadeBegin, g_job.cascadeEnd, CullResourceSet::Depth);
        fillSkinDepth(host, g_job.cascadeBegin, g_job.cascadeEnd);

        g_job.kicked  = true;
        g_kickedFrame = frame;
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

    // One cascade or one local-light face, recorded now on `cmd`.
    void ShadowFeature::recordView(ID3D12GraphicsCommandList* cmd, const Matrix4f& lightViewProj, const Frustum3f& frustum, const Entity* light, bool beginFoliage)
    {
        if (!cmd)
            return;
        const ShadowFrame&   host    = m_frame;
        IShadowCasterSource* casters = host.casters;

        if (casters)
            casters->drawGround(cmd, frustum);

        const bool     recordThisView = host.instanceStatic && host.drawList && m_viewIndex < kMaxStaticViews;
        const uint32_t skinView       = m_viewIndex;
        if (recordThisView)
        {
            float lightVp[16];
            lightViewProj.store(lightVp);
            const ViewId view{ m_viewIndex };
            if (!host.drawList->gpuCullActive())
                host.drawList->buildView(frustum, lightVp, lightVp, RenderPassKind::Depth, view);
            host.drawList->recordDepth(cmd, view, lightVp);
            ++m_viewIndex;
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

        if (casters)
        {
            ShadowCasterView view;
            view.viewProj     = &lightViewProj;
            view.frustum      = &frustum;
            view.instanced    = recordThisView;
            view.beginFoliage = beginFoliage;
            view.light        = light;
            casters->drawCasters(cmd, view);
        }
    }

    void ShadowFeature::recordCascades(ID3D12GraphicsCommandList* cmd)
    {
        ShadowSystem* shadows = m_frame.shadows;
        if (!shadows || !shadows->isValid() || !shadows->enabled())
            return;
        shadows->beginCapture(cmd);
        for (int i = 0; i < shadows->cascadeCount(); ++i)
        {
            shadows->beginCascade(cmd, i);
            const Frustum3f casterFrustum(shadows->cascade(i).viewProj);
            recordView(cmd, shadows->cascade(i).viewProj, casterFrustum, nullptr, true);
        }
        shadows->endCapture(cmd);
    }

    void ShadowFeature::recordFrameCull(ID3D12GraphicsCommandList* mainCmd)
    {
        if (m_culled)
            return;
        m_culled                = true;
        const ShadowFrame& host = m_frame;
        if (host.casters)
            host.casters->prepareDirectional();
        if (!host.renderer || !mainCmd)
            return;
        // The worker records the cascades on the shadow list, which runs before the main list.
        m_workerKicked = kickWorker(mainCmd);
        if (!m_workerKicked)
            dispatchFrameCull(host, mainCmd);
    }

    void ShadowFeature::recordDirectional(ID3D12GraphicsCommandList* mainCmd)
    {
        recordFrameCull(mainCmd);
        if (!m_frame.renderer || !mainCmd || m_workerKicked)
            return;
        recordCascades(mainCmd);
    }

    void ShadowFeature::recordLocal(ID3D12GraphicsCommandList* cmd)
    {
        LocalShadowSystem* local = m_frame.localShadows;
        if (!cmd || !local || local->faceCountThisFrame() <= 0)
            return;
        local->beginCapture(cmd);
        for (int f = 0; f < local->faceCountThisFrame(); ++f)
        {
            local->beginFace(cmd, f);
            const LocalShadowFaceDraw& face = local->face(f);
            recordView(cmd, face.viewProj, face.frustum, &face.light, face.firstFace);
        }
        local->endCapture(cmd);
    }

    void registerShadowFeature(RenderFeatureRegistry& registry, ShadowFeature& feature)
    {
        registry.add(&feature.cull());
        registry.add(&feature.directional());
        registry.add(&feature.local());
    }

} // namespace Dark
