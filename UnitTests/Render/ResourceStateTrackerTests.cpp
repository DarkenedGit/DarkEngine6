#include "Render/RenderGraphBarriers.h"
#include "Render/ResourceStateTracker.h"

#include <gtest/gtest.h>

#include <cstdint>

// Phase 6 (plan §3.7): the legacy-state translation every converted ResourceBarrier site goes
// through, and the record-order state table that gives opaque owners and the graph one before-state.

using namespace Dark;
using namespace Dark::Rg;

namespace
{
    // D3D12_RESOURCE_STATES values (d3d12.h), so the test needs no Windows headers.
    constexpr uint32_t kCommon = 0, kVbCb = 0x1, kIndex = 0x2, kRt = 0x4, kUav = 0x8, kDepthWrite = 0x10, kDepthRead = 0x20, kNonPixelSrv = 0x40, kPixelSrv = 0x80,
                       kIndirect = 0x200, kCopyDest = 0x400, kCopySource = 0x800, kGenericRead = 0xAC3, kStreamOut = 0x100, kResolveDest = 0x1000;

    const void* key(uintptr_t v)
    {
        return reinterpret_cast<const void*>(v);
    }

    UseState legacy(uint32_t states, ResourceKind kind)
    {
        UseState s;
        EXPECT_TRUE(legacyStateEquivalent(states, kind, s)) << "states 0x" << std::hex << states;
        return s;
    }
} // namespace

TEST(LegacyStateEquivalent, TextureStatesMapToTheSpecLayouts)
{
    constexpr ResourceKind T = ResourceKind::Texture;
    EXPECT_EQ(legacy(kRt, T), (UseState{ Sync::RenderTarget, Access::RenderTarget, Layout::RenderTarget }));
    EXPECT_EQ(legacy(kUav, T), (UseState{ Sync::AllShading, Access::UnorderedAccess, Layout::UnorderedAccess }));
    EXPECT_EQ(legacy(kDepthWrite, T), (UseState{ Sync::DepthStencil, Access::DepthStencilWrite, Layout::DepthStencilWrite }));
    EXPECT_EQ(legacy(kDepthRead, T), (UseState{ Sync::DepthStencil, Access::DepthStencilRead, Layout::DepthStencilRead }));
    EXPECT_EQ(legacy(kPixelSrv, T), (UseState{ Sync::PixelShading, Access::ShaderResource, Layout::ShaderResource }));
    EXPECT_EQ(legacy(kNonPixelSrv, T), (UseState{ Sync::NonPixelShading, Access::ShaderResource, Layout::ShaderResource }));
    EXPECT_EQ(legacy(kPixelSrv | kNonPixelSrv, T), (UseState{ Sync::PixelShading | Sync::NonPixelShading, Access::ShaderResource, Layout::ShaderResource }));
    EXPECT_EQ(legacy(kCopyDest, T), (UseState{ Sync::Copy, Access::CopyDest, Layout::CopyDest }));
    EXPECT_EQ(legacy(kCopySource, T), (UseState{ Sync::Copy, Access::CopySource, Layout::CopySource }));
    EXPECT_EQ(legacy(kCommon, T), (UseState{ Sync::All, Access::Common, Layout::Common }));
}

TEST(LegacyStateEquivalent, MixedReadsOnATextureUseGenericReadAndDropBufferBits)
{
    const UseState s = legacy(kGenericRead, ResourceKind::Texture);
    EXPECT_EQ(s.layout, Layout::GenericRead);
    EXPECT_EQ(s.access, Access::ShaderResource | Access::CopySource);
    EXPECT_TRUE(any(s.sync & Sync::ExecuteIndirect)); // the sync scope still covers what the state named
    EXPECT_EQ(legacy(kPixelSrv | kCopySource, ResourceKind::Texture).layout, Layout::GenericRead);
}

