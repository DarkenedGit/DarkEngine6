#pragma once

#include "ECS/Entity.h"
#include "Math/Matrix4f.h"
#include "Render/Frustum3f.h"

#include <cstdint>

struct ID3D12GraphicsCommandList;

namespace Dark
{

    // One cascade or one local-light face, as the caster source sees it.
    struct ShadowCasterView
    {
        const Math::Matrix4f* viewProj = nullptr;
        const Frustum3f*      frustum  = nullptr;
        // The instanced static list drew this view, so the casters it accepted are skipped.
        bool instanced = false;
        // Foliage culls on the first face of a light (and on every cascade), then redraws that list.
        bool beginFoliage       = false;
        bool profileFoliageCull = true;
        // A local face's light; a point light bounds its foliage cull by its range. Null for cascades.
        const Entity* light = nullptr;
    };

    // Worker-thread draws of a kicked shadow job. `user` must stay valid until the frame's render
    // jobs are joined: it is not the per-frame source.
    struct ShadowWorkerDraws
    {
        // The ground, when groundOnWorker() said it draws on the worker.
        void (*ground)(void* user, ID3D12GraphicsCommandList* cmd, const Frustum3f& frustum) = nullptr;
        // The foliage list retainFoliage() returned for the cascade.
        void (*foliage)(void* user, ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& viewProj, uint32_t retained) = nullptr;
        void* user                                                                                                     = nullptr;
    };

    // The domain casters of the shadow passes (models, primitives, terrain, foliage). ShadowFeature
    // records the passes and the instanced lists; the source adds what Render cannot see. Main thread
    // only, except the ShadowWorkerDraws it hands out.
    class IShadowCasterSource
    {
    public:
        static constexpr uint32_t kNoRetainedFoliage = ~0u;

        virtual ~IShadowCasterSource() = default;

        // The FrameCull pass, before the G-buffer and any cascade: create the GPU resources of this
        // frame's casters.
        virtual void prepareDirectional() = 0;

        // Immediate recording on `cmd` (the main list, the shadow list or a local face). drawGround
        // runs before the instanced lists, drawCasters after them.
        virtual void drawGround(ID3D12GraphicsCommandList* cmd, const Frustum3f& frustum)      = 0;
        virtual void drawCasters(ID3D12GraphicsCommandList* cmd, const ShadowCasterView& view) = 0;

        // Worker path, on main while the cascades are captured. Casters go through pushShadowCaster.
        // False while a depth resource set the worker needs does not exist yet.
        virtual bool workerReady() const = 0;
        // Before the first cascade is captured.
        virtual void beginWorkerCapture() = 0;
        // True when the ground draws on the worker (ShadowWorkerDraws::ground) instead of as casters.
        virtual bool groundOnWorker() const                       = 0;
        virtual void captureGround(const Frustum3f& frustum)      = 0;
        virtual void captureCasters(const ShadowCasterView& view) = 0;
        // Culls and keeps this cascade's foliage for the worker; kNoRetainedFoliage when there is none.
        virtual uint32_t retainFoliage(const Frustum3f& frustum) = 0;
        // Once per kicked job, before its cascades are captured.
        virtual ShadowWorkerDraws workerDraws() = 0;
    };

} // namespace Dark
