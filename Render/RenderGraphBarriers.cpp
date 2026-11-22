#include "Render/RenderGraphBarriers.h"

#include <cstdint>
#include <string>

namespace Dark::Rg
{

    namespace
    {
        constexpr Sync   kComputeSyncs    = Sync::All | Sync::ComputeShading | Sync::Copy | Sync::ExecuteIndirect | Sync::ClearUav;
        constexpr Access kComputeAccesses = Access::ShaderResource | Access::UnorderedAccess | Access::CopySource | Access::CopyDest | Access::IndirectArgument | Access::ConstantBuffer;

        bool subset(uint32_t bits, uint32_t allowed)
        {
            return (bits & ~allowed) == 0;
        }

        // Syncs that cover one access bit.
        Sync syncsForAccessBit(Access bit)
        {
            switch (bit)
            {
            case Access::VertexBuffer:
            case Access::IndexBuffer:
                return Sync::All | Sync::Draw | Sync::IndexInput | Sync::VertexShading;
            case Access::ConstantBuffer:
            case Access::ShaderResource:
                return Sync::All | Sync::Draw | Sync::VertexShading | Sync::PixelShading | Sync::ComputeShading | Sync::AllShading | Sync::NonPixelShading;
            case Access::UnorderedAccess:
                return Sync::All | Sync::Draw | Sync::VertexShading | Sync::PixelShading | Sync::ComputeShading | Sync::AllShading | Sync::NonPixelShading | Sync::ClearUav;
            case Access::RenderTarget:
                return Sync::All | Sync::Draw | Sync::RenderTarget;
            case Access::DepthStencilWrite:
            case Access::DepthStencilRead:
                return Sync::All | Sync::Draw | Sync::DepthStencil;
            case Access::IndirectArgument:
                return Sync::All | Sync::ExecuteIndirect;
            case Access::CopyDest:
            case Access::CopySource:
                return Sync::All | Sync::Copy;
            default:
                return Sync::None;
            }
        }

        bool accessFitsSync(Access access, Sync sync, std::string* why)
        {
            const uint32_t bits = static_cast<uint32_t>(access);
            for (uint32_t bit = 1; bit != 0 && bit < 0x80000000u; bit <<= 1)
            {
                if ((bits & bit) == 0)
                    continue;
                if (!any(syncsForAccessBit(static_cast<Access>(bit)) & sync))
                {
                    if (why)
                        *why = "access bit " + std::to_string(bit) + " is not covered by sync " + std::to_string(static_cast<uint32_t>(sync));
                    return false;
                }
            }
            return true;
        }

        bool validateSide(const UseState& s, ResourceKind kind, Queue queue, bool afterSide, std::string* why)
        {
            const char* side     = afterSide ? "after" : "before";
            const bool  none     = s.sync == Sync::None;
            const bool  noAccess = s.access == Access::NoAccess;
            // SYNC_NONE needs NO_ACCESS; a real sync with NO_ACCESS is legal (wait without a flush).
            if (none && !noAccess)
            {
                if (why)
                    *why = std::string(side) + ": SYNC_NONE must pair with ACCESS_NO_ACCESS";
                return false;
            }
            if (!noAccess && static_cast<uint32_t>(s.access) & static_cast<uint32_t>(Access::NoAccess))
            {
                if (why)
                    *why = std::string(side) + ": NO_ACCESS combined with other access bits";
                return false;
            }
            if (!syncLegalOnQueue(s.sync, queue))
            {
                if (why)
                    *why = std::string(side) + ": sync not legal on the " + queueName(queue) + " queue";
                return false;
            }
            if (!noAccess && !accessLegalOnQueue(s.access, queue))
            {
                if (why)
                    *why = std::string(side) + ": access not legal on the " + queueName(queue) + " queue";
                return false;
            }
            if (!noAccess && !accessFitsSync(s.access, s.sync, why))
                return false;
            if (kind == ResourceKind::Texture)
            {
                if (afterSide && s.layout == Layout::Undefined)
                {
                    if (why)
                        *why = "after: layout UNDEFINED";
                    return false;
                }
                if (s.layout != Layout::Undefined && !layoutLegalOnQueue(s.layout, queue))
                {
                    if (why)
                        *why = std::string(side) + ": layout " + layoutName(s.layout) + " not legal on the " + queueName(queue) + " queue";
                    return false;
                }
                if (!noAccess && !accessAllowedInLayout(s.access, s.layout))
                {
                    if (why)
                        *why = std::string(side) + ": access not allowed in layout " + layoutName(s.layout);
                    return false;
                }
            }
            else if (!noAccess && (any(s.access & (Access::RenderTarget | Access::DepthStencilRead | Access::DepthStencilWrite))))
            {
                if (why)
                    *why = std::string(side) + ": texture-only access on a buffer";
                return false;
            }
            return true;
        }
    } // namespace

