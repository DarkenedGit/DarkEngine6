#pragma once

// Shared helpers for the RenderGraph unit tests (Phase 3: CPU graph core, no D3D12).

#include <gtest/gtest.h>

#include "Render/RenderGraph.h"
#include "Render/RenderGraphBarriers.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Dark::Rg::Test
{

    // DXGI_FORMAT values (the graph stores formats as integers; no dxgiformat.h here).
    inline constexpr uint32_t kRgba16F   = 10;
    inline constexpr uint32_t kR11G11B10 = 26;
    inline constexpr uint32_t kRgba8     = 28;
    inline constexpr uint32_t kD32       = 40;
    inline constexpr uint32_t kR32F      = 41;
    inline constexpr uint32_t kR8        = 61;
    inline constexpr uint32_t kBgra8     = 87;

    inline TextureDesc tex(uint32_t format = kRgba16F, float scale = 1.0f)
    {
        TextureDesc d;
        d.format = format;
        d.scale  = scale;
        return d;
    }

    inline TextureDesc absTex(uint32_t w, uint32_t h, uint32_t format = kRgba16F)
    {
        TextureDesc d;
        d.sizeMode = SizeMode::Absolute;
        d.width    = w;
        d.height   = h;
        d.format   = format;
        return d;
    }

    inline BufferDesc buf(uint64_t size, uint32_t stride = 0)
    {
        BufferDesc d;
        d.size   = size;
        d.stride = stride;
        return d;
    }

    // A pass with no data and an empty execute; returns its index.
    template <class Setup> uint32_t pass(RenderGraph& g, const char* name, Setup&& setup)
    {
        g.addPass<NoData>(name, [&](PassBuilder& b, NoData&) { setup(b); }, [](PassContext&, const NoData&) {});
        return g.passCount() - 1;
    }

    inline std::string errorText(const RenderGraph& g)
    {
        std::string s;
        for (const Error& e : g.errors())
            s += std::string(errorCodeName(e.code)) + ": " + e.message + "\n";
        return s;
    }

    // compile() + validateCompiled() with readable failure output.
    inline ::testing::AssertionResult compiles(RenderGraph& g)
    {
        if (!g.compile())
            return ::testing::AssertionFailure() << "compile failed:\n" << errorText(g);
        std::string why;
        if (!g.validateCompiled(&why))
            return ::testing::AssertionFailure() << "validateCompiled: " << why;
        return ::testing::AssertionSuccess();
    }

    inline uint32_t countErrors(const RenderGraph& g, ErrorCode code)
    {
        uint32_t n = 0;
        for (const Error& e : g.errors())
            n += e.code == code ? 1u : 0u;
        return n;
    }

    // Barriers on one resource, in plan order.
    inline std::vector<BarrierRec> barriersFor(const RenderGraph& g, const char* resource)
    {
        std::vector<BarrierRec> out;
        const uint16_t          r = g.findResource(resource);
        for (const BarrierRec& b : g.barriers())
            if (b.resource == r)
                out.push_back(b);
        return out;
    }

    inline const char* placementText(BarrierPlacement p)
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

    // Records every executor callback as a short string.
    class Recorder : public Executor
    {
    public:
        explicit Recorder(const RenderGraph& g) :
            m_graph(g)
        {
        }

        void barriers(const BarrierBatch& batch, const BarrierRec* records) override
        {
            std::string s = std::string("B ") + placementText(batch.placement) + " " + itemName(batch.pass) + " " + queueName(batch.queue) + " [";
            for (uint32_t i = 0; i < batch.count; ++i)
                s += std::string(i ? "," : "") + m_graph.resourceInfo(records[i].resource).name;
            events.push_back(s + "]");
            for (uint32_t i = 0; i < batch.count; ++i)
                records_.push_back(records[i]);
        }
        void signal(Queue queue, uint32_t value) override
        {
            events.push_back(std::string("S ") + queueName(queue) + " " + std::to_string(value));
        }
        void wait(Queue waitQueue, Queue signalQueue, uint32_t value) override
        {
            events.push_back(std::string("W ") + queueName(waitQueue) + "<-" + queueName(signalQueue) + " " + std::to_string(value));
        }
        void beginPass(const PassContext& ctx) override
        {
            events.push_back(std::string("P ") + ctx.passName());
        }
        void endPass(const PassContext& ctx) override
        {
            events.push_back(std::string("E ") + ctx.passName());
        }

        std::string itemName(uint32_t item) const
        {
            if (item == kPrologue)
                return "prologue";
            if (item == kEpilogue)
                return "epilogue";
            return m_graph.passName(item);
        }

        // Index of the first event equal to text, or -1.
        int find(const std::string& text) const
        {
            for (size_t i = 0; i < events.size(); ++i)
                if (events[i] == text)
                    return static_cast<int>(i);
            return -1;
        }

        std::vector<std::string> events;
        std::vector<BarrierRec>  records_;

    private:
        const RenderGraph& m_graph;
    };

} // namespace Dark::Rg::Test
