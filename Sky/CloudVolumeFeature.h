#pragma once

#include "Math/Matrix4f.h"
#include "Render/RenderFeature.h"
#include "Sky/CloudVolumeGpuList.h"
#include "Sky/CloudVolumePipeline.h"

namespace Dark
{

    class Camera3D;
    class World;

    // What the cloud volumes draw with this frame. The world, camera and view-projection are read
    // when the pass executes (the GBuffer pass fills the view-projection); `frame` is captured when set.
    struct CloudFrameInputs
    {
        World*                world    = nullptr;
        const Camera3D*       camera   = nullptr;
        const Math::Matrix4f* viewProj = nullptr;
        CloudVolumeFrame      frame{};
    };

    // Ray-marched cloud volumes composited onto HDR after water (PassId::Clouds). The pass samples
    // depth instead of testing it, so only HDR is bound.
    class CloudVolumeFeature final : public IRenderFeature
    {
    public:
        const char* name() const override
        {
            return "Clouds";
        }
        FeatureStage stage() const override
        {
            return FeatureStage::Transparent;
        }
        PassId pass() const override
        {
            return PassId::Clouds;
        }

        bool create(const FeatureCreateInfo& info) override;
        void destroy() override;
        void addPasses(FeatureBuilder& b, const FrameView& view) override;

        // Any time before the Clouds pass executes. Without a world, camera or view-projection the
        // pass draws nothing.
        void setFrameInputs(const CloudFrameInputs& inputs)
        {
            m_inputs = inputs;
        }

        const CloudVolumePipeline& pipeline() const
        {
            return m_pipeline;
        }
        const CloudVolumeGpuList& gpuList() const
        {
            return m_gpuList;
        }

    private:
        void draw(Rg::PassContext& c, D3D12_GPU_DESCRIPTOR_HANDLE depthTable);

        Renderer*           m_renderer = nullptr;
        CloudVolumePipeline m_pipeline;
        CloudVolumeGpuList  m_gpuList;
        CloudFrameInputs    m_inputs;
    };

} // namespace Dark