    bool isWriteUse(Use use)
    {
        return use == Use::RenderTarget || use == Use::DepthWrite || use == Use::Uav || use == Use::CopyDst;
    }

    bool isReadUse(Use use)
    {
        switch (use)
        {
        case Use::PixelSrv:
        case Use::ComputeSrv:
        case Use::AnySrv:
        case Use::DepthRead:
        case Use::CopySrc:
        case Use::IndirectArgs:
        case Use::VertexIndex:
            return true;
        default:
            return false;
        }
    }

    bool useAllowedForKind(Use use, ResourceKind kind)
    {
        switch (use)
        {
        case Use::RenderTarget:
        case Use::DepthWrite:
        case Use::DepthRead:
            return kind == ResourceKind::Texture;
        case Use::IndirectArgs:
        case Use::VertexIndex:
            return kind == ResourceKind::Buffer;
        case Use::Present:
        case Use::Count:
            return false;
        default:
            return true;
        }
    }

    bool useLegalOnQueue(Use use, Queue queue)
    {
        if (queue == Queue::Graphics)
            return use != Use::Present && use != Use::Count;
        switch (use)
        {
        case Use::ComputeSrv:
        case Use::AnySrv:
        case Use::Uav:
        case Use::CopySrc:
        case Use::CopyDst:
        case Use::IndirectArgs:
            return true;
        default:
            return false;
        }
    }

    UseState useState(Use use, ResourceKind kind, Queue queue)
    {
        const bool compute = queue == Queue::AsyncCompute;
        UseState   s;
        switch (use)
        {
        case Use::PixelSrv:
            s = { Sync::PixelShading, Access::ShaderResource, Layout::ShaderResource };
            break;
        case Use::ComputeSrv:
            s = { Sync::ComputeShading, Access::ShaderResource, Layout::ShaderResource };
            break;
        case Use::AnySrv:
            s = { compute ? Sync::ComputeShading : Sync::AllShading, Access::ShaderResource, Layout::ShaderResource };
            break;
        case Use::RenderTarget:
            s = { Sync::RenderTarget, Access::RenderTarget, Layout::RenderTarget };
            break;
        case Use::DepthWrite:
            s = { Sync::DepthStencil, Access::DepthStencilWrite, Layout::DepthStencilWrite };
            break;
        case Use::DepthRead:
            s = { Sync::DepthStencil | Sync::PixelShading, Access::DepthStencilRead | Access::ShaderResource, Layout::DepthStencilRead };
            break;
        case Use::Uav:
            s = { compute ? Sync::ComputeShading : Sync::AllShading, Access::UnorderedAccess, Layout::UnorderedAccess };
            break;
        case Use::CopySrc:
            s = { Sync::Copy, Access::CopySource, Layout::CopySource };
            break;
        case Use::CopyDst:
            s = { Sync::Copy, Access::CopyDest, Layout::CopyDest };
            break;
        case Use::IndirectArgs:
            s = { Sync::ExecuteIndirect, Access::IndirectArgument, Layout::Undefined };
            break;
        case Use::VertexIndex:
            s = { Sync::IndexInput | Sync::VertexShading, Access::VertexBuffer | Access::IndexBuffer, Layout::Undefined };
            break;
        default:
            s = {};
            break;
        }
        if (kind == ResourceKind::Buffer)
            s.layout = Layout::Undefined;
        return s;
    }

    bool isReadOnlyState(const UseState& state)
    {
        constexpr Access kWrites = Access::RenderTarget | Access::UnorderedAccess | Access::DepthStencilWrite | Access::CopyDest;
        return !any(state.access & kWrites) || state.access == Access::NoAccess;
    }

