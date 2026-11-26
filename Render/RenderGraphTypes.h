#pragma once

#include <cstdint>
#include <string>

// Render graph value types (plan §3.3, §3.7, §3.11). Pure C++: no D3D12 or Windows headers, so the
// graph compiler is unit-tested natively. The Sync/Access/Layout enums mirror D3D12_BARRIER_SYNC,
// D3D12_BARRIER_ACCESS and D3D12_BARRIER_LAYOUT value for value (enhanced barriers only, D13);
// UnitTests/Render/RenderGraphD3D12MirrorTests.cpp static_asserts that on Windows, so the rhi
// emitter (Phase 4) converts with a static_cast.

namespace Dark::Rg
{

    inline constexpr uint16_t kInvalidIndex = 0xFFFF;
    inline constexpr uint16_t kAllMips      = 0xFFFF;
    inline constexpr uint32_t kNoPass       = 0xFFFFFFFFu;
    // Graphics-queue barrier/sync slots before the first pass and after the last one.
    inline constexpr uint32_t kPrologue = 0xFFFFFFFEu;
    inline constexpr uint32_t kEpilogue = 0xFFFFFFFDu;
    // A texture handle's version bit for a disabled pass's output (RenderGraph::disabledOutput).
    inline constexpr uint16_t kDisabledOutputTag = 0x8000;

    struct TextureHandle
    {
        uint16_t index   = kInvalidIndex;
        uint16_t version = 0;
        bool     valid() const
        {
            return index != kInvalidIndex;
        }
        // The output of a pass that did not run this frame: names no resource (never valid()).
        bool disabledOutput() const
        {
            return index == kInvalidIndex && (version & kDisabledOutputTag) != 0;
        }
        bool operator==(const TextureHandle&) const = default;
    };

    struct BufferHandle
    {
        uint16_t index   = kInvalidIndex;
        uint16_t version = 0;
        bool     valid() const
        {
            return index != kInvalidIndex;
        }
        bool operator==(const BufferHandle&) const = default;
    };

    enum class SizeMode : uint8_t
    {
        Absolute,
        RelativeToOutput, // scale x output size
    };

    // format/viewFormat hold DXGI_FORMAT values (kept as integers so this header has no DXGI dependency).
    struct TextureDesc
    {
        SizeMode sizeMode   = SizeMode::RelativeToOutput;
        float    scale      = 1.0f; // 0.5 = half-res
        uint32_t width      = 0;    // Absolute only
        uint32_t height     = 0;    // Absolute only
        uint16_t mips       = 1;    // kAllMips = full chain down to 1x1
        uint16_t arraySize  = 1;
        uint32_t format     = 0; // DXGI_FORMAT
        uint32_t viewFormat = 0;
        float    clear[4]   = {};

        bool operator==(const TextureDesc&) const = default;
    };

    struct BufferDesc
    {
        uint64_t size   = 0;
        uint32_t stride = 0; // 0 = raw/untyped

        bool operator==(const BufferDesc&) const = default;
    };

    // The only thing passes say about access (§3.3).
    enum class Use : uint8_t
    {
        PixelSrv,
        ComputeSrv,
        AnySrv,
        RenderTarget,
        DepthWrite,
        DepthRead, // read-only depth: depth test and/or sampled
        Uav,
        CopySrc,
        CopyDst,
        IndirectArgs, // buffers only
        VertexIndex,  // buffers only
        Present,      // reserved: the back buffer's final layout, not a pass use
        Count
    };

    enum class Queue : uint8_t
    {
        Graphics,
        AsyncCompute,
    };
    inline constexpr uint32_t kQueueCount = 2;

    enum class PassFlags : uint8_t
    {
        None        = 0,
        SideEffect  = 1, // never culled; its work is visible outside the graph
        NeverCull   = 2,
        OwnBarriers = 4, // opaque pass during migration (Phases 4-6): graphics only; its owner records barriers through the ResourceStateTracker
    };
    constexpr PassFlags operator|(PassFlags a, PassFlags b)
    {
        return static_cast<PassFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }
    constexpr PassFlags operator&(PassFlags a, PassFlags b)
    {
        return static_cast<PassFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }
    constexpr bool hasFlag(PassFlags set, PassFlags f)
    {
        return (static_cast<uint8_t>(set) & static_cast<uint8_t>(f)) != 0;
    }

    enum class ResourceKind : uint8_t
    {
        Texture,
        Buffer,
    };

    enum class Lifetime : uint8_t
    {
        Transient, // created by a pass this frame; aliasable
        Imported,  // owned outside the graph (back buffer, persistent targets until Phase 5)
        History,   // one slot of a history pair (§3.6), owned by the graph's history registry
    };

    // D3D12_BARRIER_SYNC (enhanced barriers).
    enum class Sync : uint32_t
    {
        None            = 0,
        All             = 0x1,
        Draw            = 0x2,
        IndexInput      = 0x4,
        VertexShading   = 0x8,
        PixelShading    = 0x10,
        DepthStencil    = 0x20,
        RenderTarget    = 0x40,
        ComputeShading  = 0x80,
        Copy            = 0x200,
        ExecuteIndirect = 0x800,
        AllShading      = 0x1000,
        NonPixelShading = 0x2000,
        ClearUav        = 0x8000,
    };

    // D3D12_BARRIER_ACCESS.
    enum class Access : uint32_t
    {
        Common            = 0,
        VertexBuffer      = 0x1,
        ConstantBuffer    = 0x2,
        IndexBuffer       = 0x4,
        RenderTarget      = 0x8,
        UnorderedAccess   = 0x10,
        DepthStencilWrite = 0x20,
        DepthStencilRead  = 0x40,
        ShaderResource    = 0x80,
        IndirectArgument  = 0x200,
        CopyDest          = 0x400,
        CopySource        = 0x800,
        NoAccess          = 0x80000000u,
    };

