#pragma once

#include "Render/RenderFeature.h"
#include "Sky/SkyPipeline.h"

#include <cstdint>

namespace Dark
{

    class Camera3D;
    class ShadowSystem;

    // What the sky draws with this frame. The camera, environment and shadows are read when the pass
    // executes (after the GBuffer pass jitters the camera); the scalars are captured when set.
    struct SkyFrameInputs
    {
        const Camera3D*         camera      = nullptr;
        const Sky::Environment* env         = nullptr;
        const ShadowSystem*     shadows     = nullptr;
        float                   exposure    = 1.0f; // tonemap owns exposure
        float                   waterLevel  = 0.0f;
        float                   fogScale    = 1.0f;
        float                   thermal     = 0.0f;
        float                   ultraviolet = 0.0f;
    };

    // The sky dome: one HDR draw with read-only depth after the lighting passes (PassId::Sky).
    class SkyFeature final : public IRenderFeature
    {
    public:
        const char* name() const override
        {
            return "Sky";
        }
        FeatureStage stage() const override
        {
            return FeatureStage::Sky;
        }
        PassId pass() const override
        {
            return PassId::Sky;
        }

        bool create(const FeatureCreateInfo& info) override;
        void destroy() override;
        void addPasses(FeatureBuilder& b, const FrameView& view) override;

        // Any time before the Sky pass executes. Without a camera or environment the pass draws nothing.
        void setFrameInputs(const SkyFrameInputs& inputs)
        {
            m_inputs = inputs;
        }

        const SkyPipeline& pipeline() const
        {
            return m_pipeline;
        }

    private:
        void draw(Rg::PassContext& c, D3D12_GPU_DESCRIPTOR_HANDLE shadowTable);

        Renderer*      m_renderer = nullptr;
        SkyPipeline    m_pipeline;
        SkyFrameInputs m_inputs;
    };

    // The Sky module's features, in the order the host runs them.
    void registerSkyFeatures(RenderFeatureRegistry& registry, SkyFeature& sky);

} // namespace Dark
