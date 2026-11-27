#include "Render/EnhancedBarrierEmitter.h"
#include "Render/RenderGraphTypes.h"
#include "Render/ResourceStateTracker.h"
#include "Render/ShadowInputs.h"

#include <gtest/gtest.h>

#include <cstdint>

// Phase 6 (plan §3.7, D13): the enhanced barrier structs every barrier in the engine now goes
// through, without a device. Resources are fake pointers: every call here passes the kind, so no
// method is ever called on them, and the lists are never recorded (no command list).

using namespace Dark;
using namespace Dark::Rg;

namespace
{
    ID3D12Resource* fakeRes(uintptr_t v)
    {
        return reinterpret_cast<ID3D12Resource*>(v);
    }

    // Installs a tracker for the test (this thread) and removes it again.
    struct ScopedTracker
    {
        ResourceStateTracker tracker;
        ScopedTracker()
        {
            setFrameStateTracker(&tracker);
        }
        ~ScopedTracker()
        {
            setFrameStateTracker(nullptr);
        }
    };

    // A list that is emptied, not recorded, when the test is done with it.
    struct TestList : EnhancedBarrierList
    {
        ~TestList()
        {
            discard();
        }
    };
} // namespace

TEST(EnhancedBarrierEmitter, TextureBarrierCopiesTheStatesAndAddressesAllSubresources)
{
    const UseState              rt{ Sync::RenderTarget, Access::RenderTarget, Layout::RenderTarget };
    const UseState              sr{ Sync::PixelShading, Access::ShaderResource, Layout::ShaderResource };
    const D3D12_TEXTURE_BARRIER b = makeTextureBarrier(fakeRes(0x10), rt, sr);
    EXPECT_EQ(b.SyncBefore, D3D12_BARRIER_SYNC_RENDER_TARGET);
    EXPECT_EQ(b.SyncAfter, D3D12_BARRIER_SYNC_PIXEL_SHADING);
    EXPECT_EQ(b.AccessBefore, D3D12_BARRIER_ACCESS_RENDER_TARGET);
    EXPECT_EQ(b.AccessAfter, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
    EXPECT_EQ(b.LayoutBefore, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    EXPECT_EQ(b.LayoutAfter, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
    EXPECT_EQ(b.pResource, fakeRes(0x10));
    EXPECT_EQ(b.Subresources.IndexOrFirstMipLevel, 0xFFFFFFFFu);
    EXPECT_EQ(b.Subresources.NumMipLevels, 0u);
    EXPECT_EQ(b.Flags, D3D12_TEXTURE_BARRIER_FLAG_NONE);
}

TEST(EnhancedBarrierEmitter, OneSubresourceAndDiscard)
{
    const UseState              none{ Sync::None, Access::NoAccess, Layout::Undefined };
    const UseState              rt{ Sync::RenderTarget, Access::RenderTarget, Layout::RenderTarget };
    const D3D12_TEXTURE_BARRIER b = makeTextureBarrier(fakeRes(0x10), none, rt, true, 5);
    EXPECT_EQ(b.Subresources.IndexOrFirstMipLevel, 5u);
    EXPECT_EQ(b.Subresources.NumMipLevels, 0u); // 0: IndexOrFirstMipLevel is a subresource index
    EXPECT_EQ(b.Flags, D3D12_TEXTURE_BARRIER_FLAG_DISCARD);
    EXPECT_EQ(b.LayoutBefore, D3D12_BARRIER_LAYOUT_UNDEFINED);
    EXPECT_EQ(b.AccessBefore, D3D12_BARRIER_ACCESS_NO_ACCESS);
}

TEST(EnhancedBarrierEmitter, BufferBarrierCoversTheWholeBuffer)
{
    const UseState             uav{ Sync::ComputeShading, Access::UnorderedAccess, Layout::Undefined };
    const UseState             ind{ Sync::ExecuteIndirect, Access::IndirectArgument, Layout::Undefined };
    const D3D12_BUFFER_BARRIER b = makeBufferBarrier(fakeRes(0x20), uav, ind);
    EXPECT_EQ(b.Offset, 0u);
    EXPECT_EQ(b.Size, UINT64_MAX);
    EXPECT_EQ(b.SyncBefore, D3D12_BARRIER_SYNC_COMPUTE_SHADING);
    EXPECT_EQ(b.AccessAfter, D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT);
}

TEST(EnhancedBarrierEmitter, UntrackedLegacyTransitionTranslatesBothStates)
{
    TestList list;
    ASSERT_TRUE(list.transition(fakeRes(0x10), ResourceKind::Texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
    ASSERT_EQ(list.textureCount(), 1u);
    const D3D12_TEXTURE_BARRIER& b = list.textureBarrier(0);
    EXPECT_EQ(b.LayoutBefore, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
    EXPECT_EQ(b.LayoutAfter, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    EXPECT_EQ(b.SyncBefore, D3D12_BARRIER_SYNC_PIXEL_SHADING);
    EXPECT_EQ(b.SyncAfter, D3D12_BARRIER_SYNC_RENDER_TARGET);

    // Same state: nothing, like the legacy helpers.
    ASSERT_TRUE(list.transition(fakeRes(0x10), ResourceKind::Texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET));
    EXPECT_EQ(list.count(), 1u);

    // Buffers go to the buffer group.
    ASSERT_TRUE(list.transition(fakeRes(0x20), ResourceKind::Buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
    ASSERT_EQ(list.bufferCount(), 1u);
    EXPECT_EQ(list.bufferBarrier(0).AccessBefore, D3D12_BARRIER_ACCESS_COPY_DEST);
    EXPECT_EQ(list.bufferBarrier(0).AccessAfter, D3D12_BARRIER_ACCESS_VERTEX_BUFFER | D3D12_BARRIER_ACCESS_CONSTANT_BUFFER);

    // A state with no enhanced equivalent records nothing.
    EXPECT_FALSE(list.transition(fakeRes(0x30), ResourceKind::Texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_STREAM_OUT));
    EXPECT_EQ(list.count(), 2u);
}

TEST(EnhancedBarrierEmitter, PerSliceTransitionKeepsTheSubresourceIndex)
{
    TestList list;
    for (uint32_t slice = 0; slice < 3; ++slice)
        ASSERT_TRUE(list.transition(fakeRes(0x40), ResourceKind::Texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE, slice + 4));
    ASSERT_EQ(list.textureCount(), 3u);
    EXPECT_EQ(list.textureBarrier(2).Subresources.IndexOrFirstMipLevel, 6u);
    EXPECT_EQ(list.textureBarrier(2).LayoutAfter, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
}

TEST(EnhancedBarrierEmitter, TrackedResourceTakesItsBeforeStateFromTheTracker)
{
    ScopedTracker scope;
    scope.tracker.track(fakeRes(0x10), ResourceKind::Texture, { Sync::All, Access::Common, Layout::Common });
    TestList list;
    // The caller still believes PIXEL_SHADER_RESOURCE; the tracker knows better.
    ASSERT_TRUE(list.transition(fakeRes(0x10), ResourceKind::Texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
    ASSERT_EQ(list.textureCount(), 1u);
    EXPECT_EQ(list.textureBarrier(0).LayoutBefore, D3D12_BARRIER_LAYOUT_COMMON);
    EXPECT_EQ(list.textureBarrier(0).LayoutAfter, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    EXPECT_EQ(scope.tracker.current(fakeRes(0x10))->layout, Layout::RenderTarget);

    // Already there: no barrier, whatever the caller passes as before.
    ASSERT_TRUE(list.transition(fakeRes(0x10), ResourceKind::Texture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));
    EXPECT_EQ(list.textureCount(), 1u);

    // transitionTo needs no before at all.
    ASSERT_TRUE(list.transitionTo(fakeRes(0x10), UseState{ Sync::PixelShading, Access::ShaderResource, Layout::ShaderResource }));
    ASSERT_EQ(list.textureCount(), 2u);
    EXPECT_EQ(list.textureBarrier(1).LayoutBefore, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
}

TEST(EnhancedBarrierEmitter, PerSubresourceBarrierOnATrackedResourceStaysUntracked)
{
    ScopedTracker scope;
    scope.tracker.track(fakeRes(0x10), ResourceKind::Texture, { Sync::RenderTarget, Access::RenderTarget, Layout::RenderTarget });
    TestList list;
    ASSERT_TRUE(list.transition(fakeRes(0x10), ResourceKind::Texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST, 2));
    ASSERT_EQ(list.textureCount(), 1u);
    EXPECT_EQ(list.textureBarrier(0).LayoutBefore, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE); // the caller's
    EXPECT_EQ(scope.tracker.current(fakeRes(0x10))->layout, Layout::RenderTarget);         // untouched
}

TEST(EnhancedBarrierEmitter, TransitionToAnUntrackedResourceRecordsNothing)
{
    ScopedTracker scope;
    TestList      list;
    EXPECT_FALSE(list.transitionTo(fakeRes(0x50), UseState{ Sync::RenderTarget, Access::RenderTarget, Layout::RenderTarget }));
    EXPECT_EQ(list.count(), 0u);
}

TEST(EnhancedBarrierEmitter, UavBarrierKeepsTheUnorderedAccessState)
{
    TestList list;
    list.uav(fakeRes(0x10), ResourceKind::Texture);
    list.uav(fakeRes(0x20), ResourceKind::Buffer);
    ASSERT_EQ(list.textureCount(), 1u);
    ASSERT_EQ(list.bufferCount(), 1u);
    const D3D12_TEXTURE_BARRIER& t = list.textureBarrier(0);
    EXPECT_EQ(t.LayoutBefore, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
    EXPECT_EQ(t.LayoutAfter, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
    EXPECT_EQ(t.SyncBefore, D3D12_BARRIER_SYNC_ALL_SHADING);
    EXPECT_EQ(t.AccessAfter, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
    EXPECT_EQ(list.bufferBarrier(0).AccessBefore, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
}

namespace
{
    ID3D12Resource* resolveByIndex(const void* context, uint16_t resource)
    {
        const auto* table = static_cast<ID3D12Resource* const*>(context);
        return table[resource];
    }

    BarrierRec rec(uint16_t resource, ResourceKind kind, const UseState& before, const UseState& after, bool tracked = false, bool discard = false)
    {
        BarrierRec r;
        r.resource = resource;
        r.kind     = kind;
        r.before   = before;
        r.after    = after;
        r.tracked  = tracked;
        r.discard  = discard;
        return r;
    }
} // namespace

TEST(EnhancedBarrierEmitter, GraphRecordsUseTheTrackerForTrackedResourcesAndTheirOwnStatesOtherwise)
{
    ScopedTracker  scope;
    ID3D12Resource* table[3] = { fakeRes(0x10), fakeRes(0x20), nullptr };
    const UseState rt{ Sync::RenderTarget, Access::RenderTarget, Layout::RenderTarget };
    const UseState sr{ Sync::PixelShading, Access::ShaderResource, Layout::ShaderResource };
    const UseState ua{ Sync::ComputeShading, Access::UnorderedAccess, Layout::UnorderedAccess };
    const UseState none{ Sync::None, Access::NoAccess, Layout::Undefined };
    // Resource 0 is tracked and an opaque owner left it in UNORDERED_ACCESS.
    scope.tracker.track(table[0], ResourceKind::Texture, ua);

    const BarrierRec records[] = {
        rec(0, ResourceKind::Texture, none, sr, true),         // tracked boundary record
        rec(1, ResourceKind::Texture, none, rt, false, true),  // untracked, discard
        rec(2, ResourceKind::Texture, rt, sr),                 // no native resource: skipped
    };
    EnhancedBarrierEmitter emitter;
    TestList               list;
    emitter.collect(list, records, 3, &resolveByIndex, table, false);
    ASSERT_EQ(list.textureCount(), 2u);
    EXPECT_EQ(list.textureBarrier(0).LayoutBefore, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
    EXPECT_EQ(list.textureBarrier(0).LayoutAfter, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
    EXPECT_EQ(list.textureBarrier(0).Flags, D3D12_TEXTURE_BARRIER_FLAG_NONE);
    EXPECT_EQ(list.textureBarrier(1).LayoutBefore, D3D12_BARRIER_LAYOUT_UNDEFINED);
    EXPECT_EQ(list.textureBarrier(1).Flags, D3D12_TEXTURE_BARRIER_FLAG_DISCARD);
    EXPECT_EQ(scope.tracker.current(table[0])->layout, Layout::ShaderResource);
    EXPECT_EQ(emitter.emitted(), 2u);
}

TEST(EnhancedBarrierEmitter, TrackedRecordOnAnUntrackedResourceIsSkipped)
{
    ScopedTracker   scope;
    ID3D12Resource* table[1] = { fakeRes(0x10) };
    const BarrierRec r = rec(0, ResourceKind::Texture, { Sync::None, Access::NoAccess, Layout::Undefined }, { Sync::PixelShading, Access::ShaderResource, Layout::ShaderResource }, true);
    EnhancedBarrierEmitter emitter;
    TestList               list;
    emitter.collect(list, &r, 1, &resolveByIndex, table, false);
    EXPECT_EQ(list.count(), 0u);
    EXPECT_EQ(emitter.emitted(), 0u);
}

TEST(EnhancedBarrierEmitter, ValidateCountsLayoutMismatchesOnly)
{
    ScopedTracker   scope;
    ID3D12Resource* table[2] = { fakeRes(0x10), fakeRes(0x20) };
    const UseState  rt{ Sync::RenderTarget, Access::RenderTarget, Layout::RenderTarget };
    const UseState  rtWide{ Sync::All, Access::RenderTarget, Layout::RenderTarget };
    const UseState  sr{ Sync::PixelShading, Access::ShaderResource, Layout::ShaderResource };
    scope.tracker.track(table[0], ResourceKind::Texture, rtWide);
    scope.tracker.track(table[1], ResourceKind::Texture, sr);
    const BarrierRec records[] = {
        rec(0, ResourceKind::Texture, rt, sr), // same layout, other sync: not a mismatch
        rec(1, ResourceKind::Texture, rt, sr), // plan says RT, tracker says SR: mismatch (no barrier needed)
    };
    EnhancedBarrierEmitter emitter;
    TestList               list;
    emitter.collect(list, records, 2, &resolveByIndex, table, true);
    EXPECT_EQ(emitter.mismatches(), 1u);
    EXPECT_EQ(list.textureCount(), 1u);
    EXPECT_EQ(list.textureBarrier(0).SyncBefore, D3D12_BARRIER_SYNC_ALL); // the tracker's, not the plan's
}

TEST(EnhancedBarrierEmitter, APlannedRecordWaitsForADeclaredWriteTheTrackerNeverSaw)
{
    // The sun map: the plan's final transition left it no-access in DEPTH_WRITE, the next frame's
    // Shadows pass wrote it without a barrier, and the first reader's record says so.
    ScopedTracker   scope;
    ID3D12Resource* table[1] = { fakeRes(0x10) };
    const UseState  parked{ Sync::None, Access::NoAccess, Layout::DepthStencilWrite };
    const UseState  dw = useState(Use::DepthWrite, ResourceKind::Texture, Queue::Graphics);
    const UseState  sr = useState(Use::PixelSrv, ResourceKind::Texture, Queue::Graphics);
    scope.tracker.track(table[0], ResourceKind::Texture, parked);
    const BarrierRec       r = rec(0, ResourceKind::Texture, dw, sr);
    EnhancedBarrierEmitter emitter;
    TestList               list;
    emitter.collect(list, &r, 1, &resolveByIndex, table, true);
    ASSERT_EQ(list.textureCount(), 1u);
    EXPECT_EQ(list.textureBarrier(0).SyncBefore, D3D12_BARRIER_SYNC_DEPTH_STENCIL);
    EXPECT_EQ(list.textureBarrier(0).AccessBefore, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
    EXPECT_EQ(list.textureBarrier(0).LayoutBefore, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
    EXPECT_EQ(list.textureBarrier(0).LayoutAfter, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
    EXPECT_EQ(*scope.tracker.current(table[0]), sr);
    EXPECT_EQ(emitter.mismatches(), 0u);
}

TEST(EnhancedBarrierEmitter, APlannedBeforeStateWidensTheTrackersInTheSameLayoutOnly)
{
    ScopedTracker   scope;
    ID3D12Resource* table[3] = { fakeRes(0x10), fakeRes(0x20), fakeRes(0x30) };
    const UseState  rt{ Sync::RenderTarget, Access::RenderTarget, Layout::RenderTarget };
    const UseState  ua{ Sync::ComputeShading, Access::UnorderedAccess, Layout::UnorderedAccess };
    const UseState  sr{ Sync::PixelShading, Access::ShaderResource, Layout::ShaderResource };
    const UseState  csr{ Sync::ComputeShading, Access::ShaderResource, Layout::ShaderResource };
    const UseState  placeholder{ Sync::None, Access::NoAccess, Layout::ShaderResource };
    scope.tracker.track(table[0], ResourceKind::Texture, sr);
    scope.tracker.track(table[1], ResourceKind::Texture, sr);
    scope.tracker.track(table[2], ResourceKind::Texture, sr);
    const BarrierRec records[] = {
        rec(0, ResourceKind::Texture, csr, rt),         // same layout: both readers waited for
        rec(1, ResourceKind::Texture, ua, rt),          // another layout: the tracker's state
        rec(2, ResourceKind::Texture, placeholder, rt), // an import's placeholder: the tracker's state
    };
    EnhancedBarrierEmitter emitter;
    TestList               list;
    emitter.collect(list, records, 3, &resolveByIndex, table, false);
    ASSERT_EQ(list.textureCount(), 3u);
    EXPECT_EQ(list.textureBarrier(0).SyncBefore, D3D12_BARRIER_SYNC_PIXEL_SHADING | D3D12_BARRIER_SYNC_COMPUTE_SHADING);
    EXPECT_EQ(list.textureBarrier(0).AccessBefore, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
    EXPECT_EQ(list.textureBarrier(1).SyncBefore, D3D12_BARRIER_SYNC_PIXEL_SHADING);
    EXPECT_EQ(list.textureBarrier(1).LayoutBefore, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
    EXPECT_EQ(list.textureBarrier(2).SyncBefore, D3D12_BARRIER_SYNC_PIXEL_SHADING);
    EXPECT_EQ(list.textureBarrier(2).AccessBefore, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
    for (ID3D12Resource* res : table)
        EXPECT_EQ(*scope.tracker.current(res), rt);
}

TEST(EnhancedBarrierEmitter, TrackedRecordsKeepTheTrackersBeforeState)
{
    // A record after an opaque pass: the owner moved the resource, the plan's placeholder means nothing.
    ScopedTracker   scope;
    ID3D12Resource* table[1] = { fakeRes(0x10) };
    const UseState  parked{ Sync::None, Access::NoAccess, Layout::DepthStencilWrite };
    const UseState  dw = useState(Use::DepthWrite, ResourceKind::Texture, Queue::Graphics);
    const UseState  sr = useState(Use::PixelSrv, ResourceKind::Texture, Queue::Graphics);
    scope.tracker.track(table[0], ResourceKind::Texture, parked);
    const BarrierRec       r = rec(0, ResourceKind::Texture, dw, sr, true);
    EnhancedBarrierEmitter emitter;
    TestList               list;
    emitter.collect(list, &r, 1, &resolveByIndex, table, false);
    ASSERT_EQ(list.textureCount(), 1u);
    EXPECT_EQ(list.textureBarrier(0).SyncBefore, D3D12_BARRIER_SYNC_NONE);
    EXPECT_EQ(list.textureBarrier(0).AccessBefore, D3D12_BARRIER_ACCESS_NO_ACCESS);
    EXPECT_EQ(list.textureBarrier(0).LayoutBefore, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
}

TEST(EnhancedBarrierEmitter, AReadAfterADeclaredReadInTheSameLayoutStillNeedsNoBarrier)
{
    ScopedTracker   scope;
    ID3D12Resource* table[1] = { fakeRes(0x10) };
    const UseState  sr{ Sync::PixelShading, Access::ShaderResource, Layout::ShaderResource };
    const UseState  csr{ Sync::ComputeShading, Access::ShaderResource, Layout::ShaderResource };
    scope.tracker.track(table[0], ResourceKind::Texture, sr);
    const BarrierRec       r = rec(0, ResourceKind::Texture, sr, csr);
    EnhancedBarrierEmitter emitter;
    TestList               list;
    emitter.collect(list, &r, 1, &resolveByIndex, table, false);
    EXPECT_EQ(list.count(), 0u);
    EXPECT_EQ(scope.tracker.current(table[0])->sync, Sync::PixelShading | Sync::ComputeShading);
}

TEST(EnhancedBarrierEmitter, TexturesAreCreatedInTheEnhancedLayoutTheirStateMapsTo)
{
    D3D12_RESOURCE_DESC tex{};
    tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    D3D12_RESOURCE_DESC buf{};
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    EXPECT_TRUE(createNeedsLayout(tex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    EXPECT_TRUE(createNeedsLayout(tex, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    EXPECT_TRUE(createNeedsLayout(tex, D3D12_RESOURCE_STATE_COPY_DEST));
    EXPECT_TRUE(createNeedsLayout(tex, D3D12_RESOURCE_STATE_GENERIC_READ));
    EXPECT_TRUE(createNeedsLayout(tex, D3D12_RESOURCE_STATE_RENDER_TARGET));
    EXPECT_TRUE(createNeedsLayout(tex, D3D12_RESOURCE_STATE_DEPTH_WRITE));
    EXPECT_TRUE(createNeedsLayout(tex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    EXPECT_TRUE(createNeedsLayout(tex, D3D12_RESOURCE_STATE_COMMON));
    EXPECT_FALSE(createNeedsLayout(tex, D3D12_RESOURCE_STATE_STREAM_OUT));
    EXPECT_FALSE(createNeedsLayout(buf, D3D12_RESOURCE_STATE_COPY_DEST));
    EXPECT_EQ(initialLayout(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE), D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
    EXPECT_EQ(initialLayout(D3D12_RESOURCE_STATE_COPY_DEST), D3D12_BARRIER_LAYOUT_COPY_DEST);
    EXPECT_EQ(initialLayout(D3D12_RESOURCE_STATE_GENERIC_READ), D3D12_BARRIER_LAYOUT_GENERIC_READ);
    EXPECT_EQ(initialLayout(D3D12_RESOURCE_STATE_RENDER_TARGET), D3D12_BARRIER_LAYOUT_RENDER_TARGET);
    EXPECT_EQ(initialLayout(D3D12_RESOURCE_STATE_DEPTH_WRITE), D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
    EXPECT_EQ(initialLayout(D3D12_RESOURCE_STATE_COMMON), D3D12_BARRIER_LAYOUT_COMMON);
}

TEST(EnhancedBarrierEmitter, ShadowMapsAreCreatedInTheirFrameStartState)
{
    // ShadowSystem / LocalShadowSystem create in DEPTH_WRITE and trackResource that state; the first
    // frame's Shadows pass declares its write with no barrier, so the two must be the same state.
    Rg::UseState created;
    ASSERT_TRUE(legacyEquivalent(D3D12_RESOURCE_STATE_DEPTH_WRITE, Rg::ResourceKind::Texture, created));
    EXPECT_EQ(created, shadowFrameStartState());

    // The plan's final transition after the last reader: back to the write layout, no access.
    const Rg::UseState          read = Rg::useState(Rg::Use::PixelSrv, Rg::ResourceKind::Texture, Rg::Queue::Graphics);
    const D3D12_TEXTURE_BARRIER b    = makeTextureBarrier(nullptr, read, { Rg::Sync::None, Rg::Access::NoAccess, shadowFrameStartState().layout });
    EXPECT_EQ(b.SyncBefore, D3D12_BARRIER_SYNC_PIXEL_SHADING);
    EXPECT_EQ(b.AccessBefore, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
    EXPECT_EQ(b.LayoutBefore, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
    EXPECT_EQ(b.SyncAfter, D3D12_BARRIER_SYNC_NONE);
    EXPECT_EQ(b.AccessAfter, D3D12_BARRIER_ACCESS_NO_ACCESS);
    EXPECT_EQ(b.LayoutAfter, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
    EXPECT_EQ(b.Subresources.IndexOrFirstMipLevel, kAllSubresources);
}

TEST(EnhancedBarrierEmitter, ResourceDesc1CopiesEveryField)
{
    D3D12_RESOURCE_DESC d{};
    d.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Alignment          = 65536;
    d.Width              = 640;
    d.Height             = 360;
    d.DepthOrArraySize   = 6;
    d.MipLevels          = 3;
    d.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.SampleDesc         = { 1, 0 };
    d.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    d.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    const D3D12_RESOURCE_DESC1 d1 = resourceDesc1(d);
    EXPECT_EQ(d1.Dimension, d.Dimension);
    EXPECT_EQ(d1.Alignment, d.Alignment);
    EXPECT_EQ(d1.Width, d.Width);
    EXPECT_EQ(d1.Height, d.Height);
    EXPECT_EQ(d1.DepthOrArraySize, d.DepthOrArraySize);
    EXPECT_EQ(d1.MipLevels, d.MipLevels);
    EXPECT_EQ(d1.Format, d.Format);
    EXPECT_EQ(d1.Flags, d.Flags);
    EXPECT_EQ(d1.SamplerFeedbackMipRegion.Width, 0u);
}
