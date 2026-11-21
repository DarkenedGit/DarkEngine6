#include "RenderGraphTestUtil.h"

#include <cstdint>
#include <string>
#include <vector>

// Phase 4 executor contract: PassContext::cmd() and real imports, OwnBarriers (opaque) passes get no
// barrier records, and SplitExecutor splits a queue's command list at every cross-queue wait/signal.

using namespace Dark::Rg;
using namespace Dark::Rg::Test;

namespace
{
    // Stands in for a command list / resource pointer; only identity is compared.
    template <class T> T* fakePtr(uintptr_t tag)
    {
        return reinterpret_cast<T*>(tag);
    }

    class ListExecutor : public Executor
    {
    public:
        ID3D12GraphicsCommandList* commandList(Queue q) const override
        {
            return fakePtr<ID3D12GraphicsCommandList>(q == Queue::Graphics ? 0x1000 : 0x2000);
        }
    };

    // Records the queue operations a SplitExecutor asks for, in order.
    class SplitLog : public SplitExecutor
    {
    public:
        std::vector<std::string> ops;
        uint32_t                 list[kQueueCount] = { 0, 0 };          // index of the open list per queue
        std::vector<std::string> passList;                              // "<pass>@<queue><list>"
        bool                     dirty[kQueueCount] = { false, false }; // recorded work not yet submitted
        std::vector<std::string> violations;                            // signal/wait issued over unsubmitted work

    protected:
        void passBegin(const PassContext& ctx) override
        {
            const uint32_t q = static_cast<uint32_t>(ctx.queue());
            ops.push_back(std::string("pass ") + ctx.passName());
            dirty[q] = true;
            passList.push_back(std::string(ctx.passName()) + "@" + queueName(ctx.queue()) + std::to_string(list[q]));
        }
        void recordBarriers(const BarrierBatch& batch, const BarrierRec*) override
        {
            ops.push_back(std::string("barriers ") + queueName(batch.queue));
            dirty[static_cast<uint32_t>(batch.queue)] = true;
        }
        bool submitList(Queue q) override
        {
            ops.push_back(std::string("submit ") + queueName(q));
            ++list[static_cast<uint32_t>(q)];
            dirty[static_cast<uint32_t>(q)] = false;
            return true;
        }
        void queueSignal(Queue q, uint32_t value) override
        {
            ops.push_back(std::string("signal ") + queueName(q) + " " + std::to_string(value));
            if (dirty[static_cast<uint32_t>(q)])
                violations.push_back(ops.back());
        }
        void queueWait(Queue q, Queue signalQueue, uint32_t value) override
        {
            ops.push_back(std::string("wait ") + queueName(q) + " on " + queueName(signalQueue) + " " + std::to_string(value));
            if (dirty[static_cast<uint32_t>(q)])
                violations.push_back(ops.back());
        }
    };

    // Depth (graphics) -> SSAO (compute) -> Light (graphics) -> back buffer.
    void buildSsao(RenderGraph& g, bool asyncOn = true)
    {
        CompileOptions o;
        o.asyncCompute = asyncOn;
        g.setOptions(o);
        g.beginFrame(640, 360);
        const TextureHandle bb = g.importBackBuffer(nullptr, 640, 360, kBgra8);
        TextureHandle       d, ao;
        pass(g, "Depth", [&](PassBuilder& b) { d = b.write(b.create("D", tex(kD32)), Use::DepthWrite); });
        pass(g, "SSAO",
             [&](PassBuilder& b)
             {
                 b.queue(Queue::AsyncCompute);
                 b.read(d, Use::ComputeSrv);
                 ao = b.write(b.create("AO", tex(kR8, 0.5f)), Use::Uav);
             });
        pass(g, "Light",
             [&](PassBuilder& b)
             {
                 b.read(ao, Use::PixelSrv);
                 b.write(bb);
             });
    }

    size_t indexOf(const std::vector<std::string>& v, const std::string& s)
    {
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i] == s)
                return i;
        return v.size();
    }
} // namespace

TEST(RenderGraphExecutor, CmdIsTheExecutorsListForThePassQueue)
{
    RenderGraph g;
    g.beginFrame(64, 64);
    const TextureHandle        bb   = g.importBackBuffer(fakePtr<ID3D12Resource>(0xB0), 64, 64, kBgra8);
    ID3D12GraphicsCommandList* seen = nullptr;
    ID3D12GraphicsCommandList* none = fakePtr<ID3D12GraphicsCommandList>(1);
    ID3D12Resource*            res  = nullptr;
    struct Data
    {
        TextureHandle out;
    };
    g.addPass<Data>(
        "Draw", [&](PassBuilder& b, Data& d) { d.out = b.write(bb); },
        [&](PassContext& ctx, const Data& d)
        {
            seen = ctx.cmd();
            res  = ctx.resource(d.out);
        });
    ASSERT_TRUE(compiles(g));
    ListExecutor exec;
    ASSERT_TRUE(g.execute(&exec));
    EXPECT_EQ(seen, fakePtr<ID3D12GraphicsCommandList>(0x1000));
    EXPECT_EQ(res, fakePtr<ID3D12Resource>(0xB0)); // the import's own resource, every version

    // Without an executor there is no list.
    g.beginFrame(64, 64);
    const TextureHandle bb2 = g.importBackBuffer(nullptr, 64, 64, kBgra8);
    g.addPass<Data>("Draw", [&](PassBuilder& b, Data& d) { d.out = b.write(bb2); }, [&](PassContext& ctx, const Data&) { none = ctx.cmd(); });
    ASSERT_TRUE(compiles(g));
    ASSERT_TRUE(g.execute());
    EXPECT_EQ(none, nullptr);
}

