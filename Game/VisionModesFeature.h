#pragma once

#include "Game/VisionModes.h"
#include "Game/VisionModesPipeline.h"
#include "Math/Vector3f.h"
#include "Render/FrameSetup.h"
#include "Render/SceneRenderer.h"

namespace Dark
{

    class Camera3D;
    class Renderer;

    // Infrared and ultraviolet vision. A frame setup hook (FrameComposer::setupHooks) and a game-slot
    // feature (SceneRenderer::addGameFeature): ultraviolet is engine remaps only; heat writes
    // SurfaceTag and draws a full-screen pass that replaces (strength 1) or blends over HDR. Add it
    // to the game slot before features that draw over the scene, so they stay on top of heat.
    class VisionModesFeature final : public IFrameSetupHook, public IGameFeature
    {
    public:
        // This frame's modes and view. Set before FrameComposer::resolveSetup; `camera` is read when
        // the heat pass executes and must outlive execute(). Without a camera heat asks for nothing.
        struct Frame
        {
            VisionModes     modes;
            const Camera3D* camera = nullptr;
            Math::Vector3f  sunDir{ 0.0f, 1.0f, 0.0f };
        };

        VisionModesFeature();

        // False (logged as a warning with `tag`) when the pipeline cannot be created; heat then asks
        // for nothing and ultraviolet still works.
        bool create(Renderer& renderer, const char* tag);
        void release();
        bool isValid() const
        {
            return m_pipeline.isValid();
        }

        void setFrame(const Frame& frame)
        {
            m_frame = frame;
        }
        // What the last setupFrame asked for (Off until one runs).
        const VisionDecision& decision() const
        {
            return m_decision;
        }
        const VisionLut& heatLut() const
        {
            return m_heatLut;
        }

        const char* name() const override
        {
            return "VisionModes";
        }
        void setupFrame(const FrameView& view, FrameSetup& setup) override;
        void addPasses(FeatureBuilder& b, Renderer& renderer, const WorldDrawContext& draws) override;

    private:
        VisionModesPipeline m_pipeline;
        VisionLut           m_heatLut;
        Frame               m_frame;
        VisionDecision      m_decision;
        bool                m_loggedMissing = false;
    };

} // namespace Dark