    // D3D12_BARRIER_LAYOUT. Buffers have no layout; their records carry Undefined and are ignored.
    enum class Layout : uint32_t
    {
        Undefined         = 0xFFFFFFFFu,
        Common            = 0,
        Present           = 0,
        GenericRead       = 1,
        RenderTarget      = 2,
        UnorderedAccess   = 3,
        DepthStencilWrite = 4,
        DepthStencilRead  = 5,
        ShaderResource    = 6,
        CopySource        = 7,
        CopyDest          = 8,
        // Read-only depth that is also sampled (DEPTH_STENCIL_READ allows depth-stencil-read access
        // only). Direct queue only; 31 is also usable on compute lists (non-depth reads there).
        DirectQueueGenericRead                       = 19,
        DirectQueueGenericReadComputeQueueAccessible = 31,
    };

    constexpr Sync operator|(Sync a, Sync b)
    {
        return static_cast<Sync>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    constexpr Sync operator&(Sync a, Sync b)
    {
        return static_cast<Sync>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }
    constexpr Access operator|(Access a, Access b)
    {
        return static_cast<Access>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    constexpr Access operator&(Access a, Access b)
    {
        return static_cast<Access>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }
    constexpr bool any(Sync s)
    {
        return static_cast<uint32_t>(s) != 0;
    }
    constexpr bool any(Access a)
    {
        return static_cast<uint32_t>(a) != 0;
    }

    // Sync/access/layout for one access (or the union of a merged read group).
    struct UseState
    {
        Sync   sync   = Sync::None;
        Access access = Access::NoAccess;
        Layout layout = Layout::Undefined;

        bool operator==(const UseState&) const = default;
    };

    enum class BarrierPlacement : uint8_t
    {
        BeforePass, // recorded right before the pass, on its queue
        AfterPass,  // recorded right after the pass, before its signal (queue handoff / final state)
        Prologue,   // graphics queue, before the first pass
        Epilogue,   // graphics queue, after the last pass
    };

    // One enhanced barrier the executor emits (plan §3.7: {resource, before-use, after-use, queue}).
    struct BarrierRec
    {
        uint16_t         resource  = kInvalidIndex;
        ResourceKind     kind      = ResourceKind::Texture;
        Queue            queue     = Queue::Graphics;
        BarrierPlacement placement = BarrierPlacement::BeforePass;
        uint32_t         pass      = kNoPass; // pass index for Before/AfterPass, kPrologue / kEpilogue otherwise
        UseState         before;
        UseState         after;
        bool             discard = false; // LAYOUT_UNDEFINED first use: D3D12_TEXTURE_BARRIER_FLAG_DISCARD
        bool             aliased = false; // first use of memory another transient used earlier this frame
        // The before-state is only known at execute time: the previous access was an OwnBarriers
        // (opaque) pass, so the executor takes it from the ResourceStateTracker (Phase 6). The
        // compiled `before` is then NO_ACCESS/UNDEFINED as a placeholder and is never recorded.
        bool tracked = false;
    };

    // Barriers merged per pass: every record at one (placement, pass, queue) is one Barrier() call.
    struct BarrierBatch
    {
        BarrierPlacement placement = BarrierPlacement::BeforePass;
        uint32_t         pass      = kNoPass;
        Queue            queue     = Queue::Graphics;
        uint32_t         first     = 0; // index into barriers()
        uint32_t         count     = 0;
    };

    // Cross-queue sync point (§3.11): Signal(fence[signalQueue], frameBase + signalValue) after
    // afterPass; Wait(fence[signalQueue], frameBase + signalValue) on waitQueue before beforePass.
    struct SyncPoint
    {
        Queue    signalQueue = Queue::Graphics;
        uint32_t afterPass   = kNoPass; // pass index or kPrologue
        Queue    waitQueue   = Queue::AsyncCompute;
        uint32_t beforePass  = kNoPass; // pass index or kEpilogue
        uint32_t signalValue = 0;       // 1-based, increasing along signalQueue within the frame
    };

    enum class ErrorCode : uint8_t
    {
        InvalidHandle,    // handle from another frame/graph, or a texture/buffer mix-up
        ReadBeforeWrite,  // read of a transient version nothing wrote
        StaleWrite,       // write to an older version: two writers with no order
        WrongUseForKind,  // e.g. RenderTarget on a buffer, IndirectArgs on a texture
        NotAWriteUse,     // write()/readWrite() with a read-only use
        NotAReadUse,      // read() with a write use
        PresentUse,       // Use::Present in a pass
        IncompatibleUses, // one pass uses one resource in two incompatible ways
        IllegalOnQueue,   // use not legal on the pass's queue (e.g. RenderTarget on AsyncCompute)
        OwnBarriersOnCompute,
        MixedOpaqueUse, // one resource used by an OwnBarriers pass and a declared AsyncCompute pass
        Cycle,
        HistoryDescMismatch, // history(name) twice in one frame with different descs
        TooManyResources,
        NotCompiled,       // execute() without a successful compile()
        DisabledOutputUse, // read or write of a disabled pass's output (PassBuilder::tryRead takes a fallback)
    };

    struct Error
    {
        ErrorCode   code     = ErrorCode::InvalidHandle;
        uint32_t    pass     = kNoPass;
        uint16_t    resource = kInvalidIndex;
        std::string message;
    };

    const char* useName(Use use);
    const char* queueName(Queue queue);
    const char* errorCodeName(ErrorCode code);
    const char* layoutName(Layout layout);

} // namespace Dark::Rg