TEST(RenderGraphExecutor, OpaquePassesGetNoBarrierRecords)
{
    RenderGraph g;
    g.beginFrame(256, 256);
    const TextureHandle bb     = g.importBackBuffer(nullptr, 256, 256, kBgra8);
    TextureHandle       hdr    = g.importTexture("Hdr", absTex(256, 256), fakePtr<ID3D12Resource>(0x10), Layout::Undefined, Layout::Undefined);
    TextureHandle       depth  = g.importTexture("Depth", absTex(256, 256, kD32), fakePtr<ID3D12Resource>(0x20), Layout::Undefined, Layout::Undefined);
    const PassFlags     opaque = PassFlags::OwnBarriers | PassFlags::SideEffect;
    pass(g, "GBuffer",
         [&](PassBuilder& b)
         {
             b.flags(opaque);
             depth = b.write(depth, Use::DepthWrite);
         });
    pass(g, "Lighting",
         [&](PassBuilder& b)
         {
             b.flags(opaque);
             b.read(depth, Use::DepthRead);
             hdr = b.write(hdr);
         });
    pass(g, "Post",
         [&](PassBuilder& b)
         {
             b.flags(opaque);
             b.read(hdr);
             b.write(bb);
         });
    ASSERT_TRUE(compiles(g));
    EXPECT_TRUE(g.barriers().empty());
    EXPECT_TRUE(g.barrierBatches().empty());
    ASSERT_EQ(g.order().size(), 3u);
    EXPECT_EQ(g.passName(g.order()[0]), std::string("GBuffer"));
    EXPECT_EQ(g.passName(g.order()[2]), std::string("Post"));
}

TEST(RenderGraphExecutor, OpaqueAndDeclaredUseOfOneResourceIsAnError)
{
    RenderGraph g;
    g.beginFrame(64, 64);
    const TextureHandle bb  = g.importBackBuffer(nullptr, 64, 64, kBgra8);
    TextureHandle       hdr = g.importTexture("Hdr", absTex(64, 64), nullptr, Layout::RenderTarget, Layout::RenderTarget);
    pass(g, "Opaque",
         [&](PassBuilder& b)
         {
             b.flags(PassFlags::OwnBarriers | PassFlags::SideEffect);
             hdr = b.write(hdr);
         });
    pass(g, "Declared",
         [&](PassBuilder& b)
         {
             b.read(hdr);
             b.write(bb);
         });
    EXPECT_FALSE(g.compile());
    EXPECT_TRUE(g.hasError(ErrorCode::MixedOpaqueUse)) << errorText(g);
    EXPECT_STREQ(errorCodeName(ErrorCode::MixedOpaqueUse), "MixedOpaqueUse");
}

TEST(RenderGraphExecutor, SplitExecutorSplitsListsAtEveryCrossQueueWaitAndSignal)
{
    RenderGraph g;
    buildSsao(g);
    ASSERT_TRUE(compiles(g));
    ASSERT_FALSE(g.syncPoints().empty());
    SplitLog exec;
    exec.resetSplits();
    ASSERT_TRUE(g.execute(&exec));
    ASSERT_TRUE(exec.finish());

    // Every signal and wait comes after its queue's recorded work was submitted.
    EXPECT_TRUE(exec.violations.empty()) << exec.violations.front();
    // Depth and Light are on different graphics lists; SSAO is alone on the first compute list.
    EXPECT_NE(indexOf(exec.passList, "Depth@Graphics0"), exec.passList.size());
    EXPECT_NE(indexOf(exec.passList, "Light@Graphics1"), exec.passList.size());
    EXPECT_NE(indexOf(exec.passList, "SSAO@AsyncCompute0"), exec.passList.size());
    EXPECT_EQ(exec.listsSubmitted(Queue::Graphics), 2u);
    EXPECT_EQ(exec.listsSubmitted(Queue::AsyncCompute), 1u);
    EXPECT_EQ(exec.ops.back(), "submit Graphics"); // finish() submits the last open list
}

TEST(RenderGraphExecutor, SplitExecutorKeepsOneListWithoutSyncPoints)
{
    RenderGraph g;
    buildSsao(g, false); // forced onto graphics: no sync points
    ASSERT_TRUE(compiles(g));
    ASSERT_TRUE(g.syncPoints().empty());
    SplitLog exec;
    exec.resetSplits();
    ASSERT_TRUE(g.execute(&exec));
    ASSERT_TRUE(exec.finish());
    EXPECT_EQ(exec.listsSubmitted(Queue::Graphics), 1u);
    EXPECT_EQ(exec.listsSubmitted(Queue::AsyncCompute), 0u);
    for (const std::string& p : exec.passList)
        EXPECT_EQ(p.substr(p.find('@')), "@Graphics0") << p;
}
