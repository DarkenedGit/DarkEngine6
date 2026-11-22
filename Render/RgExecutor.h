#pragma once

#include "Render/EnhancedBarrierEmitter.h"
#include "Render/RenderGraph.h"

#include <cstdint>

namespace Dark
{

    class Renderer;

    // The D3D12 frame-graph executor (render-graph plan Phases 4 and 6): runs the compiled passes on
    // the renderer's main command list, which Renderer::endFrame still closes and submits (after the
    // shadow worker's list). It follows SplitExecutor's rule of one list per cross-queue segment, but
    // until the compute queue lands (Phase 10) there is only the graphics queue: a split, a signal
    // or a wait is an error. Barrier batches go through the EnhancedBarrierEmitter, which takes the
    // before-state of tracked resources from the frame ResourceStateTracker (Phase 6). A pass with a
    // GPU scope gets a profiler scope around its execute. A converted pass (no OwnBarriers) starts
    // with the renderer's descriptor ring bound and gets its tables from it (PassContext::srvTable,
    // Phase 6 part 2); opaque passes keep binding their own heaps. unbindTargets clears the output
    // merger before a barrier batch that follows a pass which may have bound targets (SplitExecutor).
    class RgExecutor final : public Rg::SplitExecutor
    {
    public:
        void                       beginFrame(Renderer& renderer, const Rg::RenderGraph& graph);
        ID3D12GraphicsCommandList* commandList(Rg::Queue queue) const override;
        // False when this frame asked for something the Phase 4 executor cannot do (logged once each).
        bool ok() const
        {
            return m_ok;
        }
        // Barriers recorded and rg.validate layout mismatches, since the start (Phase 6 exit gate).
        uint32_t barriersEmitted() const
        {
            return m_emitter.emitted();
        }
        uint32_t validateMismatches() const
        {
            return m_emitter.mismatches();
        }

        uint64_t descriptorTable(const uint64_t* cpu, uint32_t count) override;

    protected:
        void recordBarriers(const Rg::BarrierBatch& batch, const Rg::BarrierRec* records) override;
        void passBegin(const Rg::PassContext& ctx) override;
        void passEnd(const Rg::PassContext& ctx) override;
        void unbindTargets(Rg::Queue queue) override;
        bool submitList(Rg::Queue queue) override;
        void queueSignal(Rg::Queue queue, uint32_t value) override;
        void queueWait(Rg::Queue waitQueue, Rg::Queue signalQueue, uint32_t value) override;

    private:
        void fail(uint32_t what, const char* message);

        static ID3D12Resource* resolve(const void* context, uint16_t resource);

        Renderer*              m_renderer = nullptr;
        const Rg::RenderGraph* m_graph    = nullptr;
        EnhancedBarrierEmitter m_emitter;
        bool      m_ok       = true;
        uint32_t  m_logged   = 0; // one bit per failure kind
    };

} // namespace Dark
