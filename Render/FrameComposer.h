#pragma once

#include "Render/FrameFeatures.h"
#include "Render/RenderGraph.h"
#include "Render/RgExecutor.h"

#include <cstdint>
#include <utility>

struct ID3D12Resource;

namespace Dark
{

    class Renderer;

    // Drives a 3D host's frame through the render graph (render-graph plan Phase 4). Per frame:
    // begin() imports the back buffer, depth, the scene targets and the host's shadow maps; the host
    // set()s a callback per PassId it runs this frame (each calls the existing recording code with
    // ctx.cmd()); execute() adds them in feature-list order as opaque passes, compiles, checks the
    // order against the list and runs them on the main list. Async compute stays off (Phase 10).
    class FrameComposer
    {
    public:
        void begin(Renderer& renderer, const FeaturePass* list, uint32_t count, ID3D12Resource* shadowMap, ID3D12Resource* localShadowArray);

        template <class Fn> bool set(PassId id, Fn&& fn)
        {
            return m_features.set(id, std::forward<Fn>(fn));
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
        void importResource(RenderResource resource, ID3D12Resource* native);

        Rg::RenderGraph m_graph;
        FeatureFrame    m_features;
        RgExecutor      m_executor;
        Renderer*       m_renderer = nullptr;
    };

} // namespace Dark
