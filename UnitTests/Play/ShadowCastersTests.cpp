#include <gtest/gtest.h>

#include "Play/ShadowCasters.h"
#include "Render/ShadowFeature.h"

#include <vector>

// GameplayShadowCasters on a host without a world, terrain or foliage: what it reports to
// ShadowFeature, that it hands the host's primitive callback the view it was given, and the render
// half shadowFrameOf takes from the host.

using namespace Dark;

namespace
{
    struct PrimCall
    {
        ID3D12GraphicsCommandList* cmd       = nullptr;
        bool                       instanced = false;
        bool                       capturing = false;
    };

    void recordPrims(void* user, ID3D12GraphicsCommandList* cmd, const Math::Matrix4f&, const Frustum3f&, bool recordThisView)
    {
        static_cast<std::vector<PrimCall>*>(user)->push_back({ cmd, recordThisView, capturingShadowCasters() });
    }

    ID3D12GraphicsCommandList* fakeCmd()
    {
        return reinterpret_cast<ID3D12GraphicsCommandList*>(static_cast<uintptr_t>(0x1000));
    }
} // namespace

TEST(ShadowCasters, AnEmptyHostIsReadyAndKeepsTheGroundOffTheWorker)
{
    ShadowCasterHost host;
    host.drawTerrain = true; // no terrain: the ground cannot draw on the worker
    GameplayShadowCasters casters(host);
    EXPECT_TRUE(casters.workerReady());
    EXPECT_FALSE(casters.groundOnWorker());
    EXPECT_EQ(casters.retainFoliage(Frustum3f()), IShadowCasterSource::kNoRetainedFoliage);
    casters.prepareDirectional();
    casters.beginWorkerCapture();
    casters.captureGround(Frustum3f());
    casters.drawGround(fakeCmd(), Frustum3f());
}

TEST(ShadowCasters, WorkerDrawsWithoutTerrainOrFoliageDrawNothing)
{
    ShadowCasterHost        host;
    GameplayShadowCasters   casters(host);
    const ShadowWorkerDraws draws = casters.workerDraws();
    ASSERT_NE(draws.ground, nullptr);
    ASSERT_NE(draws.foliage, nullptr);
    ASSERT_NE(draws.user, nullptr);
    draws.ground(draws.user, fakeCmd(), Frustum3f());
    draws.foliage(draws.user, fakeCmd(), Math::Matrix4f::IDENTITY, 0);
}

TEST(ShadowCasters, PrimitivesGetTheListAndWhetherTheInstancedListDrewTheView)
{
    std::vector<PrimCall> calls;
    ShadowCasterHost      host;
    host.drawPrimitives = &recordPrims;
    host.user           = &calls;
    GameplayShadowCasters casters(host);

    const Math::Matrix4f viewProj = Math::Matrix4f::IDENTITY;
    const Frustum3f      frustum(viewProj);
    ShadowCasterView     view;
    view.viewProj  = &viewProj;
    view.frustum   = &frustum;
    view.instanced = true;
    casters.drawCasters(fakeCmd(), view);
    view.instanced = false;
    casters.captureCasters(view);

    ASSERT_EQ(calls.size(), 2u);
    EXPECT_EQ(calls[0].cmd, fakeCmd());
    EXPECT_TRUE(calls[0].instanced);
    // Captured for the worker: no list. Outside a kicked frame nothing is capturing.
    EXPECT_EQ(calls[1].cmd, nullptr);
    EXPECT_FALSE(calls[1].instanced);
    EXPECT_FALSE(calls[1].capturing);
}

TEST(ShadowCasters, ShadowFrameOfTakesTheRenderHalfOfTheHost)
{
    ShadowCasterHost host;
    host.renderer        = reinterpret_cast<Renderer*>(static_cast<uintptr_t>(0x10));
    host.jobs            = reinterpret_cast<JobSystem*>(static_cast<uintptr_t>(0x20));
    host.shadows         = reinterpret_cast<ShadowSystem*>(static_cast<uintptr_t>(0x30));
    host.drawList        = reinterpret_cast<DrawList*>(static_cast<uintptr_t>(0x40));
    host.skinned         = reinterpret_cast<SkinnedInstanceBuffer*>(static_cast<uintptr_t>(0x50));
    host.skinnedShadow   = reinterpret_cast<const SkinnedMeshPipeline*>(static_cast<uintptr_t>(0x60));
    host.instanceStatic  = true;
    host.instanceSkinned = true;
    GameplayShadowCasters casters(host);
    auto*                 local = reinterpret_cast<LocalShadowSystem*>(static_cast<uintptr_t>(0x70));

    const ShadowFrame frame = shadowFrameOf(host, local, &casters);
    EXPECT_EQ(frame.renderer, host.renderer);
    EXPECT_EQ(frame.jobs, host.jobs);
    EXPECT_EQ(frame.shadows, host.shadows);
    EXPECT_EQ(frame.localShadows, local);
    EXPECT_EQ(frame.drawList, host.drawList);
    EXPECT_EQ(frame.skinned, host.skinned);
    EXPECT_EQ(frame.skinnedShadow, host.skinnedShadow);
    EXPECT_TRUE(frame.instanceStatic);
    EXPECT_TRUE(frame.instanceSkinned);
    EXPECT_EQ(frame.casters, &casters);
}
