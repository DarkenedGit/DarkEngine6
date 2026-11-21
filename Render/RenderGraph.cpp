#include "Render/RenderGraph.h"

#include "Core/Log.h"
#include "Render/RenderGraphBarriers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Dark::Rg
{

    namespace
    {
        constexpr size_t kArenaBlockBytes = 64 * 1024;
        constexpr size_t kMaxResources    = kInvalidIndex; // indices 0..0xFFFE
        constexpr size_t kMaxLoggedErrors = 256;

        uint64_t fnv(const std::string& text)
        {
            uint64_t h = 1469598103934665603ull;
            for (const char c : text)
            {
                h ^= static_cast<unsigned char>(c);
                h *= 1099511628211ull;
            }
            return h;
        }

        const char* kindName(ResourceKind kind)
        {
            return kind == ResourceKind::Texture ? "texture" : "buffer";
        }

        uint32_t resolveDim(uint32_t output, float scale)
        {
            const float v = static_cast<float>(output) * scale;
            return v < 1.0f ? 1u : static_cast<uint32_t>(v);
        }
    } // namespace

    // ---------------------------------------------------------------- Arena

    namespace
    {
        // Bytes needed to bring `at` up to `align` (a power of two). Uses the real address, not the
        // offset into the block: operator new only guarantees 16 bytes (MSVC x64), so a block base
        // need not be 32- or 64-aligned.
        size_t paddingFor(const std::byte* at, size_t align)
        {
            const uintptr_t addr = reinterpret_cast<uintptr_t>(at);
            return static_cast<size_t>((~addr + 1) & (uintptr_t{ align } - 1));
        }
    } // namespace

    void* Arena::allocate(size_t bytes, size_t align)
    {
        DE_ASSERT(align != 0 && (align & (align - 1)) == 0, "Arena alignment must be a power of two");
        if (bytes == 0)
            bytes = 1;
        for (; m_current < m_blocks.size(); ++m_current)
        {
            Block&       b   = m_blocks[m_current];
            const size_t pad = paddingFor(b.base + b.used, align);
            if (pad <= b.size - b.used && bytes <= b.size - b.used - pad)
            {
                std::byte* out = b.base + b.used + pad;
                b.used += pad + bytes;
                m_used += bytes;
                return out;
            }
        }
        // A fresh block holds the request plus the worst-case padding for its alignment.
        Block        b;
        const size_t skewRoom = m_baseRemainder ? 64 : 0;
        b.size                = std::max(kArenaBlockBytes, bytes + align);
        b.data                = std::make_unique<std::byte[]>(b.size + skewRoom);
        b.base                = b.data.get();
        if (m_baseRemainder)
            b.base += (m_baseRemainder + 64 - reinterpret_cast<uintptr_t>(b.base) % 64) % 64;
        const size_t pad = paddingFor(b.base, align);
        std::byte*   out = b.base + pad;
        b.used           = pad + bytes;
        m_used += bytes;
        m_blocks.push_back(std::move(b));
        m_current = m_blocks.size() - 1;
        return out;
    }

    const char* Arena::copyString(const char* text)
    {
        if (!text)
            text = "";
        const size_t n   = std::strlen(text);
        char*        out = static_cast<char*>(allocate(n + 1, 1));
        std::memcpy(out, text, n + 1);
        return out;
    }

    void Arena::reset()
    {
        for (auto it = m_destructors.rbegin(); it != m_destructors.rend(); ++it)
            it->destroy(it->object);
        m_destructors.clear();
        for (Block& b : m_blocks)
            b.used = 0;
        // Keep one block (the first) so steady-state frames don't allocate.
        if (m_blocks.size() > 1)
            m_blocks.resize(1);
        m_current = 0;
        m_used    = 0;
    }

    // ---------------------------------------------------------------- PassBuilder

    TextureHandle PassBuilder::create(const char* name, const TextureDesc& desc)
    {
        RenderGraph::ResourceNode node;
        node.name     = m_graph.m_arena.copyString(name);
        node.kind     = ResourceKind::Texture;
        node.lifetime = Lifetime::Transient;
        node.tex      = desc;
        node.creator  = m_pass;
        node.versions.emplace_back();
        m_graph.resolveSize(node);
        const uint16_t index = m_graph.addResource(std::move(node), m_pass);
        return { index, 0 };
    }

    BufferHandle PassBuilder::create(const char* name, const BufferDesc& desc)
    {
        RenderGraph::ResourceNode node;
        node.name     = m_graph.m_arena.copyString(name);
        node.kind     = ResourceKind::Buffer;
        node.lifetime = Lifetime::Transient;
        node.buf      = desc;
        node.creator  = m_pass;
        node.versions.emplace_back();
        const uint16_t index = m_graph.addResource(std::move(node), m_pass);
        return { index, 0 };
    }

    TextureHandle PassBuilder::read(TextureHandle h, Use use, uint16_t mip)
    {
        const uint16_t v = m_graph.recordRead(m_pass, h.index, h.version, ResourceKind::Texture, use, mip);
        return v == kInvalidIndex ? TextureHandle{} : h;
    }

    TextureHandle PassBuilder::write(TextureHandle h, Use use)
    {
        const uint16_t v = m_graph.recordWrite(m_pass, h.index, h.version, ResourceKind::Texture, use, false);
        return v == kInvalidIndex ? TextureHandle{} : TextureHandle{ h.index, v };
    }

    TextureHandle PassBuilder::readWrite(TextureHandle h, Use use)
    {
        const uint16_t v = m_graph.recordWrite(m_pass, h.index, h.version, ResourceKind::Texture, use, true);
        return v == kInvalidIndex ? TextureHandle{} : TextureHandle{ h.index, v };
    }

    BufferHandle PassBuilder::read(BufferHandle h, Use use)
    {
        const uint16_t v = m_graph.recordRead(m_pass, h.index, h.version, ResourceKind::Buffer, use, kAllMips);
        return v == kInvalidIndex ? BufferHandle{} : h;
    }

    BufferHandle PassBuilder::write(BufferHandle h, Use use)
    {
        const uint16_t v = m_graph.recordWrite(m_pass, h.index, h.version, ResourceKind::Buffer, use, false);
        return v == kInvalidIndex ? BufferHandle{} : BufferHandle{ h.index, v };
    }

    BufferHandle PassBuilder::readWrite(BufferHandle h, Use use)
    {
        const uint16_t v = m_graph.recordWrite(m_pass, h.index, h.version, ResourceKind::Buffer, use, true);
        return v == kInvalidIndex ? BufferHandle{} : BufferHandle{ h.index, v };
    }

    void PassBuilder::queue(Queue q)
    {
        m_graph.m_passes[m_pass].declaredQueue = q;
    }
    void PassBuilder::flags(PassFlags f)
    {
        m_graph.m_passes[m_pass].flags = m_graph.m_passes[m_pass].flags | f;
    }

    void PassBuilder::scope(const char* gpuScopeName, uint32_t color)
    {
        m_graph.m_passes[m_pass].scopeName  = m_graph.m_arena.copyString(gpuScopeName);
        m_graph.m_passes[m_pass].scopeColor = color;
    }

    // ---------------------------------------------------------------- PassContext

    const char* PassContext::passName() const
    {
        return m_graph.passName(m_pass);
    }
    Queue PassContext::queue() const
    {
        return m_graph.passQueue(m_pass);
    }
    const char* PassContext::scopeName() const
    {
        return m_graph.m_passes[m_pass].scopeName;
    }
    uint32_t PassContext::scopeColor() const
    {
        return m_graph.m_passes[m_pass].scopeColor;
    }
    uint64_t PassContext::frameIndex() const
    {
        return m_graph.m_frameIndex;
    }

    uint32_t PassContext::width(TextureHandle h) const
    {
        const auto* r = m_graph.resourceAt(h.index, ResourceKind::Texture);
        return r ? r->width : 0;
    }
    uint32_t PassContext::height(TextureHandle h) const
    {
        const auto* r = m_graph.resourceAt(h.index, ResourceKind::Texture);
        return r ? r->height : 0;
    }
    const TextureDesc* PassContext::desc(TextureHandle h) const
    {
        const auto* r = m_graph.resourceAt(h.index, ResourceKind::Texture);
        return r ? &r->tex : nullptr;
    }
    ID3D12Resource* PassContext::resource(TextureHandle h) const
    {
        const auto* r = m_graph.resourceAt(h.index, ResourceKind::Texture);
        return r ? r->native : nullptr;
    }
    ID3D12Resource* PassContext::resource(BufferHandle h) const
    {
        const auto* r = m_graph.resourceAt(h.index, ResourceKind::Buffer);
        return r ? r->native : nullptr;
    }
    uint32_t PassContext::aliasSlot(TextureHandle h) const
    {
        return m_graph.resourceAt(h.index, ResourceKind::Texture) ? m_graph.m_cRes[h.index].aliasSlot : kNoPass;
    }
    uint32_t PassContext::aliasSlot(BufferHandle h) const
    {
        return m_graph.resourceAt(h.index, ResourceKind::Buffer) ? m_graph.m_cRes[h.index].aliasSlot : kNoPass;
    }
    uint32_t PassContext::historyId(TextureHandle h) const
    {
        const auto* r = m_graph.resourceAt(h.index, ResourceKind::Texture);
        return r ? r->historyId : kNoPass;
    }

    // ---------------------------------------------------------------- RenderGraph: frame

    RenderGraph::RenderGraph()
    {
        m_blackboard.m_arena = &m_arena;
    }
    RenderGraph::~RenderGraph() = default;

    void RenderGraph::beginFrame(uint32_t outputWidth, uint32_t outputHeight)
    {
        // End of the previous frame: swap the history pairs it wrote (§3.6).
        for (HistoryEntry& e : m_history)
        {
            if (!e.requested)
                continue;
            const bool written = m_executed && e.currRes != kInvalidIndex && e.currRes < m_resources.size() && m_resources[e.currRes].versions.size() > 1;
            if (written)
            {
                e.initialized[e.cur] = true;
                e.cur ^= 1;
                e.prevValid = true;
            }
            else
            {
                e.prevValid = false;
            }
        }
        // A history nobody asked for last frame belongs to a disabled feature: drop it (its memory goes in Phase 5).
        std::erase_if(m_history, [](const HistoryEntry& e) { return !e.requested; });
        for (HistoryEntry& e : m_history)
        {
            e.requested = false;
            e.prevRes   = kInvalidIndex;
            e.currRes   = kInvalidIndex;
        }
        if (m_frameIndex > 0 && (outputWidth != m_outputWidth || outputHeight != m_outputHeight))
            for (HistoryEntry& e : m_history)
                e.prevValid = false;

        ++m_frameIndex;
        m_outputWidth  = outputWidth;
        m_outputHeight = outputHeight;
        m_passes.clear();
        m_resources.clear();
        m_errors.clear();
        m_blackboard.reset();
        m_arena.reset();
        m_executed   = false;
        m_compiledOk = false;
        m_fromCache  = false;
    }

    uint32_t RenderGraph::beginPassRecord(const char* name)
    {
        PassNode node;
        node.name = m_arena.copyString(name);
        m_passes.push_back(std::move(node));
        return static_cast<uint32_t>(m_passes.size() - 1);
    }

    void RenderGraph::setPassThunk(uint32_t pass, const void* data, void* exec, ThunkFn fn)
    {
        m_passes[pass].data  = data;
        m_passes[pass].exec  = exec;
        m_passes[pass].thunk = fn;
    }

    void RenderGraph::addError(ErrorCode code, uint32_t pass, uint16_t resource, std::string message)
    {
        Error e;
        e.code     = code;
        e.pass     = pass;
        e.resource = resource;
        e.message  = std::move(message);
        m_errors.push_back(std::move(e));
    }

    void RenderGraph::logErrorsOnce()
    {
        for (const Error& e : m_errors)
        {
            const uint64_t h = fnv(e.message);
            if (std::find(m_loggedErrors.begin(), m_loggedErrors.end(), h) != m_loggedErrors.end())
                continue;
            if (m_loggedErrors.size() < kMaxLoggedErrors)
                m_loggedErrors.push_back(h);
            const char* code = errorCodeName(e.code);
            DE_LOG_ERROR(LogCategory::Render, "RenderGraph: {} ({})", e.message, code);
        }
    }

    bool RenderGraph::hasError(ErrorCode code) const
    {
        return std::any_of(m_errors.begin(), m_errors.end(), [code](const Error& e) { return e.code == code; });
    }

    uint16_t RenderGraph::addResource(ResourceNode&& node, uint32_t pass)
    {
        if (m_resources.size() >= kMaxResources)
        {
            addError(ErrorCode::TooManyResources, pass, kInvalidIndex, std::string("too many resources this frame, '") + node.name + "' dropped");
            return kInvalidIndex;
        }
        m_resources.push_back(std::move(node));
        return static_cast<uint16_t>(m_resources.size() - 1);
    }

    void RenderGraph::resolveSize(ResourceNode& node) const
    {
        if (node.tex.sizeMode == SizeMode::Absolute)
        {
            node.width  = node.tex.width;
            node.height = node.tex.height;
        }
        else
        {
            node.width  = resolveDim(m_outputWidth, node.tex.scale);
            node.height = resolveDim(m_outputHeight, node.tex.scale);
        }
    }

    const RenderGraph::ResourceNode* RenderGraph::resourceAt(uint16_t index, ResourceKind kind) const
    {
        if (index >= m_resources.size() || m_resources[index].kind != kind)
            return nullptr;
        return &m_resources[index];
    }

    bool RenderGraph::checkHandle(uint32_t pass, uint16_t index, uint16_t version, ResourceKind kind, const char* what)
    {
        const std::string passText = pass == kNoPass ? std::string("graph") : std::string("pass '") + m_passes[pass].name + "'";
        if (index >= m_resources.size())
        {
            addError(ErrorCode::InvalidHandle, pass, kInvalidIndex, passText + ": " + what + " of an invalid " + kindName(kind) + " handle");
            return false;
        }
        const ResourceNode& r = m_resources[index];
        if (r.kind != kind || version >= r.versions.size())
        {
            addError(ErrorCode::InvalidHandle, pass, index, passText + ": " + what + " of '" + r.name + "' with a stale or mismatched " + kindName(kind) + " handle");
            return false;
        }
        return true;
    }

    uint16_t RenderGraph::recordRead(uint32_t pass, uint16_t index, uint16_t version, ResourceKind kind, Use use, uint16_t mip)
    {
        if (!checkHandle(pass, index, version, kind, "read"))
            return kInvalidIndex;
        ResourceNode&     r     = m_resources[index];
        const std::string where = std::string("pass '") + m_passes[pass].name + "' reads '" + r.name + "'";
        if (use == Use::Present)
        {
            addError(ErrorCode::PresentUse, pass, index, where + " as Present; Present is the back buffer's final layout, not a pass use");
            return kInvalidIndex;
        }
        if (!isReadUse(use))
        {
            addError(ErrorCode::NotAReadUse, pass, index, where + " with write use " + useName(use) + "; use write() or readWrite()");
            return kInvalidIndex;
        }
        if (!useAllowedForKind(use, kind))
        {
            addError(ErrorCode::WrongUseForKind, pass, index, where + " as " + useName(use) + ", which a " + kindName(kind) + " can't be");
            return kInvalidIndex;
        }
        if (r.versions[version].writer == kNoPass)
        {
            addError(ErrorCode::ReadBeforeWrite, pass, index,
                     where + " version " + std::to_string(version) + " before any pass wrote it" + (r.lifetime == Lifetime::History ? " (a history curr: read prev instead)" : ""));
            return kInvalidIndex;
        }
        AccessRec a;
        a.resource = index;
        a.version  = version;
        a.use      = use;
        a.read     = true;
        a.mip      = mip;
        m_passes[pass].accesses.push_back(a);
        auto& readers = r.versions[version].readers;
        if (std::find(readers.begin(), readers.end(), pass) == readers.end())
            readers.push_back(pass);
        return version;
    }

    uint16_t RenderGraph::recordWrite(uint32_t pass, uint16_t index, uint16_t version, ResourceKind kind, Use use, bool alsoRead)
    {
        if (!checkHandle(pass, index, version, kind, alsoRead ? "readWrite" : "write"))
            return kInvalidIndex;
        ResourceNode&     r     = m_resources[index];
        const std::string where = std::string("pass '") + m_passes[pass].name + "' writes '" + r.name + "'";
        if (use == Use::Present)
        {
            addError(ErrorCode::PresentUse, pass, index, where + " as Present; Present is the back buffer's final layout, not a pass use");
            return kInvalidIndex;
        }
        if (!isWriteUse(use))
        {
            addError(ErrorCode::NotAWriteUse, pass, index, where + " with read-only use " + useName(use));
            return kInvalidIndex;
        }
        if (!useAllowedForKind(use, kind))
        {
            addError(ErrorCode::WrongUseForKind, pass, index, where + " as " + useName(use) + ", which a " + kindName(kind) + " can't be");
            return kInvalidIndex;
        }
        if (static_cast<size_t>(version) + 1 != r.versions.size())
        {
            const uint32_t    other     = r.versions[version + 1].writer;
            const std::string otherName = other < m_passes.size() ? std::string(m_passes[other].name) : std::string("?");
            addError(ErrorCode::StaleWrite, pass, index,
                     where + " version " + std::to_string(version) + ", but pass '" + otherName + "' already wrote version " + std::to_string(version + 1) +
                         " (two writers with no order: write the handle the other pass returned)");
            return kInvalidIndex;
        }
        if (alsoRead && r.versions[version].writer == kNoPass)
        {
            addError(ErrorCode::ReadBeforeWrite, pass, index, where + " with readWrite before any pass wrote it");
            return kInvalidIndex;
        }
        if (r.versions.size() >= kInvalidIndex)
        {
            addError(ErrorCode::TooManyResources, pass, index, where + ": too many versions");
            return kInvalidIndex;
        }
        VersionNode v;
        v.writer = pass;
        r.versions.push_back(std::move(v));
        AccessRec a;
        a.resource = index;
        a.version  = version;
        a.use      = use;
        a.read     = alsoRead;
        a.write    = true;
        m_passes[pass].accesses.push_back(a);
        return static_cast<uint16_t>(version + 1);
    }

    // ---------------------------------------------------------------- imports and history

    TextureHandle RenderGraph::importTexture(const char* name, const TextureDesc& desc, ID3D12Resource* native, Layout initial, Layout finalLayout)
    {
        ResourceNode node;
        node.name        = m_arena.copyString(name);
        node.kind        = ResourceKind::Texture;
        node.lifetime    = Lifetime::Imported;
        node.tex         = desc;
        node.native      = native;
        node.initial     = initial;
        node.finalLayout = finalLayout;
        node.hasFinal    = finalLayout != Layout::Undefined;
        VersionNode v0;
        v0.writer = kPrologue; // contents come from outside the graph
        node.versions.push_back(std::move(v0));
        resolveSize(node);
        const uint16_t index = addResource(std::move(node), kNoPass);
        return index == kInvalidIndex ? TextureHandle{} : TextureHandle{ index, 0 };
    }

    BufferHandle RenderGraph::importBuffer(const char* name, const BufferDesc& desc, ID3D12Resource* native)
    {
        ResourceNode node;
        node.name     = m_arena.copyString(name);
        node.kind     = ResourceKind::Buffer;
        node.lifetime = Lifetime::Imported;
        node.buf      = desc;
        node.native   = native;
        VersionNode v0;
        v0.writer = kPrologue;
        node.versions.push_back(std::move(v0));
        const uint16_t index = addResource(std::move(node), kNoPass);
        return index == kInvalidIndex ? BufferHandle{} : BufferHandle{ index, 0 };
    }

    TextureHandle RenderGraph::importBackBuffer(ID3D12Resource* native, uint32_t width, uint32_t height, uint32_t dxgiFormat)
    {
        TextureDesc desc;
        desc.sizeMode = SizeMode::Absolute;
        desc.width    = width;
        desc.height   = height;
        desc.format   = dxgiFormat;
        return importTexture("backbuffer", desc, native, Layout::Present, Layout::Present);
    }

    HistoryPair RenderGraph::history(const char* name, const TextureDesc& desc)
    {
        const std::string key = name ? name : "";
        ResourceNode      probe;
        probe.tex = desc;
        resolveSize(probe);

        HistoryEntry* entry = nullptr;
        for (HistoryEntry& e : m_history)
            if (e.name == key)
                entry = &e;

        if (entry && entry->requested)
        {
            if (!(entry->desc == desc))
            {
                addError(ErrorCode::HistoryDescMismatch, kNoPass, entry->currRes, "history '" + key + "' requested twice this frame with different descs");
                return {};
            }
            HistoryPair again;
            if (entry->prevRes != kInvalidIndex)
                again.prev = { entry->prevRes, 0 };
            if (entry->currRes != kInvalidIndex)
                again.curr = { entry->currRes, 0 };
            return again;
        }
        if (!entry)
        {
            m_history.emplace_back();
            entry         = &m_history.back();
            entry->name   = key;
            entry->ids[0] = m_nextHistoryId++;
            entry->ids[1] = m_nextHistoryId++;
        }
        else if (!(entry->desc == desc) || entry->width != probe.width || entry->height != probe.height)
        {
            // New size or format: new allocations (Phase 5), nothing to read.
            entry->ids[0]         = m_nextHistoryId++;
            entry->ids[1]         = m_nextHistoryId++;
            entry->initialized[0] = entry->initialized[1] = false;
            entry->cur                                    = 0;
            entry->prevValid                              = false;
        }
        entry->desc      = desc;
        entry->width     = probe.width;
        entry->height    = probe.height;
        entry->requested = true;

        HistoryPair pair;
        {
            ResourceNode curr;
            curr.name        = m_arena.copyString((key + ".curr").c_str());
            curr.kind        = ResourceKind::Texture;
            curr.lifetime    = Lifetime::History;
            curr.tex         = desc;
            curr.initial     = entry->initialized[entry->cur] ? Layout::ShaderResource : Layout::Undefined;
            curr.finalLayout = Layout::ShaderResource; // resting layout, legal on both queues
            curr.hasFinal    = true;
            curr.historyId   = entry->ids[entry->cur];
            curr.versions.emplace_back(); // must be written before it is read
            resolveSize(curr);
            // addResource may grow m_resources; entry points into m_history, which it doesn't touch.
            entry->currRes = addResource(std::move(curr), kNoPass);
            if (entry->currRes != kInvalidIndex)
                pair.curr = { entry->currRes, 0 };
        }
        if (entry->prevValid)
        {
            ResourceNode prev;
            prev.name        = m_arena.copyString((key + ".prev").c_str());
            prev.kind        = ResourceKind::Texture;
            prev.lifetime    = Lifetime::History;
            prev.tex         = desc;
            prev.initial     = Layout::ShaderResource;
            prev.finalLayout = Layout::ShaderResource;
            prev.hasFinal    = true;
            prev.historyId   = entry->ids[entry->cur ^ 1];
            VersionNode v0;
            v0.writer = kPrologue; // last frame's contents
            prev.versions.push_back(std::move(v0));
            resolveSize(prev);
            entry->prevRes = addResource(std::move(prev), kNoPass);
            if (entry->prevRes != kInvalidIndex)
                pair.prev = { entry->prevRes, 0 };
        }
        return pair;
    }

    void RenderGraph::invalidateHistory()
    {
        for (HistoryEntry& e : m_history)
            e.prevValid = false;
    }

    // ---------------------------------------------------------------- execute

    bool RenderGraph::execute(Executor* executor)
    {
        if (!m_compiledOk)
        {
            addError(ErrorCode::NotCompiled, kNoPass, kInvalidIndex, "execute() without a successful compile()");
            logErrorsOnce();
            return false;
        }
        size_t     batch    = 0;
        const auto runBatch = [&](BarrierPlacement placement, uint32_t pass)
        {
            while (batch < m_batches.size() && m_batches[batch].placement == placement && m_batches[batch].pass == pass)
            {
                if (executor)
                    executor->barriers(m_batches[batch], m_barriers.data() + m_batches[batch].first);
                ++batch;
            }
        };
        const auto waits = [&](uint32_t item, Queue q)
        {
            if (!executor)
                return;
            for (const SyncPoint& s : m_syncs)
                if (s.beforePass == item)
                    executor->wait(q, s.signalQueue, s.signalValue);
        };
        const auto signals = [&](uint32_t item, Queue q)
        {
            if (!executor)
                return;
            // Every sync point after one item shares that item's signal value: signal it once.
            for (const SyncPoint& s : m_syncs)
                if (s.afterPass == item && s.signalQueue == q)
                {
                    executor->signal(q, s.signalValue);
                    return;
                }
        };

        runBatch(BarrierPlacement::Prologue, kPrologue);
        signals(kPrologue, Queue::Graphics);
        for (const uint32_t p : m_order)
        {
            const Queue q = m_cPass[p].queue;
            waits(p, q);
            runBatch(BarrierPlacement::BeforePass, p);
            PassContext ctx(*this, p, executor);
            if (executor)
                executor->beginPass(ctx);
            if (m_passes[p].thunk)
                m_passes[p].thunk(m_passes[p].exec, m_passes[p].data, ctx);
            if (executor)
                executor->endPass(ctx);
            runBatch(BarrierPlacement::AfterPass, p);
            signals(p, q);
        }
        waits(kEpilogue, Queue::Graphics);
        runBatch(BarrierPlacement::Epilogue, kEpilogue);
        m_executed = true;
        return true;
    }

    // ---------------------------------------------------------------- queries

    uint32_t RenderGraph::findPass(const char* name) const
    {
        for (uint32_t i = 0; i < m_passes.size(); ++i)
            if (name && std::strcmp(m_passes[i].name, name) == 0)
                return i;
        return kNoPass;
    }

    const char* RenderGraph::passName(uint32_t pass) const
    {
        if (pass == kPrologue)
            return "<prologue>";
        if (pass == kEpilogue)
            return "<epilogue>";
        return pass < m_passes.size() ? m_passes[pass].name : "";
    }

    bool RenderGraph::passCulled(uint32_t pass) const
    {
        return pass >= m_cPass.size() || !m_compiledOk || !m_cPass[pass].live;
    }

    Queue RenderGraph::passQueue(uint32_t pass) const
    {
        if (pass < m_cPass.size() && m_compiledOk)
            return m_cPass[pass].queue;
        return pass < m_passes.size() && m_options.asyncCompute ? m_passes[pass].declaredQueue : Queue::Graphics;
    }

    uint32_t RenderGraph::passOrder(uint32_t pass) const
    {
        return pass < m_cPass.size() && m_compiledOk ? m_cPass[pass].order : kNoPass;
    }

    uint16_t RenderGraph::findResource(const char* name) const
    {
        for (size_t i = 0; i < m_resources.size(); ++i)
            if (name && std::strcmp(m_resources[i].name, name) == 0)
                return static_cast<uint16_t>(i);
        return kInvalidIndex;
    }

    ResourceInfo RenderGraph::resourceInfo(uint16_t resource) const
    {
        ResourceInfo info;
        if (resource >= m_resources.size())
            return info;
        const ResourceNode& r = m_resources[resource];
        info.name             = r.name;
        info.kind             = r.kind;
        info.lifetime         = r.lifetime;
        info.width            = r.width;
        info.height           = r.height;
        info.versions         = static_cast<uint16_t>(r.versions.size());
        info.historyId        = r.historyId;
        if (m_compiledOk && resource < m_cRes.size())
        {
            const CompiledResource& c = m_cRes[resource];
            info.bytes                = c.bytes;
            info.used                 = c.used;
            info.firstPass            = c.firstPass;
            info.lastPass             = c.lastPass;
            info.queueMask            = c.queueMask;
            info.aliasSlot            = c.aliasSlot;
        }
        return info;
    }

} // namespace Dark::Rg
