#include "RenderGraphTestUtil.h"

#include "Render/RenderTargetPool.h"
#include "Render/TemporalHistory.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// HistoryTargetSet (render-graph plan Phase 5): RenderGraph history slots bound to pool targets.
// The graph decides validity and ping-pong; the set registers one target per live slot and frees
// slots the graph dropped (feature off, resize, format change).

using namespace Dark::Rg;
using namespace Dark::Rg::Test;

namespace
{
    struct NamedStorage : TargetStorage
    {
        std::string name;
    };

    class CountingBackend : public TargetBackend
    {
    public:
        std::unique_ptr<TargetStorage> create(const TargetDesc&, TargetExtent extent, const char* name) override
        {
            ++creates;
            auto s     = std::make_unique<NamedStorage>();
            s->name    = name;
            s->native  = reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0x1000 + creates * 0x10));
            s->srv     = 0x100u + static_cast<uintptr_t>(creates);
            lastExtent = extent;
            names.push_back(name);
            return s;
        }
        void retire(std::unique_ptr<TargetStorage>) override
        {
            ++retires;
        }
        int                      creates = 0;
        int                      retires = 0;
        TargetExtent             lastExtent;
        std::vector<std::string> names;
    };

    TargetDesc historyDesc(uint32_t format = kRgba16F)
    {
        TargetDesc d;
        d.format       = format;
        d.rtv          = true;
        d.initialState = 0x80; // PIXEL_SHADER_RESOURCE
        return d;
    }

    // One frame: request "Taa" (when asked), write curr unless skip, compile, execute, collect.
    struct Frame
    {
        RenderGraph      graph;
        CountingBackend  backend;
        RenderTargetPool pool{ backend };
        HistoryTargetSet set;

        HistoryTargets run(uint32_t w, uint32_t h, bool request = true, bool write = true, uint32_t format = kRgba16F, bool invalidate = false)
        {
            if (pool.outputWidth() != w || pool.outputHeight() != h)
            {
                EXPECT_TRUE(pool.resizeAfterDrain(w, h));
            }
            graph.beginFrame(w, h);
            const TextureHandle bb = graph.importBackBuffer(nullptr, w, h, kBgra8);
            if (invalidate)
                graph.invalidateHistory();
            HistoryTargets out;
            if (request)
                out = set.request(graph, pool, "Taa", historyDesc(format));
            pass(graph, "Post",
                 [&](PassBuilder& b)
                 {
                     if (out.pair.prev.valid())
                         b.read(out.pair.prev, Use::PixelSrv);
                     if (out.pair.curr.valid())
                         b.write(out.pair.curr, Use::RenderTarget);
                     b.write(bb, Use::RenderTarget);
                 });
            EXPECT_TRUE(compiles(graph));
            set.collect(graph, pool);
            EXPECT_TRUE(graph.execute());
            if (request && !write)
                graph.markHistoryUnwritten("Taa");
            return out;
        }
    };
} // namespace

TEST(TemporalHistory, FirstUseRegistersBothSlotsWithPixNamesAndNoValidPrev)
{
    Frame                f;
    const HistoryTargets h = f.run(1280, 720);
    ASSERT_TRUE(h.valid());
    EXPECT_FALSE(h.prevValid);
    EXPECT_NE(h.prev, h.curr);
    EXPECT_EQ(f.pool.count(), 2u);
    EXPECT_EQ(f.set.count(), 2u);
    ASSERT_EQ(f.backend.names.size(), 2u);
    EXPECT_EQ(f.backend.names[0].rfind("DE.History.Taa#", 0), 0u) << f.backend.names[0];
    EXPECT_EQ(f.backend.lastExtent, (TargetExtent{ 1280, 720 }));
    // Output-relative descs become Absolute in the pool, so a pool resize never touches a history.
    EXPECT_EQ(f.pool.get(h.curr).generation, 1u);
}

