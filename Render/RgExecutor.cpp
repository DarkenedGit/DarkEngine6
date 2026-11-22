#include "Render/RgExecutor.h"

#include "Core/Log.h"
#include "Render/DescriptorRingHeap.h"
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
        // Opaque passes may leave any heap bound; a converted pass binds nothing itself.
        if (!ctx.ownBarriers() && ctx.cmd())
        {
            if (DescriptorRingHeap* ring = m_renderer ? m_renderer->descriptorRing() : nullptr)
                ring->bind(ctx.cmd());
            else
                fail(5, "a converted pass without the descriptor ring");
        }
    }

    uint64_t RgExecutor::descriptorTable(const uint64_t* cpu, uint32_t count)
    {
        DescriptorRingHeap* ring = m_renderer ? m_renderer->descriptorRing() : nullptr;
        if (!ring || !cpu || count == 0 || count > 32)
            return 0;
        D3D12_CPU_DESCRIPTOR_HANDLE handles[32];
        for (uint32_t i = 0; i < count; ++i)
            handles[i].ptr = static_cast<SIZE_T>(cpu[i]);
        return ring->frameTable(handles, count).ptr;
    }

    void RgExecutor::passEnd(const Rg::PassContext& ctx)
    {
        if (ctx.scopeName())
            profileGpuEnd(ctx.cmd());
    }

    void RgExecutor::unbindTargets(Rg::Queue queue)
    {
        if (queue != Rg::Queue::Graphics)
            return;
        if (ID3D12GraphicsCommandList* cmd = commandList(queue))
            cmd->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
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