    bool combineReadStates(const UseState& a, const UseState& b, ResourceKind kind, UseState& out)
    {
        if (!isReadOnlyState(a) || !isReadOnlyState(b))
            return false;
        out.sync   = a.sync | b.sync;
        out.access = a.access | b.access;
        if (kind == ResourceKind::Buffer)
        {
            out.layout = Layout::Undefined;
            return true;
        }
        if (a.layout == b.layout)
        {
            out.layout = a.layout;
            return true;
        }
        const auto pair = [&](Layout x, Layout y) { return (a.layout == x && b.layout == y) || (a.layout == y && b.layout == x); };
        if (pair(Layout::ShaderResource, Layout::CopySource) || pair(Layout::GenericRead, Layout::ShaderResource) || pair(Layout::GenericRead, Layout::CopySource))
        {
            out.layout = Layout::GenericRead;
            return true;
        }
        if (pair(Layout::DepthStencilRead, Layout::ShaderResource))
        {
            out.layout = Layout::DepthStencilRead;
            return true;
        }
        return false;
    }

    bool layoutLegalOnQueue(Layout layout, Queue queue)
    {
        switch (layout)
        {
        case Layout::Undefined:
        case Layout::Common:
        case Layout::GenericRead:
        case Layout::UnorderedAccess:
        case Layout::ShaderResource:
        case Layout::CopySource:
        case Layout::CopyDest:
            return true;
        case Layout::RenderTarget:
        case Layout::DepthStencilWrite:
        case Layout::DepthStencilRead:
            return queue == Queue::Graphics;
        }
        return false;
    }

    bool syncLegalOnQueue(Sync sync, Queue queue)
    {
        if (queue == Queue::Graphics)
            return true;
        return subset(static_cast<uint32_t>(sync), static_cast<uint32_t>(kComputeSyncs));
    }

    bool accessLegalOnQueue(Access access, Queue queue)
    {
        if (access == Access::NoAccess || queue == Queue::Graphics)
            return true;
        return subset(static_cast<uint32_t>(access), static_cast<uint32_t>(kComputeAccesses));
    }

    bool accessAllowedInLayout(Access access, Layout layout)
    {
        if (access == Access::NoAccess)
            return true;
        Access allowed = Access::Common;
        switch (layout)
        {
        case Layout::Common:
            allowed = Access::ShaderResource | Access::CopySource | Access::CopyDest;
            break;
        case Layout::GenericRead:
            allowed = Access::ShaderResource | Access::CopySource;
            break;
        case Layout::RenderTarget:
            allowed = Access::RenderTarget;
            break;
        case Layout::UnorderedAccess:
            allowed = Access::UnorderedAccess;
            break;
        case Layout::DepthStencilWrite:
            allowed = Access::DepthStencilWrite | Access::DepthStencilRead;
            break;
        case Layout::DepthStencilRead:
            allowed = Access::DepthStencilRead | Access::ShaderResource;
            break;
        case Layout::ShaderResource:
            allowed = Access::ShaderResource;
            break;
        case Layout::CopySource:
            allowed = Access::CopySource;
            break;
        case Layout::CopyDest:
            allowed = Access::CopyDest;
            break;
        case Layout::Undefined:
            return false;
        }
        return subset(static_cast<uint32_t>(access), static_cast<uint32_t>(allowed));
    }

    bool validateBarrier(const BarrierRec& rec, std::string* why)
    {
        if ((rec.placement == BarrierPlacement::Prologue || rec.placement == BarrierPlacement::Epilogue) && rec.queue != Queue::Graphics)
        {
            if (why)
                *why = "prologue/epilogue barriers are graphics-queue only";
            return false;
        }
        if (!validateSide(rec.before, rec.kind, rec.queue, false, why) || !validateSide(rec.after, rec.kind, rec.queue, true, why))
            return false;
        if (rec.kind == ResourceKind::Texture)
        {
            if (rec.discard && rec.before.layout != Layout::Undefined)
            {
                if (why)
                    *why = "DISCARD without a LAYOUT_UNDEFINED before-layout";
                return false;
            }
            if (rec.before.layout == Layout::Undefined && rec.before.access != Access::NoAccess)
            {
                if (why)
                    *why = "UNDEFINED before-layout needs ACCESS_NO_ACCESS";
                return false;
            }
        }
        else if (rec.discard)
        {
            if (why)
                *why = "DISCARD on a buffer";
            return false;
        }
        return true;
    }