TEST(TemporalHistory, PingPongsWithoutNewAllocations)
{
    Frame                f;
    const HistoryTargets h1 = f.run(640, 360);
    const HistoryTargets h2 = f.run(640, 360);
    ASSERT_TRUE(h2.valid());
    EXPECT_TRUE(h2.prevValid);
    EXPECT_EQ(h2.prev, h1.curr); // last frame's curr is read this frame
    EXPECT_EQ(h2.curr, h1.prev);
    const HistoryTargets h3 = f.run(640, 360);
    EXPECT_EQ(h3.curr, h1.curr);
    EXPECT_TRUE(h3.prevValid);
    EXPECT_EQ(f.backend.creates, 2);
    EXPECT_EQ(f.backend.retires, 0);
}

TEST(TemporalHistory, ResizeAllocatesNewSlotsAndCollectsTheOldOnes)
{
    Frame f;
    f.run(640, 360);
    f.run(640, 360);
    const HistoryTargets resized = f.run(800, 600);
    ASSERT_TRUE(resized.valid());
    EXPECT_FALSE(resized.prevValid);
    EXPECT_EQ(f.backend.lastExtent, (TargetExtent{ 800, 600 }));
    EXPECT_EQ(f.backend.creates, 4);
    EXPECT_EQ(f.backend.retires, 2); // the 640x360 pair, in the same frame's collect
    EXPECT_EQ(f.pool.count(), 2u);
    EXPECT_EQ(f.set.count(), 2u);
    EXPECT_TRUE(f.run(800, 600).prevValid);
}

TEST(TemporalHistory, FormatChangeIsANewHistory)
{
    Frame f;
    f.run(640, 360);
    f.run(640, 360);
    const HistoryTargets other = f.run(640, 360, true, true, kR11G11B10);
    EXPECT_FALSE(other.prevValid);
    EXPECT_EQ(f.pool.count(), 2u);
    EXPECT_EQ(f.backend.retires, 2);
}

TEST(TemporalHistory, NotRequestedFreesTheMemoryAndRestartsWithoutPrev)
{
    Frame f;
    f.run(640, 360);
    f.run(640, 360);
    f.run(640, 360, false); // feature off this frame: entry still alive until the next beginFrame
    f.run(640, 360, false); // now dropped by the graph and collected
    EXPECT_EQ(f.pool.count(), 0u);
    EXPECT_EQ(f.set.count(), 0u);
    EXPECT_EQ(f.backend.retires, 2);
    const HistoryTargets back = f.run(640, 360);
    ASSERT_TRUE(back.valid());
    EXPECT_FALSE(back.prevValid);
}

TEST(TemporalHistory, InvalidateAndSkippedWritesDropPrevButKeepMemory)
{
    Frame f;
    f.run(640, 360);
    f.run(640, 360);
    const HistoryTargets cut = f.run(640, 360, true, true, kRgba16F, true);
    EXPECT_FALSE(cut.prevValid);
    EXPECT_TRUE(f.run(640, 360).prevValid);

    f.run(640, 360, true, false); // the pass skipped its write
    EXPECT_FALSE(f.run(640, 360).prevValid);
    EXPECT_TRUE(f.run(640, 360).prevValid);
    EXPECT_EQ(f.backend.creates, 2);
    EXPECT_EQ(f.backend.retires, 0);
}

TEST(TemporalHistory, MinimizedOutputRequestsNothing)
{
    CountingBackend  backend;
    RenderTargetPool pool(backend);
    RenderGraph      graph;
    HistoryTargetSet set;
    graph.beginFrame(640, 360);
    const HistoryTargets h = set.request(graph, pool, "Taa", historyDesc()); // pool has no output size
    EXPECT_FALSE(h.valid());
    EXPECT_FALSE(h.pair.curr.valid());
    EXPECT_EQ(backend.creates, 0);
    EXPECT_FALSE(set.request(graph, pool, nullptr, historyDesc()).valid());
}

TEST(TemporalHistory, ClearUnregistersEverything)
{
    Frame f;
    f.run(640, 360);
    f.set.clear(f.pool);
    EXPECT_EQ(f.set.count(), 0u);
    EXPECT_EQ(f.pool.count(), 0u);
    EXPECT_EQ(f.backend.retires, 2);
}
