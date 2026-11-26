#pragma once

#include "Math/Matrix4f.h"
#include "Render/LocalShadowSystem.h"
#include "Render/RenderFeature.h"
#include "Render/ShadowCasterSource.h"
#include "Render/FrameSetup.h"
#include "Render/ShadowSystem.h"

#include <cstdint>

struct ID3D12GraphicsCommandList;

namespace Dark
{

    class Camera3D;
    class DrawList;
    class IShadowCasterSource;
    class JobSystem;
    class Mesh;
    class Renderer;
    class SceneRenderer;
    class SkinnedInstanceBuffer;
    class SkinnedMeshPipeline;
    class SkinningUploadRing;
    class World;

    // Non-owning. The host fills one each frame before the registry declares the features. The
    // shadow maps and the skinned depth pipeline are not part of it: they are bound once
    // (bindShadowMaps).
    struct ShadowFrame
    {
        Renderer*              renderer        = nullptr;
        JobSystem*             jobs            = nullptr;
        DrawList*              drawList        = nullptr;
        SkinnedInstanceBuffer* skinned         = nullptr;
        bool                   instanceStatic  = false;
        bool                   instanceSkinned = false;
        IShadowCasterSource*   casters         = nullptr;
    };

    // True while main is filling the caster array for the shadow worker.
    // Primitive callbacks append mesh and world instead of drawing.
    bool capturingShadowCasters();
    void pushShadowCaster(const Mesh* mesh, const Math::Matrix4f& world, uint64_t bones = 0);

    // The FrameCull, Shadows and LocalShadows entries. FrameCull (before GBuffer) kicks the shadow
    // worker, which records the cascades on the shadow list, or records the frame's GPU cull on the
    // main list; then Shadows (after GBuffer) records the cascades there. LocalShadows draws the
    // selected faces on the main list. The shadow entries share the frame's depth view index, which
    // starts at 1 (view 0 is the camera).
    class ShadowFeature
    {
    public:
        ShadowFeature();
        ShadowFeature(const ShadowFeature&)            = delete;
        ShadowFeature& operator=(const ShadowFeature&) = delete;

        // The maps and the skinned depth pipeline the entries record into, and the skinning ring the
        // host's skinned casters upload through. SceneRenderer owns them;
        // registerShadowFeature(SceneRenderer&, ShadowFeature&) binds its own before init. Null
        // (unbound): no cascades, no faces, no skinned worker casters.
        void bindShadowMaps(ShadowSystem* shadows, LocalShadowSystem* localShadows, const SkinnedMeshPipeline* skinnedShadow, SkinningUploadRing* skinRing);

        // Per frame, before setFrame, in this order of the host's frame: the local faces this frame
        // draws, then the cascades. No-ops while unbound. `setup` is the resolved frame setup
        // (FrameComposer::resolveSetup): a disabled DirectionalShadows or LocalShadows bit turns that
        // system off (strength 0 / no faces) so the depth views, worker and lighting constants agree.
        void updateLocal(World& world, const Camera3D& camera, const Frustum3f& cameraFrustum, uint32_t frameIndex, bool select, const FrameSetup& setup = {});
        void updateDirectional(const Camera3D& camera, const Math::Vector3f& lightDirToward, const Math::AABox3f& sceneBounds, float sunElevation, float cloudCoverage, uint32_t frameIndex,
                               const FrameSetup& setup = {});

        // The depth views of the frame's GPU cull, in the order the entries record them: each cascade
        // (when the map exists and shadows are on), then each local face. `add(viewProj, frustum)`.
        template <class AddView> void forEachDepthView(AddView&& add) const
        {
            if (m_shadows && m_shadows->isValid() && m_shadows->enabled())
            {
                for (int i = 0; i < m_shadows->cascadeCount(); ++i)
                    add(m_shadows->cascade(i).viewProj, Frustum3f(m_shadows->cascade(i).viewProj));
            }
            if (m_localShadows)
            {
                for (int f = 0; f < m_localShadows->faceCountThisFrame(); ++f)
                {
                    const LocalShadowFaceDraw& face = m_localShadows->face(f);
                    add(face.viewProj, face.frustum);
                }
            }
        }

