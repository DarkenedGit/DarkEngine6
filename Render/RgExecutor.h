#pragma once

#include "Render/RenderGraph.h"

#include <cstdint>

namespace Dark
{

    class Renderer;

    // The D3D12 frame-graph executor (render-graph plan Phase 4): runs the compiled passes on the
    // renderer's main command list, which Renderer::endFrame still closes and submits (after the
    // shadow worker's list). It follows SplitExecutor's rule of one list per cross-queue segment, but
    // until the compute queue lands (Phase 10) there is only the graphics queue: a split, a signal
    // or a wait is an error, as is a barrier record. Every Phase 4 pass is opaque (OwnBarriers), so
    // the graph plans none; the enhanced barrier emitter is Phase 6. A pass with a GPU scope gets a
    // profiler scope around its execute.
    class RgExecutor final : public Rg::SplitExecutor
    {
    public:
        void                       beginFrame(Renderer& renderer);
        ID3D12GraphicsCommandList* commandList(Rg::Queue queue) const override;
        // False when this frame asked for something the Phase 4 executor cannot do (logged once each).
        bool ok() const
        {
            return m_ok;
        }

    protected:
        void recordBarriers(const Rg::BarrierBatch& batch, const Rg::BarrierRec* records) override;
        void passBegin(const Rg::PassContext& ctx) override;
        void passEnd(const Rg::PassContext& ctx) override;
        bool submitList(Rg::Queue queue) override;
        void queueSignal(Rg::Queue queue, uint32_t value) override;
        void queueWait(Rg::Queue waitQueue, Rg::Queue signalQueue, uint32_t value) override;

    private:
        void fail(uint32_t what, const char* message);

        Renderer* m_renderer = nullptr;
        bool      m_ok       = true;
        uint32_t  m_logged   = 0; // one bit per failure kind
    };

} // namespace Dark
