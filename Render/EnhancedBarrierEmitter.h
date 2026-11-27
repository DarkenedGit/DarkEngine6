#pragma once

#include "Render/RenderGraphTypes.h"

#include <cstdint>
#include <d3d12.h>

// Enhanced barriers, the only barrier back end (render-graph plan §3.7, D13, Phase 6). Everything
// that changes a resource's state records an ID3D12GraphicsCommandList7::Barrier through this file;
// no ID3D12GraphicsCommandList::ResourceBarrier call is left in the engine, so legacy and enhanced
// barriers never meet on one subresource (the spec only allows that through a COMMON round trip).
//
// - makeTextureBarrier / makeBufferBarrier: Rg::UseState pairs to D3D12 structs (the Rg enums
//   mirror D3D12 by value; RenderGraphD3D12MirrorTests static_asserts that). No device needed.
// - EnhancedBarrierList: collects barriers and records them as one Barrier() call. Its legacy-state
//   entry points (transition(before, after), uav()) are how owners that still track
//   D3D12_RESOURCE_STATES record barriers: the states are translated with the spec's equivalence
//   tables (Rg::legacyStateEquivalent). When the resource is in the frame's ResourceStateTracker the
//   tracker's state replaces the caller's `before` and receives the `after`.
// - EnhancedBarrierEmitter: records one compiled graph barrier batch.

namespace Dark
{

    class ResourceStateTracker;

    inline constexpr uint32_t kAllSubresources = 0xFFFFFFFFu; // == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES

    // A legacy subresource index (or kAllSubresources) as an enhanced subresource range.
    D3D12_BARRIER_SUBRESOURCE_RANGE subresourceRange(uint32_t subresource);
    D3D12_TEXTURE_BARRIER makeTextureBarrier(ID3D12Resource* resource, const Rg::UseState& before, const Rg::UseState& after, bool discard = false,
                                             uint32_t subresource = kAllSubresources);
    D3D12_BUFFER_BARRIER  makeBufferBarrier(ID3D12Resource* resource, const Rg::UseState& before, const Rg::UseState& after);

    // The tracker the legacy-state helpers consult (Renderer sets it at init, null at shutdown).
    // Only the thread that set it sees it: the shadow worker records untracked resources only and
    // gets null, so the table is never read while the main thread writes it.
    void                  setFrameStateTracker(ResourceStateTracker* tracker);
    ResourceStateTracker* frameStateTracker();

    // Legacy creation state -> enhanced state (for track()). False (logged) for a state with no
    // public layout equivalent.
    bool legacyEquivalent(D3D12_RESOURCE_STATES state, Rg::ResourceKind kind, Rg::UseState& out);
    Rg::ResourceKind resourceKind(ID3D12Resource* resource); // buffer or texture, from GetDesc

