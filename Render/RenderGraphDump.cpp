#include "Render/RenderGraph.h"

#include "Render/RenderGraphBarriers.h"

#include <cstdint>
#include <cstdio>
#include <string>

// DOT / JSON dumps of the frame's graph (plan §3.12): passes, queues, culled passes, resources and
// versions, sync points and barriers. The Graph Inspector (Phase 13) and -rgdump read the JSON.

namespace Dark::Rg
{

    namespace
    {
        void appendEscaped(std::string& out, const char* text)
        {
            for (const char* c = text ? text : ""; *c; ++c)
            {
                switch (*c)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (static_cast<unsigned char>(*c) < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(*c)));
                        out += buf;
                    }
                    else
                    {
                        out += *c;
                    }
                }
            }
        }

        std::string quoted(const char* text)
        {
            std::string s = "\"";
            appendEscaped(s, text);
            s += '"';
            return s;
        }

        std::string hex(uint32_t v)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "0x%x", v);
            return buf;
        }

        const char* placementName(BarrierPlacement p)
        {
            switch (p)
            {
            case BarrierPlacement::BeforePass:
                return "before";
            case BarrierPlacement::AfterPass:
                return "after";
            case BarrierPlacement::Prologue:
                return "prologue";
            case BarrierPlacement::Epilogue:
                return "epilogue";
            }
            return "?";
        }

        const char* lifetimeName(Lifetime l)
        {
            switch (l)
            {
            case Lifetime::Transient:
                return "transient";
            case Lifetime::Imported:
                return "imported";
            case Lifetime::History:
                return "history";
            }
            return "?";
        }

        std::string itemJson(uint32_t item)
        {
            if (item == kPrologue)
                return "\"prologue\"";
            if (item == kEpilogue)
                return "\"epilogue\"";
            return std::to_string(item);
        }

        std::string stateJson(const UseState& s)
        {
            return std::string("{\"sync\":\"") + hex(static_cast<uint32_t>(s.sync)) + "\",\"access\":\"" + hex(static_cast<uint32_t>(s.access)) + "\",\"layout\":\"" + layoutName(s.layout) + "\"}";
        }
    } // namespace

    void RenderGraph::dumpDot(std::string& out) const
    {
        out = "digraph RenderGraph {\n  rankdir=LR;\n  node [fontname=\"Consolas\"];\n";
        for (uint32_t p = 0; p < m_passes.size(); ++p)
        {
            const bool culled = passCulled(p);
            out += "  p" + std::to_string(p) + " [shape=box,label=" + quoted((std::string(m_passes[p].name) + "\n" + queueName(passQueue(p))).c_str());
            if (culled)
                out += ",style=dashed,color=gray,fontcolor=gray";
            else if (passQueue(p) == Queue::AsyncCompute)
                out += ",style=filled,fillcolor=\"#dde8ff\"";
            out += "];\n";
        }
        for (uint32_t r = 0; r < m_resources.size(); ++r)
        {
            const ResourceNode& res = m_resources[r];
            for (uint32_t v = 0; v < res.versions.size(); ++v)
            {
                const std::string id = "r" + std::to_string(r) + "_" + std::to_string(v);
                out += "  " + id + " [shape=ellipse,label=" + quoted((std::string(res.name) + " v" + std::to_string(v)).c_str()) + "];\n";
                const uint32_t w = res.versions[v].writer;
                if (w < m_passes.size())
                    out += "  p" + std::to_string(w) + " -> " + id + ";\n";
                for (const uint32_t reader : res.versions[v].readers)
                    out += "  " + id + " -> p" + std::to_string(reader) + ";\n";
            }
        }
        if (m_compiledOk)
        {
            for (const SyncPoint& s : m_syncs)
            {
                if (s.afterPass >= m_passes.size() || s.beforePass >= m_passes.size())
                    continue;
                out += "  p" + std::to_string(s.afterPass) + " -> p" + std::to_string(s.beforePass) +
                       " [style=dashed,color=red,label=" + quoted((std::string("fence ") + queueName(s.signalQueue) + " " + std::to_string(s.signalValue)).c_str()) + "];\n";
            }
        }
        out += "}\n";
    }

    void RenderGraph::dumpJson(std::string& out) const
    {
        out = "{\"frame\":" + std::to_string(m_frameIndex) + ",\"output\":[" + std::to_string(m_outputWidth) + "," + std::to_string(m_outputHeight) + "]";
        out += std::string(",\"compiled\":") + (m_compiledOk ? "true" : "false");
        out += std::string(",\"options\":{\"asyncCompute\":") + (m_options.asyncCompute ? "true" : "false") + ",\"noCull\":" + (m_options.noCull ? "true" : "false") +
               ",\"fullBarriers\":" + (m_options.fullBarriers ? "true" : "false") + ",\"noAliasing\":" + (m_options.noAliasing ? "true" : "false") + "}";

        out += ",\"passes\":[";
        for (uint32_t p = 0; p < m_passes.size(); ++p)
        {
            const PassNode& n = m_passes[p];
            if (p)
                out += ",";
            out += "{\"index\":" + std::to_string(p) + ",\"name\":" + quoted(n.name) + ",\"queue\":" + quoted(queueName(passQueue(p))) + ",\"declaredQueue\":" + quoted(queueName(n.declaredQueue)) +
                   ",\"culled\":" + (passCulled(p) ? "true" : "false") + ",\"order\":" + (passOrder(p) == kNoPass ? std::string("null") : std::to_string(passOrder(p))) +
                   ",\"flags\":" + std::to_string(static_cast<uint32_t>(n.flags)) + ",\"accesses\":[";
            for (size_t i = 0; i < n.accesses.size(); ++i)
            {
                const AccessRec& a = n.accesses[i];
                if (i)
                    out += ",";
                out += "{\"resource\":" + std::to_string(a.resource) + ",\"version\":" + std::to_string(a.version) + ",\"use\":" + quoted(useName(a.use)) + ",\"read\":" + (a.read ? "true" : "false") +
                       ",\"write\":" + (a.write ? "true" : "false") + "}";
            }
            out += "]}";
        }
        out += "]";

        out += ",\"resources\":[";
        for (uint16_t r = 0; r < m_resources.size(); ++r)
        {
            const ResourceInfo info = resourceInfo(r);
            if (r)
                out += ",";
            out += "{\"index\":" + std::to_string(r) + ",\"name\":" + quoted(info.name) + ",\"kind\":" + quoted(info.kind == ResourceKind::Texture ? "texture" : "buffer") +
                   ",\"lifetime\":" + quoted(lifetimeName(info.lifetime)) + ",\"width\":" + std::to_string(info.width) + ",\"height\":" + std::to_string(info.height) +
                   ",\"bytes\":" + std::to_string(info.bytes) + ",\"versions\":" + std::to_string(info.versions) + ",\"used\":" + (info.used ? "true" : "false") +
                   ",\"first\":" + (info.firstPass == kNoPass ? std::string("null") : std::to_string(info.firstPass)) +
                   ",\"last\":" + (info.lastPass == kNoPass ? std::string("null") : std::to_string(info.lastPass)) +
                   ",\"aliasSlot\":" + (info.aliasSlot == kNoPass ? std::string("null") : std::to_string(info.aliasSlot)) + "}";
        }
        out += "]";

        if (m_compiledOk)
        {
            out += ",\"syncs\":[";
            for (size_t i = 0; i < m_syncs.size(); ++i)
            {
                const SyncPoint& s = m_syncs[i];
                if (i)
                    out += ",";
                out += "{\"signalQueue\":" + quoted(queueName(s.signalQueue)) + ",\"after\":" + itemJson(s.afterPass) + ",\"waitQueue\":" + quoted(queueName(s.waitQueue)) +
                       ",\"before\":" + itemJson(s.beforePass) + ",\"value\":" + std::to_string(s.signalValue) + "}";
            }
            out += "],\"barriers\":[";
            for (size_t i = 0; i < m_barriers.size(); ++i)
            {
                const BarrierRec& b = m_barriers[i];
                if (i)
                    out += ",";
                out += "{\"resource\":" + std::to_string(b.resource) + ",\"queue\":" + quoted(queueName(b.queue)) + ",\"placement\":" + quoted(placementName(b.placement)) +
                       ",\"pass\":" + itemJson(b.pass) + ",\"before\":" + stateJson(b.before) + ",\"after\":" + stateJson(b.after) + ",\"discard\":" + (b.discard ? "true" : "false") +
                       ",\"aliased\":" + (b.aliased ? "true" : "false") + "}";
            }
            out += "],\"aliasSlots\":[";
            for (size_t i = 0; i < m_aliasSlots.size(); ++i)
            {
                if (i)
                    out += ",";
                out += "{\"bytes\":" + std::to_string(m_aliasSlots[i].bytes) + ",\"resources\":[";
                for (size_t j = 0; j < m_aliasSlots[i].resources.size(); ++j)
                    out += (j ? "," : "") + std::to_string(m_aliasSlots[i].resources[j]);
                out += "]}";
            }
            out += "],\"transientBytes\":{\"unaliased\":" + std::to_string(m_transientBytesUnaliased) + ",\"aliased\":" + std::to_string(m_transientBytesAliased) + "}";
        }

        out += ",\"errors\":[";
        for (size_t i = 0; i < m_errors.size(); ++i)
        {
            if (i)
                out += ",";
            out += "{\"code\":" + quoted(errorCodeName(m_errors[i].code)) + ",\"message\":" + quoted(m_errors[i].message.c_str()) + "}";
        }
        out += "]}";
    }

} // namespace Dark::Rg
