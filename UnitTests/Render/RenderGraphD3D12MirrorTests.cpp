#include <gtest/gtest.h>

#include "Render/RenderGraphTypes.h"

// The graph core is pure CPU (plan Phase 3: "No D3D12 calls"), so its Sync/Access/Layout enums mirror
// D3D12_BARRIER_SYNC/ACCESS/LAYOUT by value. Phase 4's executor casts them straight across; these
// asserts keep the mirror honest against the Agility SDK header the engine builds with.

#if defined(_WIN32)

#include <d3d12.h>

#include <cstdint>

using namespace Dark::Rg;

namespace
{
    template <class A, class B> constexpr bool same(A a, B b)
    {
        return static_cast<uint32_t>(a) == static_cast<uint32_t>(b);
    }
} // namespace

static_assert(same(Sync::None, D3D12_BARRIER_SYNC_NONE));
static_assert(same(Sync::All, D3D12_BARRIER_SYNC_ALL));
static_assert(same(Sync::Draw, D3D12_BARRIER_SYNC_DRAW));
static_assert(same(Sync::IndexInput, D3D12_BARRIER_SYNC_INDEX_INPUT));
static_assert(same(Sync::VertexShading, D3D12_BARRIER_SYNC_VERTEX_SHADING));
static_assert(same(Sync::PixelShading, D3D12_BARRIER_SYNC_PIXEL_SHADING));
static_assert(same(Sync::DepthStencil, D3D12_BARRIER_SYNC_DEPTH_STENCIL));
static_assert(same(Sync::RenderTarget, D3D12_BARRIER_SYNC_RENDER_TARGET));
static_assert(same(Sync::ComputeShading, D3D12_BARRIER_SYNC_COMPUTE_SHADING));
static_assert(same(Sync::Copy, D3D12_BARRIER_SYNC_COPY));
static_assert(same(Sync::ExecuteIndirect, D3D12_BARRIER_SYNC_EXECUTE_INDIRECT));
static_assert(same(Sync::AllShading, D3D12_BARRIER_SYNC_ALL_SHADING));
static_assert(same(Sync::NonPixelShading, D3D12_BARRIER_SYNC_NON_PIXEL_SHADING));
static_assert(same(Sync::ClearUav, D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW));

static_assert(same(Access::Common, D3D12_BARRIER_ACCESS_COMMON));
static_assert(same(Access::VertexBuffer, D3D12_BARRIER_ACCESS_VERTEX_BUFFER));
static_assert(same(Access::ConstantBuffer, D3D12_BARRIER_ACCESS_CONSTANT_BUFFER));
static_assert(same(Access::IndexBuffer, D3D12_BARRIER_ACCESS_INDEX_BUFFER));
static_assert(same(Access::RenderTarget, D3D12_BARRIER_ACCESS_RENDER_TARGET));
static_assert(same(Access::UnorderedAccess, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS));
static_assert(same(Access::DepthStencilWrite, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE));
static_assert(same(Access::DepthStencilRead, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ));
static_assert(same(Access::ShaderResource, D3D12_BARRIER_ACCESS_SHADER_RESOURCE));
static_assert(same(Access::IndirectArgument, D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT));
static_assert(same(Access::CopyDest, D3D12_BARRIER_ACCESS_COPY_DEST));
static_assert(same(Access::CopySource, D3D12_BARRIER_ACCESS_COPY_SOURCE));
static_assert(same(Access::NoAccess, D3D12_BARRIER_ACCESS_NO_ACCESS));

static_assert(same(Layout::Undefined, D3D12_BARRIER_LAYOUT_UNDEFINED));
static_assert(same(Layout::Common, D3D12_BARRIER_LAYOUT_COMMON));
static_assert(same(Layout::Present, D3D12_BARRIER_LAYOUT_PRESENT));
static_assert(same(Layout::GenericRead, D3D12_BARRIER_LAYOUT_GENERIC_READ));
static_assert(same(Layout::RenderTarget, D3D12_BARRIER_LAYOUT_RENDER_TARGET));
static_assert(same(Layout::UnorderedAccess, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS));
static_assert(same(Layout::DepthStencilWrite, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE));
static_assert(same(Layout::DepthStencilRead, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ));
static_assert(same(Layout::ShaderResource, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE));
static_assert(same(Layout::CopySource, D3D12_BARRIER_LAYOUT_COPY_SOURCE));
static_assert(same(Layout::CopyDest, D3D12_BARRIER_LAYOUT_COPY_DEST));
static_assert(same(Layout::DirectQueueGenericRead, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ));
static_assert(same(Layout::DirectQueueGenericReadComputeQueueAccessible, D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ_COMPUTE_QUEUE_ACCESSIBLE));

static_assert(same(kAllMips, 0xFFFFu));
static_assert(same(Queue::Graphics, 0u) && same(Queue::AsyncCompute, 1u));

TEST(RenderGraphD3D12Mirror, EnumsMatchTheAgilityHeader)
{
    // The static_asserts above are the test; this keeps the file visible in the test list.
    EXPECT_EQ(static_cast<uint32_t>(Layout::ShaderResource), static_cast<uint32_t>(D3D12_BARRIER_LAYOUT_SHADER_RESOURCE));
}

#endif