    bool legacyStateEquivalent(uint32_t states, ResourceKind kind, UseState& out)
    {
        // D3D12_RESOURCE_STATES bits (d3d12.h; mirrored by value like the Sync/Access/Layout enums).
        constexpr uint32_t kVertexAndConstant = 0x1, kIndex = 0x2, kRenderTarget = 0x4, kUav = 0x8, kDepthWrite = 0x10, kDepthRead = 0x20, kNonPixelSrv = 0x40,
                           kPixelSrv = 0x80, kIndirect = 0x200, kCopyDest = 0x400, kCopySource = 0x800;
        constexpr uint32_t kKnown  = kVertexAndConstant | kIndex | kRenderTarget | kUav | kDepthWrite | kDepthRead | kNonPixelSrv | kPixelSrv | kIndirect | kCopyDest | kCopySource;
        constexpr uint32_t kWrites = kRenderTarget | kUav | kDepthWrite | kCopyDest;
        out = {};
        if ((states & ~kKnown) != 0)
            return false;
        const uint32_t writes = states & kWrites;
        if ((writes & (writes - 1)) != 0)
            return false; // two write states at once
        if (writes != 0 && states != writes)
            return false; // write states are exclusive in the legacy model
        if (states == 0)
        {
            // COMMON / PRESENT: everything before must finish (the legacy meaning).
            out.sync   = Sync::All;
            out.access = Access::Common;
            out.layout = kind == ResourceKind::Texture ? Layout::Common : Layout::Undefined;
            return true;
        }
        const bool texture = kind == ResourceKind::Texture;
        Sync       sync    = Sync::None;
        Access     access  = Access::Common;
        const auto add     = [&](uint32_t bit, Sync s, Access a, bool textureOk)
        {
            if ((states & bit) == 0)
                return;
            sync = sync | s;
            if (!texture || textureOk)
                access = access | a;
        };
        add(kVertexAndConstant, Sync::AllShading, Access::VertexBuffer | Access::ConstantBuffer, false);
        add(kIndex, Sync::IndexInput, Access::IndexBuffer, false);
        add(kRenderTarget, Sync::RenderTarget, Access::RenderTarget, true);
        add(kUav, Sync::AllShading, Access::UnorderedAccess, true);
        add(kDepthWrite, Sync::DepthStencil, Access::DepthStencilWrite, true);
        add(kDepthRead, Sync::DepthStencil, Access::DepthStencilRead, true);
        add(kNonPixelSrv, Sync::NonPixelShading, Access::ShaderResource, true);
        add(kPixelSrv, Sync::PixelShading, Access::ShaderResource, true);
        add(kIndirect, Sync::ExecuteIndirect, Access::IndirectArgument, false);
        add(kCopyDest, Sync::Copy, Access::CopyDest, true);
        add(kCopySource, Sync::Copy, Access::CopySource, true);
        if (!texture)
        {
            if (any(access & (Access::RenderTarget | Access::DepthStencilRead | Access::DepthStencilWrite)))
                return false;
            out = { sync, access, Layout::Undefined };
            return true;
        }
        if (access == Access::Common)
            return false; // only buffer bits (vertex/index/indirect) on a texture
        Layout layout = Layout::Undefined;
        if (states & kDepthWrite)
            layout = Layout::DepthStencilWrite;
        else if (states & kDepthRead)
            layout = Layout::DepthStencilRead;
        else if (states & kRenderTarget)
            layout = Layout::RenderTarget;
        else if (states & kUav)
            layout = Layout::UnorderedAccess;
        else if (states & kCopyDest)
            layout = Layout::CopyDest;
        else if (access == Access::CopySource)
            layout = Layout::CopySource;
        else if (access == Access::ShaderResource)
            layout = Layout::ShaderResource;
        else
            layout = Layout::GenericRead;
        if (!accessAllowedInLayout(access, layout))
            return false; // e.g. COPY_SOURCE with DEPTH_READ: no single public layout
        out = { sync, access, layout };
        return true;
    }

