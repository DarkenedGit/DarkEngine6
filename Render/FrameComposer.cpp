#include "Render/FrameComposer.h"

#include "Render/EnhancedBarrierEmitter.h"
#include "Render/ResourceStateTracker.h"

#include "Render/GraphTargets.h"
#include "Render/Renderer.h"
#include "Render/RgViews.h"

#include <chrono>
#include <d3d12.h>

namespace Dark
{

    FrameComposer::~FrameComposer()
    {
        // The Renderer outlives the hosts' composers (Application owns it in the base class).
        if (m_renderer && m_renderer->targets())
            m_histories.clear(m_renderer->targets()->pool());
    }

    namespace
    {
        double setupClock()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    } // namespace

    const FrameSetup& FrameComposer::resolveSetup(const FrameView& view)
    {
        m_view           = view;
        m_view.setup     = m_setupHooks.resolve(view, setupClock());
        m_setupResolved  = true;
        return m_view.setup;
    }

    void FrameComposer::begin(Renderer& renderer, const FrameView& view, const FeaturePass* list, uint32_t count, const FrameShadows& shadows)
    {
        const FrameSetup setup = m_setupResolved ? m_view.setup : m_setupHooks.resolve(view, setupClock());
        m_setupResolved        = false;
        m_renderer             = &renderer;
        m_view                 = view;
        m_view.setup           = setup;
        Rg::CompileOptions options = m_graph.options();
        options.asyncCompute       = false; // one graphics queue until Phase 10
        m_graph.setOptions(options);
        // Transients are sized as the device places them (heap offsets and alignment).
        if (GraphTargets* targets = renderer.targets())
            m_graph.setTransientSizer(&TransientHeap::sizeTransient, &targets->transients());
        m_graph.beginFrame(renderer.width(), renderer.height());
        m_features.begin(list, count);
        // A held frame's image must not reach a history, and the frame after it reads none.
        if (view.holdTemporal || m_holdNext)
            m_graph.invalidateHistory();
        m_holdNext = view.holdTemporal;
        // Views for converted passes (PassContext::rtv/dsv/srvTable, Phase 6 part 2).
        importResource(RenderResource::Swap, renderer.backBufferResource(), textureViews({}, renderer.backBufferRtv()));
        importResource(RenderResource::Depth, renderer.depthResource(), textureViews(renderer.depthSrvCpu(), {}, renderer.depthDsv(), renderer.depthReadOnlyDsv()));
        // Without a map the handle stays invalid: its readers bind their fallback views. The sun map
        // ends the frame writable after its last reader (ShadowInputs.h).
        if (shadows.map)
            importResource(RenderResource::Shadow, shadows.map, textureViews(cpuHandle(shadows.mapSrv)), shadowFrameStartState().layout);
        if (shadows.localMap)
            importResource(RenderResource::LocalShadow, shadows.localMap, textureViews(cpuHandle(shadows.localMapSrv)));
        importResource(RenderResource::Albedo, renderer.albedoResource(), textureViews(renderer.albedoSrvCpu(), renderer.albedoRtv()));
        importResource(RenderResource::Attrib, renderer.attribResource(), textureViews(renderer.attribSrvCpu(), renderer.attribRtv()));
        importResource(RenderResource::Velocity, renderer.velocityResource(), textureViews(renderer.velocitySrvCpu(), renderer.velocityRtv()));
        importResource(RenderResource::Ao, renderer.aoResource(), textureViews(renderer.aoSrvCpu(), renderer.aoRtv()));
        if (renderer.surfaceTagResource())
            importResource(RenderResource::SurfaceTag, renderer.surfaceTagResource(), textureViews(renderer.surfaceTagSrvCpu(), renderer.surfaceTagRtv()));
        importResource(RenderResource::Hdr, renderer.hdr(), textureViews(renderer.hdrSrvCpu(), renderer.hdrRtv()));
        importResource(RenderResource::Post, renderer.postResource(), textureViews(renderer.postHdrSrvCpu(), renderer.postRtv()));
    }

    void FrameComposer::importResource(RenderResource resource, ID3D12Resource* native, const Rg::TextureViews& views, Rg::Layout finalLayout)
    {
        // The real resource and its real size and format; a missing target imports as an empty slot.
        Rg::TextureDesc desc;
        desc.sizeMode = Rg::SizeMode::Absolute;
        desc.width    = m_renderer->width();
        desc.height   = m_renderer->height();
        if (native)
        {
            const D3D12_RESOURCE_DESC rd = native->GetDesc();
            desc.width                   = static_cast<uint32_t>(rd.Width);
            desc.height                  = static_cast<uint32_t>(rd.Height);
            desc.format                  = static_cast<uint32_t>(rd.Format);
            desc.mips                    = static_cast<uint16_t>(rd.MipLevels);
            desc.arraySize               = static_cast<uint16_t>(rd.DepthOrArraySize);
        }
        // The layout the frame tracker holds now (Phase 6), so the plan's first barriers start from it.
        Rg::Layout initial = Rg::Layout::Undefined;
        if (const ResourceStateTracker* tracker = frameStateTracker())
            if (const Rg::UseState* now = tracker->current(native))
                initial = now->layout;
        m_features.importResource(m_graph, resource, native, desc, initial, views, finalLayout);
    }

    Rg::HistoryTargets FrameComposer::history(RenderResource resource, const char* name, const Rg::TargetDesc& desc)
    {
        GraphTargets* targets = m_renderer ? m_renderer->targets() : nullptr;
        if (!targets || !targets->valid())
            return {};
        Rg::HistoryTargets h = m_histories.request(m_graph, targets->pool(), name, desc);
        if (h.valid())
        {
            // Phase 6 part 2: the slots' allocations and views, so graph barriers reach them and
            // converted passes can bind them (a graph-invalid prev stays out of the graph).
            m_graph.setNative(h.pair.curr, targets->resource(h.curr));
            m_graph.setViews(h.pair.curr, textureViews(targets->srv(h.curr), targets->rtv(h.curr)));
            if (h.pair.prev.valid())
            {
                m_graph.setNative(h.pair.prev, targets->resource(h.prev));
                m_graph.setViews(h.pair.prev, textureViews(targets->srv(h.prev), targets->rtv(h.prev)));
            }
            m_features.setHistory(resource, h.pair);
        }
        return h;
    }

    bool FrameComposer::execute(const char* host)
    {
        if (!m_renderer)
            return false;
        // Histories not requested this frame, or re-described, have left the graph: free their memory
        // (retired behind this frame's fence).
        if (GraphTargets* targets = m_renderer->targets())
            m_histories.collect(m_graph, targets->pool());
        if (!m_features.compileFrame(m_graph, host))
            return false;
        // Transients need their placed resources and views before any pass records.
        if (GraphTargets* targets = m_renderer->targets(); targets && !targets->transients().realize(m_graph))
            return false;
        m_executor.beginFrame(*m_renderer, m_graph);
        const bool ran = m_graph.execute(&m_executor);
        // The executor clears the output merger before barrier batches; the code after the graph
        // (ImGui, HUD, movie) draws into the back buffer it expects bound.
        m_renderer->bindBackBuffer();
        if (!ran)
            return false;
        // No finish(): the main list is closed and submitted by Renderer::endFrame (after the shadow list).
        return m_executor.ok();
    }

} // namespace Dark
