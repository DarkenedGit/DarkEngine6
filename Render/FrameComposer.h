#pragma once

#include "Render/FrameFeatures.h"
#include "Render/FrameSetup.h"
#include "Render/FrameView.h"
#include "Render/RenderGraph.h"
#include "Render/RgExecutor.h"
#include "Render/ShadowInputs.h"
#include "Render/TemporalHistory.h"

#include <cstdint>
#include <utility>

struct ID3D12Resource;

namespace Dark
{

    class Renderer;

    // Drives a 3D host's frame through the render graph (render-graph plan Phase 4). Per frame:
    // begin() imports the back buffer, depth, the scene targets and the host's shadow maps; the host
    // set()s or declare()s each PassId it runs this frame; execute() adds them in feature-list order,
    // compiles, checks the order against the list and runs them on the main list. The directional
    // shadow map's import ends the frame in its frame-start state (Render/ShadowInputs.h). Async
    // compute stays off (Phase 10).
    class FrameComposer
    {
    public:
        void begin(Renderer& renderer, const FrameView& view, const FeaturePass* list, uint32_t count, const FrameShadows& shadows);
        ~FrameComposer();

        // A graph history (render-graph plan Phase 5) for one of the *History resources: the graph
        // decides validity and ping-pong, the renderer's target registry holds both slots. Call
        // after begin(), before execute(). Invalid when the history cannot exist this frame.
        Rg::HistoryTargets history(RenderResource resource, const char* name, const Rg::TargetDesc& desc);
        // Camera cut / scene change: history() requests after this see no valid prev.
        void invalidateHistory()
        {
            m_graph.invalidateHistory();
        }
        // A pass skipped its declared history write at execute time.
        void markHistoryUnwritten(const char* name)
        {
            m_graph.markHistoryUnwritten(name);
        }
        // This frame's view: the host's, with the setup hooks' disabled passes added
        // (resolveSetup(), or begin() if that was not called).
        const FrameView& view() const
        {
            return m_view;
        }
        // Resolve the setup hooks before the host's shadow update, worker/cull views and lighting
        // constants, so those honour this frame's disabled bits. begin() then keeps this result and
        // does not run the hooks again. Call once per frame, with the same view begin() will get.
        const FrameSetup& resolveSetup(const FrameView& view);
        // The game's frame setup hooks (Render/FrameSetup.h).
        FrameSetupHooks& setupHooks()
        {
            return m_setupHooks;
        }

        template <class Fn> bool set(PassId id, Fn&& fn)
        {
            return m_features.set(id, std::forward<Fn>(fn));
        }
        // A converted entry (Phase 6 part 2): `fn(FeatureBuilder&)` adds the entry's passes with
        // declared uses when execute() builds the frame (FeatureFrame::declare).
        template <class Fn> bool declare(PassId id, Fn&& fn)
        {
            return m_features.declare(id, std::forward<Fn>(fn));
        }
        Renderer* renderer() const
        {
            return m_renderer;
        }

        // Records the frame. False when the graph did not compile (nothing recorded: the host's ImGui
        // and endFrame still run on the cleared back buffer) or the executor failed.
        bool execute(const char* host);

        const Rg::RenderGraph& graph() const
        {
            return m_graph;
        }
        const FeatureFrame& features() const
        {
            return m_features;
        }

    private:
        void importResource(RenderResource resource, ID3D12Resource* native, const Rg::TextureViews& views = {}, Rg::Layout finalLayout = Rg::Layout::Undefined);

        Rg::RenderGraph m_graph;
        FeatureFrame    m_features;
        RgExecutor      m_executor;
        Renderer*            m_renderer = nullptr;
        FrameView            m_view;
        bool                 m_holdNext = false; // the frame after a held one also starts without history
        Rg::HistoryTargetSet m_histories;
        FrameSetupHooks      m_setupHooks;
        bool                 m_setupResolved = false; // resolveSetup() ran; begin() consumes it
    };

} // namespace Dark
