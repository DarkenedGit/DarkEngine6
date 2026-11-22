#pragma once

#include "Render/RenderGraphTypes.h"

#include <cstdint>
#include <string>

// Enhanced-barrier rules for the render graph compiler (plan §3.7, §3.11, D13): what each Use means
// on each queue, which layouts/syncs/accesses a queue may name, how reads merge, and a validator
// for the records the compiler produces. No D3D12 calls.

namespace Dark::Rg
{

    bool isWriteUse(Use use);
    bool isReadUse(Use use);
    bool useAllowedForKind(Use use, ResourceKind kind);
    bool useLegalOnQueue(Use use, Queue queue);

    // Sync/access/layout of one use. Buffers get Layout::Undefined.
    UseState useState(Use use, ResourceKind kind, Queue queue);

    // Two read states one layout can serve (SRV + copy source -> GENERIC_READ, SRV + depth read ->
    // DIRECT_QUEUE_GENERIC_READ, as DEPTH_STENCIL_READ allows depth-stencil-read access only). False when no single layout serves both.
    bool combineReadStates(const UseState& a, const UseState& b, ResourceKind kind, UseState& out);
    bool isReadOnlyState(const UseState& state);

    bool layoutLegalOnQueue(Layout layout, Queue queue);
    bool syncLegalOnQueue(Sync sync, Queue queue);
    bool accessLegalOnQueue(Access access, Queue queue);
    bool accessAllowedInLayout(Access access, Layout layout);

    // Checks one record against the enhanced-barrier rules the graph relies on: NONE sync pairs with
    // NO_ACCESS, accesses fit the layout and the sync, everything is legal on the record's queue,
    // DISCARD only from UNDEFINED, no UNDEFINED after-layout. why gets the first problem.
    bool validateBarrier(const BarrierRec& rec, std::string* why = nullptr);

    // Enhanced-barrier equivalent of a legacy D3D12_RESOURCE_STATES value (passed as an integer so
    // this stays D3D12-free), per the enhanced-barriers spec's "Equivalent D3D12_BARRIER_LAYOUT /
    // ACCESS / SYNC for each D3D12_RESOURCE_STATES bit" tables (Phase 6). Textures get a public
    // layout: SRV-only states map to SHADER_RESOURCE, mixed read states to GENERIC_READ, depth read
    // alone to DEPTH_STENCIL_READ (which allows depth-stencil-read access only), depth read with
    // other reads to DIRECT_QUEUE_GENERIC_READ, or to DIRECT_QUEUE_GENERIC_READ_COMPUTE_QUEUE_ACCESSIBLE
    // when NON_PIXEL_SHADER_RESOURCE is among them, COMMON/PRESENT to COMMON. Buffers get
    // Layout::Undefined. False for states the engine never uses (stream out, resolve, predication,
    // video, shading rate, acceleration structures), two write states at once, or a buffer-only
    // state on a texture that has no readable bit left.
    bool legacyStateEquivalent(uint32_t d3d12States, ResourceKind kind, UseState& out);

    // Bytes per pixel of a DXGI_FORMAT value (0 = unknown or block-compressed).
    uint32_t formatBytesPerPixel(uint32_t dxgiFormat);
    // Estimated size of a resolved texture (all mips and slices); 0 when the format is unknown.
    // The alias plan uses it; Phase 9 replaces it with GetResourceAllocationInfo.
    uint64_t estimateTextureBytes(uint32_t width, uint32_t height, const TextureDesc& desc);

} // namespace Dark::Rg
