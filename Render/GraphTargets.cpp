#include "Render/GraphTargets.h"

#include "Core/Log.h"

namespace Dark
{

    GraphTargets::GraphTargets() :
        m_backend(m_allocator),
        m_pool(m_backend)
    {
    }

    GraphTargets::~GraphTargets()
    {
        shutdown();
    }

    bool GraphTargets::init(ID3D12Device* device, uint32_t width, uint32_t height)
    {
        shutdown();
        if (!m_allocator.init(device))
            return false;
        // No targets yet: this only records the output size the registry resolves against.
        return m_pool.resizeAfterDrain(width, height);
    }

    void GraphTargets::shutdown()
    {
        m_pool.clear();
        m_backend.retireAfter(0);
        m_backend.collect(UINT64_MAX);
        m_allocator.shutdown();
    }

    bool GraphTargets::resizeAfterDrain(uint32_t width, uint32_t height)
    {
        const uint32_t oldWidth  = m_pool.outputWidth();
        const uint32_t oldHeight = m_pool.outputHeight();
        if (!m_pool.resizeAfterDrain(width, height))
        {
            DE_LOG_ERROR(LogCategory::Render, "GraphTargets: resize {}x{} -> {}x{} failed; kept the old targets", oldWidth, oldHeight, width, height);
            return false;
        }
        return true;
    }

    Rg::TargetId GraphTargets::registerTexture(const char* name, const Rg::TargetDesc& desc)
    {
        const Rg::TargetId id = m_pool.registerTexture(name, desc);
        if (!id.valid())
            DE_LOG_ERROR(LogCategory::Render, "GraphTargets: could not register {} (bad desc, name taken with another desc, or allocation failed)", name ? name : "<unnamed>");
        return id;
    }

    void GraphTargets::unregisterTexture(Rg::TargetId& id)
    {
        if (id.valid())
            m_pool.unregisterTexture(id);
        id = {};
    }

    ID3D12Resource* GraphTargets::resource(Rg::TargetId id) const
    {
        const Rg::TargetInfo info = m_pool.get(id);
        return info.storage ? info.storage->native : nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GraphTargets::srv(Rg::TargetId id) const
    {
        const Rg::TargetInfo info = m_pool.get(id);
        return { info.storage ? static_cast<SIZE_T>(info.storage->srv) : 0 };
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GraphTargets::rtv(Rg::TargetId id) const
    {
        const Rg::TargetInfo info = m_pool.get(id);
        return { info.storage ? static_cast<SIZE_T>(info.storage->rtv) : 0 };
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GraphTargets::dsv(Rg::TargetId id) const
    {
        const Rg::TargetInfo info = m_pool.get(id);
        return { info.storage ? static_cast<SIZE_T>(info.storage->dsv) : 0 };
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GraphTargets::readOnlyDsv(Rg::TargetId id) const
    {
        const Rg::TargetInfo info = m_pool.get(id);
        return { info.storage ? static_cast<SIZE_T>(info.storage->readOnlyDsv) : 0 };
    }

    uint32_t GraphTargets::width(Rg::TargetId id) const
    {
        return m_pool.get(id).extent.width;
    }

    uint32_t GraphTargets::height(Rg::TargetId id) const
    {
        return m_pool.get(id).extent.height;
    }

    uint64_t GraphTargets::generation(Rg::TargetId id) const
    {
        return m_pool.get(id).generation;
    }

    GraphTarget GraphTargets::registerTarget(const char* name, const Rg::TargetDesc& desc)
    {
        GraphTarget t;
        t.id         = registerTexture(name, desc);
        t.initial    = static_cast<D3D12_RESOURCE_STATES>(desc.initialState);
        t.state      = t.initial;
        t.generation = generation(t.id);
        return t;
    }

    void GraphTargets::unregisterTarget(GraphTarget& target)
    {
        unregisterTexture(target.id);
        target.state      = target.initial;
        target.generation = 0;
    }

    bool GraphTargets::refresh(GraphTarget& target) const
    {
        const uint64_t g = generation(target.id);
        if (g == target.generation)
            return false;
        target.generation = g;
        target.state      = target.initial;
        return true;
    }

    void GraphTargets::transition(ID3D12GraphicsCommandList* cmd, GraphTarget& target, D3D12_RESOURCE_STATES after) const
    {
        refresh(target);
        ID3D12Resource* res = resource(target.id);
        if (!cmd || !res || target.state == after)
            return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = res;
        b.Transition.StateBefore = target.state;
        b.Transition.StateAfter  = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
        target.state = after;
    }

    HistoryView GraphTargets::historyView(const Rg::HistoryTargets& history) const
    {
        HistoryView v;
        if (!history.valid())
            return v;
        v.curr      = resource(history.curr);
        v.currRtv   = rtv(history.curr);
        v.prevSrv   = srv(history.prev);
        v.prevValid = history.prevValid;
        return v;
    }

    void GraphTargets::logStats(const char* when) const
    {
        DE_LOG_INFO(LogCategory::Render, "GraphTargets ({}): {} persistent targets at {}x{}, {} awaiting release", when ? when : "stats", m_pool.count(), m_pool.outputWidth(), m_pool.outputHeight(),
                    m_backend.retiredCount());
        m_allocator.logStats(when);
    }

    Rg::TargetDesc colorTargetDesc(DXGI_FORMAT format, Rg::TargetSize size, uint32_t divisor, D3D12_RESOURCE_STATES initial, const float clear[4])
    {
        Rg::TargetDesc d;
        d.size         = size;
        d.divisor      = divisor;
        d.format       = static_cast<uint32_t>(format);
        d.flags        = static_cast<uint32_t>(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        d.initialState = static_cast<uint32_t>(initial);
        d.srv          = true;
        d.rtv          = true;
        d.clear        = clear != nullptr;
        if (clear)
            for (int i = 0; i < 4; ++i)
                d.color[i] = clear[i];
        return d;
    }

    Rg::TargetDesc copyTargetDesc(DXGI_FORMAT format, D3D12_RESOURCE_STATES initial)
    {
        Rg::TargetDesc d;
        d.size         = Rg::TargetSize::OutputFloor;
        d.format       = static_cast<uint32_t>(format);
        d.initialState = static_cast<uint32_t>(initial);
        d.srv          = true;
        return d;
    }

} // namespace Dark