TEST(LegacyStateEquivalent, ReadOnlyDepthThatIsAlsoSampledStaysInDepthStencilRead)
{
    // Renderer::bindSceneColor and the water/particle passes: depth test against a read-only DSV
    // while shaders sample it.
    const UseState s = legacy(kDepthRead | kPixelSrv, ResourceKind::Texture);
    EXPECT_EQ(s.layout, Layout::DepthStencilRead);
    EXPECT_EQ(s.access, Access::DepthStencilRead | Access::ShaderResource);
    const UseState all = legacy(kDepthRead | kPixelSrv | kNonPixelSrv, ResourceKind::Texture);
    EXPECT_EQ(all.layout, Layout::DepthStencilRead);
    EXPECT_EQ(all.sync, Sync::DepthStencil | Sync::PixelShading | Sync::NonPixelShading);
}

TEST(LegacyStateEquivalent, BufferStatesHaveNoLayout)
{
    constexpr ResourceKind B = ResourceKind::Buffer;
    EXPECT_EQ(legacy(kVbCb | kIndex, B), (UseState{ Sync::AllShading | Sync::IndexInput, Access::VertexBuffer | Access::ConstantBuffer | Access::IndexBuffer, Layout::Undefined }));
    EXPECT_EQ(legacy(kIndirect, B), (UseState{ Sync::ExecuteIndirect, Access::IndirectArgument, Layout::Undefined }));
    EXPECT_EQ(legacy(kUav, B), (UseState{ Sync::AllShading, Access::UnorderedAccess, Layout::Undefined }));
    EXPECT_EQ(legacy(kCopyDest, B), (UseState{ Sync::Copy, Access::CopyDest, Layout::Undefined }));
    EXPECT_EQ(legacy(kCommon, B), (UseState{ Sync::All, Access::Common, Layout::Undefined }));
}

TEST(LegacyStateEquivalent, RejectsStatesWithNoSingleEquivalent)
{
    UseState s;
    EXPECT_FALSE(legacyStateEquivalent(kRt | kUav, ResourceKind::Texture, s));        // two writes
    EXPECT_FALSE(legacyStateEquivalent(kDepthWrite | kPixelSrv, ResourceKind::Texture, s)); // write + read
    EXPECT_FALSE(legacyStateEquivalent(kStreamOut, ResourceKind::Buffer, s));          // unused by the engine
    EXPECT_FALSE(legacyStateEquivalent(kResolveDest, ResourceKind::Texture, s));
    EXPECT_FALSE(legacyStateEquivalent(kIndex, ResourceKind::Texture, s));             // buffer-only on a texture
    EXPECT_FALSE(legacyStateEquivalent(kRt, ResourceKind::Buffer, s));                 // texture-only on a buffer
    EXPECT_FALSE(legacyStateEquivalent(kDepthRead | kCopySource, ResourceKind::Texture, s));
}

TEST(ResourceStateTracker, TracksForgetsAndReportsCurrentState)
{
    ResourceStateTracker t;
    const UseState       rt{ Sync::RenderTarget, Access::RenderTarget, Layout::RenderTarget };
    EXPECT_FALSE(t.tracked(key(0x10)));
    EXPECT_EQ(t.current(key(0x10)), nullptr);
    t.track(key(0x10), ResourceKind::Texture, rt);
    ASSERT_TRUE(t.tracked(key(0x10)));
    EXPECT_EQ(*t.current(key(0x10)), rt);
    EXPECT_EQ(t.kind(key(0x10)), ResourceKind::Texture);
    EXPECT_EQ(t.count(), 1u);
    t.forget(key(0x10));
    EXPECT_FALSE(t.tracked(key(0x10)));
    EXPECT_EQ(t.count(), 0u);
}

TEST(ResourceStateTracker, LayoutChangeNeedsABarrierAndUpdatesTheState)
{
    ResourceStateTracker t;
    const UseState       rt{ Sync::RenderTarget, Access::RenderTarget, Layout::RenderTarget };
    const UseState       sr{ Sync::PixelShading, Access::ShaderResource, Layout::ShaderResource };
    t.track(key(0x10), ResourceKind::Texture, rt);
    UseState before;
    ASSERT_TRUE(t.transition(key(0x10), sr, before));
    EXPECT_EQ(before, rt);
    EXPECT_EQ(*t.current(key(0x10)), sr);
    // Back again: before is what the last transition left.
    ASSERT_TRUE(t.transition(key(0x10), rt, before));
    EXPECT_EQ(before, sr);
}