    uint32_t formatBytesPerPixel(uint32_t f)
    {
        if (f >= 1 && f <= 4)
            return 16; // R32G32B32A32
        if (f >= 5 && f <= 8)
            return 12; // R32G32B32
        if (f >= 9 && f <= 22)
            return 8; // R16G16B16A16, R32G32, R32G8X24
        if (f >= 23 && f <= 47)
            return 4; // R10G10B10A2, R11G11B10, R8G8B8A8, R16G16, R32, R24G8
        if (f >= 48 && f <= 59)
            return 2; // R8G8, R16
        if (f >= 60 && f <= 65)
            return 1; // R8, A8
        if (f == 67 || (f >= 87 && f <= 93))
            return 4; // R9G9B9E5, B8G8R8A8/X8 family
        return 0;
    }

    uint64_t estimateTextureBytes(uint32_t width, uint32_t height, const TextureDesc& desc)
    {
        const uint64_t bpp = formatBytesPerPixel(desc.format);
        if (bpp == 0 || width == 0 || height == 0)
            return 0;
        uint64_t       total = 0;
        uint32_t       w = width, h = height;
        const uint32_t mips = desc.mips == 0 ? 1u : desc.mips;
        for (uint32_t m = 0; m < mips; ++m)
        {
            total += uint64_t{ w } * h * bpp;
            if (w == 1 && h == 1)
                break; // the chain ends at 1x1 (kAllMips = full chain)
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
        const uint64_t slices = desc.arraySize == 0 ? 1u : desc.arraySize;
        return total * slices;
    }

    const char* useName(Use use)
    {
        switch (use)
        {
        case Use::PixelSrv:
            return "PixelSrv";
        case Use::ComputeSrv:
            return "ComputeSrv";
        case Use::AnySrv:
            return "AnySrv";
        case Use::RenderTarget:
            return "RenderTarget";
        case Use::DepthWrite:
            return "DepthWrite";
        case Use::DepthRead:
            return "DepthRead";
        case Use::Uav:
            return "Uav";
        case Use::CopySrc:
            return "CopySrc";
        case Use::CopyDst:
            return "CopyDst";
        case Use::IndirectArgs:
            return "IndirectArgs";
        case Use::VertexIndex:
            return "VertexIndex";
        case Use::Present:
            return "Present";
        case Use::Count:
            break;
        }
        return "?";
    }

    const char* queueName(Queue queue)
    {
        return queue == Queue::Graphics ? "Graphics" : "AsyncCompute";
    }

    const char* errorCodeName(ErrorCode code)
    {
        switch (code)
        {
        case ErrorCode::InvalidHandle:
            return "InvalidHandle";
        case ErrorCode::ReadBeforeWrite:
            return "ReadBeforeWrite";
        case ErrorCode::StaleWrite:
            return "StaleWrite";
        case ErrorCode::WrongUseForKind:
            return "WrongUseForKind";
        case ErrorCode::NotAWriteUse:
            return "NotAWriteUse";
        case ErrorCode::NotAReadUse:
            return "NotAReadUse";
        case ErrorCode::PresentUse:
            return "PresentUse";
        case ErrorCode::IncompatibleUses:
            return "IncompatibleUses";
        case ErrorCode::IllegalOnQueue:
            return "IllegalOnQueue";
        case ErrorCode::OwnBarriersOnCompute:
            return "OwnBarriersOnCompute";
        case ErrorCode::MixedOpaqueUse:
            return "MixedOpaqueUse";
        case ErrorCode::Cycle:
            return "Cycle";
        case ErrorCode::HistoryDescMismatch:
            return "HistoryDescMismatch";
        case ErrorCode::TooManyResources:
            return "TooManyResources";
        case ErrorCode::NotCompiled:
            return "NotCompiled";
        }
        return "?";
    }

    const char* layoutName(Layout layout)
    {
        switch (layout)
        {
        case Layout::Undefined:
            return "UNDEFINED";
        case Layout::Common:
            return "COMMON";
        case Layout::GenericRead:
            return "GENERIC_READ";
        case Layout::RenderTarget:
            return "RENDER_TARGET";
        case Layout::UnorderedAccess:
            return "UNORDERED_ACCESS";
        case Layout::DepthStencilWrite:
            return "DEPTH_STENCIL_WRITE";
        case Layout::DepthStencilRead:
            return "DEPTH_STENCIL_READ";
        case Layout::ShaderResource:
            return "SHADER_RESOURCE";
        case Layout::CopySource:
            return "COPY_SOURCE";
        case Layout::CopyDest:
            return "COPY_DEST";
        }
        return "?";
    }

} // namespace Dark::Rg
