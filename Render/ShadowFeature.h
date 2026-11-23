#pragma once

#include "Math/Matrix4f.h"
#include "Render/RenderFeature.h"
#include "Render/ShadowCasterSource.h"

#include <cstdint>

struct ID3D12GraphicsCommandList;

namespace Dark
{

    class DrawList;
    class IShadowCasterSource;
    class JobSystem;
    class LocalShadowSystem;
    class Mesh;
    class Renderer;
    class ShadowSystem;
    class SkinnedInstanceBuffer;
    class SkinnedMeshPipeline;

    // Non-owning. The host fills one each frame before the registry declares the features.
    struct ShadowFrame
    {
        Renderer*                  renderer        = nullptr;
        JobSystem*                 jobs            = nullptr;
        ShadowSystem*              shadows         = nullptr;
        LocalShadowSystem*         localShadows    = nullptr;
        DrawList*                  drawList        = nullptr;
        SkinnedInstanceBuffer*     skinned         = nullptr;
        const SkinnedMeshPipeline* skinnedShadow   = nullptr;
        bool                       instanceStatic  = false;
        bool                       instanceSkinned = false;
        IShadowCasterSource*       casters         = nullptr;
    };

    // True while main is filling the caster array for the shadow worker.
    // Primitive callbacks append mesh and world instead of drawing.
    bool capturingShadowCasters();
    void pushShadowCaster(const Mesh* mesh, const Math::Matrix4f& world, uint64_t bones = 0);

    // The FrameCull, Shadows and LocalShadows entries. FrameCull (before GBuffer) records the frame's
    // GPU cull and the cascades that run ahead of the main list: it kicks the shadow worker, or records
    // them on the separate shadow list. Otherwise Shadows (after GBuffer) records them on the main
    // list. LocalShadows draws the selected faces on the main list. The shadow entries share the
    // frame's depth view index, which starts at 1 (view 0 is the camera).
    class ShadowFeature
    {
    public:
        ShadowFeature();
        ShadowFeature(const ShadowFeature&)            = delete;
        ShadowFeature& operator=(const ShadowFeature&) = delete;

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
        void recordCascades(ID3D12GraphicsCommandList* cmd, bool profileFoliageCull);
        void recordView(ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& lightViewProj, const Frustum3f& frustum, const Entity* light, bool beginFoliage, bool profileFoliageCull);

        ShadowFrame m_frame;
        uint8_t     m_viewIndex = 1;
        // Set by recordFrameCull: it ran; the worker or the shadow list has the cascades.
        bool  m_culled           = false;
        bool  m_cascadesRecorded = false;
        Entry m_cull;
        Entry m_directional;
        Entry m_local;
    };

    // Adds the three entries. The host owns the feature and registers it before SceneRenderer::init.
    void registerShadowFeature(RenderFeatureRegistry& registry, ShadowFeature& feature);

} // namespace Dark