TEST(ResourceStateTracker, ReadAfterReadInOneLayoutIsFreeAndWidensTheState)
{
    ResourceStateTracker t;
    const UseState       ps{ Sync::PixelShading, Access::ShaderResource, Layout::ShaderResource };
    const UseState       cs{ Sync::NonPixelShading, Access::ShaderResource, Layout::ShaderResource };
    const UseState       rt{ Sync::RenderTarget, Access::RenderTarget, Layout::RenderTarget };
    t.track(key(0x10), ResourceKind::Texture, ps);
    UseState before{};
    EXPECT_FALSE(t.transition(key(0x10), cs, before));
    EXPECT_EQ(t.current(key(0x10))->sync, Sync::PixelShading | Sync::NonPixelShading);
    // The next write waits for both readers.
    ASSERT_TRUE(t.transition(key(0x10), rt, before));
    EXPECT_EQ(before.sync, Sync::PixelShading | Sync::NonPixelShading);
}

TEST(ResourceStateTracker, SameWriteStateTwiceNeedsNoTransitionButAWriteAfterAReadDoes)
{
    const UseState uav{ Sync::AllShading, Access::UnorderedAccess, Layout::Undefined };
    const UseState cb{ Sync::AllShading, Access::ConstantBuffer, Layout::Undefined };
    EXPECT_FALSE(ResourceStateTracker::needsBarrier(uav, uav, ResourceKind::Buffer));
    EXPECT_TRUE(ResourceStateTracker::needsBarrier(cb, uav, ResourceKind::Buffer));
    EXPECT_TRUE(ResourceStateTracker::needsBarrier(uav, cb, ResourceKind::Buffer));
    EXPECT_FALSE(ResourceStateTracker::needsBarrier(cb, cb, ResourceKind::Buffer));
}

TEST(ResourceStateTracker, NoAccessOnEitherSideAlwaysBarriers)
{
    const UseState sr{ Sync::PixelShading, Access::ShaderResource, Layout::ShaderResource };
    const UseState off{ Sync::None, Access::NoAccess, Layout::ShaderResource };
    EXPECT_TRUE(ResourceStateTracker::needsBarrier(sr, off, ResourceKind::Texture));
    EXPECT_TRUE(ResourceStateTracker::needsBarrier(off, sr, ResourceKind::Texture));
    EXPECT_FALSE(ResourceStateTracker::needsBarrier(off, off, ResourceKind::Texture));
}

TEST(ResourceStateTracker, BuffersIgnoreLayouts)
{
    ResourceStateTracker t;
    t.track(key(0x20), ResourceKind::Buffer, { Sync::Copy, Access::CopyDest, Layout::CopyDest });
    EXPECT_EQ(t.current(key(0x20))->layout, Layout::Undefined);
    UseState before;
    ASSERT_TRUE(t.transition(key(0x20), { Sync::AllShading, Access::VertexBuffer, Layout::ShaderResource }, before));
    EXPECT_EQ(before.access, Access::CopyDest);
    EXPECT_EQ(t.current(key(0x20))->layout, Layout::Undefined);
}

TEST(ResourceStateTracker, UntrackedTransitionLeavesBeforeAlone)
{
    ResourceStateTracker t;
    UseState             before{ Sync::Copy, Access::CopySource, Layout::CopySource };
    EXPECT_FALSE(t.transition(key(0x30), { Sync::All, Access::Common, Layout::Common }, before));
    EXPECT_EQ(before.access, Access::CopySource);
    t.set(key(0x30), { Sync::All, Access::Common, Layout::Common }); // no-op when untracked
    EXPECT_FALSE(t.tracked(key(0x30)));
}

TEST(ResourceStateTracker, SetOverwritesWithoutABarrierDecision)
{
    ResourceStateTracker t;
    const UseState       rt{ Sync::RenderTarget, Access::RenderTarget, Layout::RenderTarget };
    const UseState       present{ Sync::All, Access::Common, Layout::Common };
    t.track(key(0x40), ResourceKind::Texture, present);
    t.set(key(0x40), rt);
    EXPECT_EQ(*t.current(key(0x40)), rt);
}