    // Starts / stops tracking a resource in the frame tracker (no-op without one). `initial` is its
    // creation state.
    void trackResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES initial);
    void forgetResource(ID3D12Resource* resource);
    // Debug checks: true when the tracker holds `resource` in the layout `state` maps to (and for a
    // null resource, so an absent optional target never fires an assert).
    bool trackedInState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state);

    // A texture is created with CreateCommittedResource3 in the enhanced layout `initial` maps to.
    // Legacy CreateCommittedResource, even in a public state such as RENDER_TARGET, makes the first
    // Barrier() an interop, and the debug layer rejects that unless the texture is in COMMON
    // (error 1350). A state with no enhanced layout, and every buffer, stays on CreateCommittedResource.
    bool createNeedsLayout(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initial);
    D3D12_RESOURCE_DESC1 resourceDesc1(const D3D12_RESOURCE_DESC& desc);
    D3D12_BARRIER_LAYOUT  initialLayout(D3D12_RESOURCE_STATES initial); // public layout for a texture created in `initial`
    HRESULT createCommittedResource(ID3D12Device* device, const D3D12_HEAP_PROPERTIES& heap, D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC& desc,
                                    D3D12_RESOURCE_STATES initial, const D3D12_CLEAR_VALUE* clear, REFIID riid, void** out);

    class EnhancedBarrierList
    {
    public:
        static constexpr uint32_t kCapacity = 32; // per kind; a full list records itself first

        EnhancedBarrierList() = default;
        explicit EnhancedBarrierList(ID3D12GraphicsCommandList* cmd) :
            m_cmd(cmd)
        {
        }
        ~EnhancedBarrierList()
        {
            record();
        }
        EnhancedBarrierList(const EnhancedBarrierList&)            = delete;
        EnhancedBarrierList& operator=(const EnhancedBarrierList&) = delete;

        void setCommandList(ID3D12GraphicsCommandList* cmd)
        {
            m_cmd = cmd;
        }

        void texture(const D3D12_TEXTURE_BARRIER& barrier);
        void buffer(const D3D12_BUFFER_BARRIER& barrier);

        // Legacy-state transition (one subresource or all). Untracked resources use `before` as given.
        // Returns false when a state has no enhanced equivalent (logged once; nothing recorded).
        bool transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, uint32_t subresource = kAllSubresources);
        // Same, with the kind given (no GetDesc call; tests use it with fake resources).
        bool transition(ID3D12Resource* resource, Rg::ResourceKind kind, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, uint32_t subresource = kAllSubresources);
        // Tracked transition: before comes from the frame tracker. False when untracked (logged once).
        bool transitionTo(ID3D12Resource* resource, D3D12_RESOURCE_STATES after);
        bool transitionTo(ID3D12Resource* resource, const Rg::UseState& after);
        // Legacy UAV barrier: UAV -> UAV, all shading (UNORDERED_ACCESS layout on textures).
        void uav(ID3D12Resource* resource);
        void uav(ID3D12Resource* resource, Rg::ResourceKind kind);

        uint32_t count() const
        {
            return m_textureCount + m_bufferCount;
        }
        // What record() would submit (tests).
        uint32_t textureCount() const
        {
            return m_textureCount;
        }
        uint32_t bufferCount() const
        {
            return m_bufferCount;
        }
        const D3D12_TEXTURE_BARRIER& textureBarrier(uint32_t i) const
        {
            return m_textures[i];
        }
        const D3D12_BUFFER_BARRIER& bufferBarrier(uint32_t i) const
        {
            return m_buffers[i];
        }
        // Empties the list without recording (tests).
        void discard()
        {
            m_textureCount = m_bufferCount = 0;
        }
        // Records everything collected as one Barrier() call and empties the list. No-op when empty.
        void record();

    private:
        ID3D12GraphicsCommandList* m_cmd = nullptr;
        D3D12_TEXTURE_BARRIER      m_textures[kCapacity]{};
        D3D12_BUFFER_BARRIER       m_buffers[kCapacity]{};
        uint32_t                   m_textureCount = 0;
        uint32_t                   m_bufferCount  = 0;
    };

    // One-shot forms of the list for single barriers.
    bool recordTransition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                          uint32_t subresource = kAllSubresources);
    bool recordTransitionTo(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES after);
    void recordUavBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource);

    // Records compiled graph barrier batches (RgExecutor). Resources the frame tracker knows take
    // their before-state from it (and an owner's opaque pass may have moved them since compile),
    // widened by a planned record's own before-state (ResourceStateTracker::planBefore); records
    // whose before/after need no barrier per ResourceStateTracker::needsBarrier are dropped.
    class EnhancedBarrierEmitter
    {
    public:
        using ResolveFn = ID3D12Resource* (*)(const void* context, uint16_t resource);

        // validate: log (once per resource) when a tracked resource's state differs from the
        // compiled before-state of a record that did not ask for tracking (rg.validate).
        void record(ID3D12GraphicsCommandList* cmd, const Rg::BarrierRec* records, uint32_t count, ResolveFn resolve, const void* context, bool validate);
        // The translation record() does, into `list` (no command list needed).
        void collect(EnhancedBarrierList& list, const Rg::BarrierRec* records, uint32_t count, ResolveFn resolve, const void* context, bool validate);

        uint32_t emitted() const
        {
            return m_emitted;
        }
        uint32_t mismatches() const
        {
            return m_mismatches;
        }
        void resetCounters()
        {
            m_emitted    = 0;
            m_mismatches = 0;
        }

    private:
        uint32_t m_emitted    = 0;
        uint32_t m_mismatches = 0;
    };

} // namespace Dark
