#include "Render/RgExecutor.h"

#include "Core/Log.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"

namespace Dark
{

    void RgExecutor::beginFrame(Renderer& renderer, const Rg::RenderGraph& graph)
    {
        m_renderer = &renderer;
        m_graph    = &graph;
        m_ok       = true;
        resetSplits();
    }

    ID3D12GraphicsCommandList* RgExecutor::commandList(Rg::Queue queue) const
    {
        if (!m_renderer || queue != Rg::Queue::Graphics)
            return nullptr;
        return m_renderer->commandList();
    }

    void RgExecutor::fail(uint32_t what, const char* message)
    {
        m_ok = false;
        if (m_logged & (1u << what))
            return;
        m_logged |= 1u << what;
        DE_LOG_ERROR(LogCategory::Render, "RgExecutor: {} (logs once)", message);
    }

    ID3D12Resource* RgExecutor::resolve(const void* context, uint16_t resource)
    {
        return static_cast<const Rg::RenderGraph*>(context)->nativeResource(resource);
    }

    void RgExecutor::recordBarriers(const Rg::BarrierBatch& batch, const Rg::BarrierRec* records)
    {
        ID3D12GraphicsCommandList* cmd = commandList(batch.queue);
        if (!cmd || !m_graph)
        {
            fail(0, "a barrier batch without the graphics list (async compute is Phase 10)");
            return;
        }
        m_emitter.record(cmd, records, batch.count, &RgExecutor::resolve, m_graph, m_graph->options().validate);
    }

    void RgExecutor::passBegin(const Rg::PassContext& ctx)
    {
        if (ctx.queue() != Rg::Queue::Graphics)
            fail(1, "a pass is on the async compute queue, which does not exist until Phase 10");
        if (ctx.scopeName())
            profileGpuBegin(ctx.cmd(), ctx.scopeName(), ctx.scopeColor());
    }

    void RgExecutor::passEnd(const Rg::PassContext& ctx)
    {
        if (ctx.scopeName())
            profileGpuEnd(ctx.cmd());
    }

    bool RgExecutor::submitList(Rg::Queue queue)
    {
        (void)queue;
        fail(2, "a cross-queue split needs the compute queue and its fence (Phase 10); Renderer::endFrame submits the main list");
        return false;
    }

    void RgExecutor::queueSignal(Rg::Queue queue, uint32_t value)
    {
        (void)queue;
        (void)value;
        fail(3, "cross-queue signal without a compute queue (Phase 10)");
    }

    void RgExecutor::queueWait(Rg::Queue waitQueue, Rg::Queue signalQueue, uint32_t value)
    {
        (void)waitQueue;
        (void)signalQueue;
        (void)value;
        fail(4, "cross-queue wait without a compute queue (Phase 10)");
    }

} // namespace Dark