        // Once, after SceneRenderer::init: hands the maps' SRVs to `renderer` and the cascade depth
        // pipeline to the local faces.
        void publishShadowMaps(Renderer& renderer);
        // The debug view of both maps.
        void setDebugEnabled(bool enabled);

        // What the readers sample (lighting constants, sky, water, terrain, forward draws) and the
        // caster depth draws use. Bound (see bindShadowMaps) before any call.
        const ShadowSystem& shadowMaps() const
        {
            return *m_shadows;
        }
        const LocalShadowSystem& localShadowMaps() const
        {
            return *m_localShadows;
        }
        const SkinnedMeshPipeline* skinnedShadowPipeline() const
        {
            return m_skinnedShadow;
        }
        SkinningUploadRing* skinRing() const
        {
            return m_skinRing;
        }

        void               setFrame(const ShadowFrame& frame);
        const ShadowFrame& frame() const
        {
            return m_frame;
        }
        uint8_t viewIndex() const
        {
            return m_viewIndex;
        }

        IRenderFeature& cull()
        {
            return m_cull;
        }
        IRenderFeature& directional()
        {
            return m_directional;
        }
        IRenderFeature& local()
        {
            return m_local;
        }

        // The pass bodies. No cascades when shadows are off this frame (the map keeps its last
        // contents) and no faces when none was selected. recordDirectional runs recordFrameCull first
        // when FrameCull did not run this frame.
        void recordFrameCull(ID3D12GraphicsCommandList* mainCmd);
        void recordDirectional(ID3D12GraphicsCommandList* mainCmd);
        void recordLocal(ID3D12GraphicsCommandList* cmd);

    private:
        enum class EntryKind : uint8_t
        {
            Cull,
            Directional,
            Local,
        };

        class Entry final : public IRenderFeature
        {
        public:
            Entry(ShadowFeature& owner, EntryKind kind) :
                m_owner(owner),
                m_kind(kind)
            {
            }
            const char*  name() const override;
            FeatureStage stage() const override
            {
                return m_kind == EntryKind::Cull ? FeatureStage::PreFrame : FeatureStage::Shadows;
            }
            PassId pass() const override;
            bool   create(const FeatureCreateInfo&) override
            {
                return true;
            }
            void destroy() override {}
            void addPasses(FeatureBuilder& b, const FrameView& view) override;

        private:
            ShadowFeature& m_owner;
            EntryKind      m_kind = EntryKind::Directional;
        };

        bool kickWorker(ID3D12GraphicsCommandList* mainCmd);
        void recordCascades(ID3D12GraphicsCommandList* cmd);
        void recordView(ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& lightViewProj, const Frustum3f& frustum, const Entity* light, bool beginFoliage);

        ShadowSystem*              m_shadows       = nullptr;
        LocalShadowSystem*         m_localShadows  = nullptr;
        const SkinnedMeshPipeline* m_skinnedShadow = nullptr;
        SkinningUploadRing*        m_skinRing      = nullptr;
        ShadowFrame                m_frame;
        uint8_t                    m_viewIndex = 1;
        // Set by recordFrameCull: it ran; the worker has the cascades.
        bool  m_culled       = false;
        bool  m_workerKicked = false;
        Entry m_cull;
        Entry m_directional;
        Entry m_local;
    };

    // Adds the three entries (no maps bound). Hosts use registerShadowFeature(SceneRenderer&,
    // ShadowFeature&) (SceneRenderer.h), which also binds the scene's maps.
    void registerShadowFeature(RenderFeatureRegistry& registry, ShadowFeature& feature);

} // namespace Dark
