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

    // The Shadows and LocalShadows entries. Shadows records the frame's GPU cull and the cascades on
    // the shadow worker, the separate shadow list or the main list; LocalShadows draws the selected
    // faces. Both share the frame's depth view index, which starts at 1 (view 0 is the camera).
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

        IRenderFeature& directional()
        {
            return m_directional;
        }
        IRenderFeature& local()
        {
            return m_local;
        }

        // The pass bodies. Nothing when shadows are off this frame (the map keeps its last contents)
        // or when no face was selected.
        void recordDirectional(ID3D12GraphicsCommandList* mainCmd);
        void recordLocal(ID3D12GraphicsCommandList* cmd);

    private:
        class Entry final : public IRenderFeature
        {
        public:
            Entry(ShadowFeature& owner, bool local) :
                m_owner(owner),
                m_local(local)
            {
            }
            const char*  name() const override;
            FeatureStage stage() const override
            {
                return FeatureStage::Shadows;
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
            bool           m_local = false;
        };

        bool kickWorker(ID3D12GraphicsCommandList* mainCmd);
        void recordView(ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& lightViewProj, const Frustum3f& frustum, const Entity* light, bool beginFoliage, bool profileFoliageCull);

        ShadowFrame m_frame;
        uint8_t     m_viewIndex = 1;
        Entry       m_directional;
        Entry       m_local;
    };

    // Adds both entries. The host owns the feature and registers it before SceneRenderer::init.
    void registerShadowFeature(RenderFeatureRegistry& registry, ShadowFeature& feature);

} // namespace Dark
