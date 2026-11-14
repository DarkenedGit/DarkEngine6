#include "Render/FoliagePipeline.h"

#include "Assets/AssetManager.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Core/Log.h"
#include "Math/AABox3f.h"
#include "Math/Sphere3f.h"
#include "Render/Camera3D.h"
#include "Render/DepthState.h"
#include "Render/FoliagePrototypes.h"
#include "Render/Frustum3f.h"
#include "Render/GpuMaterial.h"
#include "Render/GpuModel.h"
#include "Render/GpuResourceCache.h"
#include "Render/MaterialSurface.h"
#include "Render/MeshConstants.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"
#include "Render/LocalShadowMath.h"
#include "Render/ShadowCascades.h"
#include "Terrain/TerrainGrid.h"
#include "Terrain/TerrainTileFile.h"
#include "Terrain/WindField.h"
#include "Terrain/WindGpu.h"

#include <cmath>
#include <cstddef>
#include <cstring>

namespace Dark
{
    namespace
    {
        // G-buffer, the cascades, and one view per shadowed local light. A point light
        // reuses that view for its other five faces. Another view in the same frame
        // would overwrite an index list the GPU has not read yet.
        constexpr uint32_t kFrameCount    = 2;
        constexpr uint32_t kViewsPerFrame = 1u + static_cast<uint32_t>(kMaxShadowCascades) + kMaxShadowedLocalLights;
        static_assert(kMaxShadowedLocalLights == 4u, "foliage view cap");
        static_assert(kViewsPerFrame == 8u, "foliage view cap");
        constexpr float    kGatherRadiusM = 96.0f;
        constexpr float    kGatherMoveM   = 16.0f;
        constexpr int      kKindCount     = static_cast<int>(Terrain::FoliageKind::Count);

        constexpr Terrain::FoliageKind kKindOrder[kKindCount] = {
            Terrain::FoliageKind::Tree,
            Terrain::FoliageKind::Flower,
            Terrain::FoliageKind::Rock,
            Terrain::FoliageKind::Grass,
        };

        static_assert(kFrameCount == Renderer::kFrameCount, "foliage upload frames");
        static_assert(Terrain::kMaxWorldTiles <= 8u, "resident mask");
        static_assert(Terrain::kMaxWorldTiles * Terrain::kMaxWorldTiles <= 64u, "resident mask");

        struct FoliageGBufferConstants
        {
            float localToRoot[16];
            float color[4];
            float roughness;
            float metallic;
            float ao;
            float normalScale;
            float alphaCutoff;
            float alphaModeMask;
            float kindIndex;
            float kindPad;
        };

        struct FoliageDepthConstants
        {
            float localToRoot[16];
            float lightWVP[16];
            float alphaCutoff;
            float alphaModeMask;
            float kindIndex;
            float pad;
        };

        static_assert(sizeof(FoliageGBufferConstants) == 28 * sizeof(float), "foliage gbuffer constants");
        // 28 constants + material table + worlds, indices, kinds, prefix, and the view CBV.
        static_assert(sizeof(FoliageGBufferConstants) / sizeof(float) + 1 + 2 + 2 + 2 + 2 + 2 <= 64, "foliage gbuffer root signature");
        static_assert(sizeof(FoliageDepthConstants) == 36 * sizeof(float), "foliage depth constants");
        static_assert(static_cast<int>(Terrain::WindKind::Count) == kKindCount, "wind kind count");
        static_assert(static_cast<int>(Terrain::WindKind::GrassTuft) == static_cast<int>(Terrain::FoliageKind::Grass), "grass wind kind");

        constexpr UINT64 kWorldStride     = static_cast<UINT64>(Terrain::kMaxFoliageDraw) * sizeof(Math::Matrix4f);
        constexpr UINT64 kIndexKindStride = static_cast<UINT64>(Terrain::kMaxFoliageDraw) * sizeof(uint32_t);
        constexpr UINT64 kIndexViewStride = kIndexKindStride * static_cast<UINT64>(kKindCount);
        constexpr UINT64 kKindStride      = static_cast<UINT64>(Terrain::kMaxFoliageDraw) * sizeof(uint32_t);
        constexpr UINT64 kCullCbStride    = 512;

        static_assert(kFrameCount == FoliagePipeline::kCullFrameSlots, "foliage cull frames");
        static_assert(kViewsPerFrame == FoliagePipeline::kCullViewsPerFrame, "foliage cull views");
        static_assert(kKindCount == static_cast<int>(FoliagePipeline::kCullKindCount), "foliage cull kinds");

        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        constexpr UINT kCullConstants = 0;
        constexpr UINT kCullWorlds    = 1;
        constexpr UINT kCullKinds     = 2;
        constexpr UINT kCullIndices   = 3;
        constexpr UINT kCullCounters  = 4;
        constexpr UINT kCullArgs      = 5;
        constexpr UINT kCullPrefix    = 6;
        constexpr UINT kCullCursor    = 7;
        constexpr UINT kCullVariant   = 8;
        constexpr uint32_t kVariantSlots = FoliagePipeline::kCullKindCount * Terrain::kMaxFoliageVariants;
        constexpr UINT64 kVariantInfoStride = 2048;

        struct FoliageCullConstants
        {
            float    planes[6][4];
            float    sphere[4];
            float    boundsMin[4][4];
            float    boundsMax[4][4];
            uint32_t kindResident[4];
            uint32_t instanceCount;
            uint32_t useSphere;
            uint32_t shadowView;
            uint32_t viewIndex;
            uint32_t partCount[4];
            uint32_t indexCount[32];
            uint32_t tail[4];
        };

        static_assert(sizeof(FoliageCullConstants) == FoliagePipeline::kCullConstantBytes, "foliage cull constants");
        static_assert(offsetof(FoliageCullConstants, sphere) == 96, "foliage cull sphere");
        static_assert(offsetof(FoliageCullConstants, boundsMin) == 112, "foliage cull bounds");
        static_assert(offsetof(FoliageCullConstants, boundsMax) == 176, "foliage cull bounds max");
        static_assert(offsetof(FoliageCullConstants, kindResident) == 240, "foliage cull resident");
        static_assert(offsetof(FoliageCullConstants, instanceCount) == 256, "foliage cull instance count");
        static_assert(offsetof(FoliageCullConstants, partCount) == 272, "foliage cull parts");
        static_assert(offsetof(FoliageCullConstants, indexCount) == 288, "foliage cull index counts");
        static_assert(offsetof(FoliageCullConstants, tail) == 416, "foliage cull tail");
        static_assert(sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) == 20, "foliage indirect stride");
        static_assert(kCullCbStride >= sizeof(FoliageCullConstants), "foliage cull cb");
        static_assert(kCullCbStride % 256u == 0u, "foliage cull cb align");

        ComPtr<ID3D12Resource> createUpload(ID3D12Device* device, UINT64 bytes, const char* what)
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width            = bytes;
            desc.Height           = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.SampleDesc       = { 1, 0 };
            desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ComPtr<ID3D12Resource> res;
            if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)), what))
                return nullptr;
            return res;
        }

        ComPtr<ID3D12Resource> createUav(ID3D12Device* device, UINT64 bytes, const char* what)
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width            = bytes;
            desc.Height           = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.Format           = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc       = { 1, 0 };
            desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            ComPtr<ID3D12Resource> res;
            if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&res)), what))
                return nullptr;
            return res;
        }

        void transitionBuffer(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
        {
            if (!cmd || !resource || before == after)
                return;
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = resource;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter  = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &barrier);
        }

        void uavBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource)
        {
            if (!cmd || !resource)
                return;
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = resource;
            cmd->ResourceBarrier(1, &barrier);
        }

        bool foliageArgWritable(D3D12_RESOURCE_STATES state)
        {
            return state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS || state == D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        }

        bool foliageIndexWritable(D3D12_RESOURCE_STATES state)
        {
            return state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS || state == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }

        bool logFoliageCullUnavailable()
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                DE_LOG_ERROR(LogCategory::Render, "FoliagePipeline: foliage cull unavailable; instances stay CPU lists");
            }
            return false;
        }

        bool logFoliagePartCap()
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                DE_LOG_ERROR(LogCategory::Render, "FoliagePipeline: foliage part cap exceeded; view stays on CPU lists");
            }
            return false;
        }

        // depth limits the walk to the CPU part prefix the depth constants read. capParts stops at the argument table.
        template <typename Fn>
        bool forKindParts(GpuResourceCache& gpu, const AssetRef<Model>& model, bool depth, bool capParts, Fn&& fn)
        {
            if (!model || !gpu.ensureModel(model))
                return true;
            GpuModel* gm = gpu.model(model->id);
            if (!gm)
                return true;
            const std::vector<GpuModel::Part>& gpuParts = gm->opaque();
            const std::vector<Model::Part>&    cpuParts = model->opaque();
            const size_t limit = depth ? (cpuParts.size() < gpuParts.size() ? cpuParts.size() : gpuParts.size()) : gpuParts.size();
            uint32_t     slot  = 0;
            for (size_t p = 0; p < limit; ++p)
            {
                const GpuModel::Part& part = gpuParts[p];
                if (part.skinned || !part.mesh.valid())
                    continue;
                if (capParts && slot >= FoliagePipeline::kCullPartsPerKind)
                    return false;
                const Model::Part* cpu = p < cpuParts.size() ? &cpuParts[p] : nullptr;
                fn(part, cpu, slot);
                ++slot;
            }
            return true;
        }

        void copyMatrix(float dst[16], const Math::Matrix4f& m)
        {
            m.store(dst);
        }

        bool recordTile(const Terrain::TerrainGrid& grid, float x, float z, int& tx, int& tz)
        {
            if (!grid.valid())
                return false;
            const float tileWorld = static_cast<float>(grid.tileCells()) * grid.cellSize();
            if (!(tileWorld > 0.0f))
                return false;
            const Math::Vector3f& origin = grid.origin();
            const int tilesX = static_cast<int>(grid.tilesX());
            const int tilesZ = static_cast<int>(grid.tilesZ());
            if (tilesX <= 0 || tilesZ <= 0)
                return false;
            tx = static_cast<int>(floorf((x - origin.x) / tileWorld));
            tz = static_cast<int>(floorf((z - origin.z) / tileWorld));
            if (tx < 0)
                tx = 0;
            if (tz < 0)
                tz = 0;
            if (tx >= tilesX)
                tx = tilesX - 1;
            if (tz >= tilesZ)
                tz = tilesZ - 1;
            return true;
        }

        Math::Matrix4f instanceWorld(const Terrain::FoliageRecord& rec)
        {
            return Math::Matrix4f::ScaleMatrix(rec.scale) * Math::Matrix4f::RotationMatrixY(rec.yaw) * Math::Matrix4f::RotationMatrixX(rec.pitch)
                * Math::Matrix4f::RotationMatrixY(rec.tiltYaw) * Math::Matrix4f::TranslationMatrix(rec.x, rec.y, rec.z);
        }

        // Grass and dandelion glTFs are authored at about a tenth of a meter.
        constexpr float kSmallFoliageScale = 10.0f;

        float foliageDrawScaleY(Terrain::FoliageKind kind, bool worldScale)
        {
            if (!worldScale && (kind == Terrain::FoliageKind::Grass || kind == Terrain::FoliageKind::Flower))
                return kSmallFoliageScale;
            return 1.0f;
        }

        Math::Matrix4f partLocalToRoot(const Math::Matrix4f& local, Terrain::FoliageKind kind, bool worldScale)
        {
            const float scale = foliageDrawScaleY(kind, worldScale);
            if (scale != 1.0f)
                return local * Math::Matrix4f::ScaleMatrix(scale);
            return local;
        }

        template <typename Fn>
        void visitResident(
            const Terrain::TerrainGrid& grid,
            const std::vector<Terrain::FoliageRecord>* editorRecords,
            const std::vector<std::vector<uint32_t>>* editorTiles,
            Fn&& fn)
        {
            if (editorRecords)
            {
                const int nx = static_cast<int>(grid.tilesX());
                const int nz = static_cast<int>(grid.tilesZ());
                if (!editorTiles || static_cast<int>(editorTiles->size()) != nx * nz)
                    return;
                for (int tz = 0; tz < nz; ++tz)
                {
                    for (int tx = 0; tx < nx; ++tx)
                    {
                        if (!grid.isResident(tx, tz))
                            continue;
                        const std::vector<uint32_t>& indices = (*editorTiles)[static_cast<size_t>(tz) * static_cast<size_t>(nx) + static_cast<size_t>(tx)];
                        for (uint32_t index : indices)
                        {
                            if (index >= editorRecords->size())
                                continue;
                            fn((*editorRecords)[index]);
                        }
                    }
                }
                return;
            }

            const int nx = static_cast<int>(grid.tilesX());
            const int nz = static_cast<int>(grid.tilesZ());
            for (int tz = 0; tz < nz; ++tz)
            {
                for (int tx = 0; tx < nx; ++tx)
                {
                    const std::vector<Terrain::FoliageRecord>* list = grid.residentFoliage(tx, tz);
                    if (!list)
                        continue;
                    for (const Terrain::FoliageRecord& rec : *list)
                        fn(rec);
                }
            }
        }

        uint64_t sourceStamp(const Terrain::TerrainGrid& grid, const std::vector<Terrain::FoliageRecord>* editorRecords)
        {
            uint64_t stamp = 0x9E3779B97F4A7C15ull;
            if (editorRecords)
            {
                stamp ^= static_cast<uint64_t>(editorRecords->size()) * 0x100000001b3ull;
                stamp ^= static_cast<uint64_t>(reinterpret_cast<uintptr_t>(editorRecords->data()));
                if (!editorRecords->empty())
                {
                    const Terrain::FoliageRecord& a = editorRecords->front();
                    const Terrain::FoliageRecord& b = editorRecords->back();
                    uint32_t bits = 0;
                    std::memcpy(&bits, &a.x, sizeof(bits));
                    stamp ^= bits;
                    std::memcpy(&bits, &a.y, sizeof(bits));
                    stamp ^= static_cast<uint64_t>(bits) << 32;
                    std::memcpy(&bits, &b.z, sizeof(bits));
                    stamp ^= bits;
                    stamp ^= static_cast<uint64_t>(a.kind) << 8;
                }
                uint64_t mask = 0;
                const int nx = static_cast<int>(grid.tilesX());
                const int nz = static_cast<int>(grid.tilesZ());
                for (int tz = 0; tz < nz; ++tz)
                {
                    for (int tx = 0; tx < nx; ++tx)
                    {
                        if (!grid.isResident(tx, tz))
                            continue;
                        mask |= 1ull << (static_cast<uint32_t>(tz) * Terrain::kMaxWorldTiles + static_cast<uint32_t>(tx));
                    }
                }
                stamp ^= mask;
            }
            else
            {
                const int nx = static_cast<int>(grid.tilesX());
                const int nz = static_cast<int>(grid.tilesZ());
                for (int tz = 0; tz < nz; ++tz)
                {
                    for (int tx = 0; tx < nx; ++tx)
                    {
                        const std::vector<Terrain::FoliageRecord>* list = grid.residentFoliage(tx, tz);
                        if (!list)
                            continue;
                        stamp ^= static_cast<uint64_t>(tx + 1) * 1315423911ull;
                        stamp ^= static_cast<uint64_t>(tz + 1) * 2654435761ull;
                        stamp ^= static_cast<uint64_t>(list->size()) << 17;
                        stamp ^= static_cast<uint64_t>(reinterpret_cast<uintptr_t>(list->data()));
                    }
                }
            }
            uint32_t ox = 0;
            std::memcpy(&ox, &grid.origin().x, sizeof(ox));
            stamp ^= ox;
            stamp ^= static_cast<uint64_t>(grid.tilesX()) << 48;
            stamp ^= static_cast<uint64_t>(grid.tilesZ()) << 40;
            return stamp;
        }

        void fillSurface(GpuResourceCache& gpu, AssetID matId, FoliageGBufferConstants& cb)
        {
            MeshGBufferConstants surface{};
            if (AssetRef<Material> mat = gpu.cpuMaterial(matId))
                applyMaterialSurface(*mat, surface);
            else
            {
                surface.color[0]      = 1.0f;
                surface.color[1]      = 1.0f;
                surface.color[2]      = 1.0f;
                surface.color[3]      = 0.0f;
                surface.roughness     = 1.0f;
                surface.metallic      = 0.0f;
                surface.ao            = 1.0f;
                surface.normalScale   = 1.0f;
                surface.alphaCutoff   = 0.5f;
                surface.alphaModeMask = 0.0f;
            }
            std::memcpy(cb.color, surface.color, sizeof(cb.color));
            cb.roughness     = surface.roughness;
            cb.metallic      = surface.metallic;
            cb.ao            = surface.ao;
            cb.normalScale   = surface.normalScale;
            cb.alphaCutoff   = surface.alphaCutoff;
            cb.alphaModeMask = surface.alphaModeMask;
        }
    } // namespace

    struct FoliageCullGpu
    {
        ComPtr<ID3D12Device>           device;
        ComPtr<ID3D12RootSignature>    root;
        ComPtr<ID3D12PipelineState>    compact;
        ComPtr<ID3D12PipelineState>    prefixPso;
        ComPtr<ID3D12PipelineState>    scatterPso;
        ComPtr<ID3D12PipelineState>    argsPso;
        ComPtr<ID3D12CommandSignature> signature;
        ComPtr<ID3D12DescriptorHeap>   heap;
        ComPtr<ID3D12DescriptorHeap>   cpuHeap;
        ComPtr<ID3D12Resource>         indices[FoliagePipeline::kCullFrameSlots];
        ComPtr<ID3D12Resource>         counters[FoliagePipeline::kCullFrameSlots];
        ComPtr<ID3D12Resource>         args[FoliagePipeline::kCullFrameSlots];
        ComPtr<ID3D12Resource>         prefix[FoliagePipeline::kCullFrameSlots];
        ComPtr<ID3D12Resource>         cursor[FoliagePipeline::kCullFrameSlots];
        ComPtr<ID3D12Resource>         cb;
        ComPtr<ID3D12Resource>         variantInfo;
        uint8_t*                       cbMap      = nullptr;
        uint8_t*                       variantMap = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS      cbGpu      = 0;
        D3D12_GPU_VIRTUAL_ADDRESS      variantGpu = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE    cpu[FoliagePipeline::kCullFrameSlots]{};
        D3D12_GPU_DESCRIPTOR_HANDLE    gpu[FoliagePipeline::kCullFrameSlots]{};
        D3D12_CPU_DESCRIPTOR_HANDLE    cursorCpu[FoliagePipeline::kCullFrameSlots]{};
        D3D12_GPU_DESCRIPTOR_HANDLE    cursorGpu[FoliagePipeline::kCullFrameSlots]{};
        D3D12_RESOURCE_STATES          argState[FoliagePipeline::kCullFrameSlots]    = { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
        D3D12_RESOURCE_STATES          indexState[FoliagePipeline::kCullFrameSlots]  = { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
        D3D12_RESOURCE_STATES          prefixState[FoliagePipeline::kCullFrameSlots] = { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
        ComPtr<ID3D12DescriptorHeap>   depthHeap;
        ComPtr<ID3D12DescriptorHeap>   depthCpuHeap;
        ComPtr<ID3D12Resource>         depthIndices[FoliagePipeline::kCullFrameSlots];
        ComPtr<ID3D12Resource>         depthCounters[FoliagePipeline::kCullFrameSlots];
        ComPtr<ID3D12Resource>         depthArgs[FoliagePipeline::kCullFrameSlots];
        ComPtr<ID3D12Resource>         depthPrefix[FoliagePipeline::kCullFrameSlots];
        ComPtr<ID3D12Resource>         depthCursor[FoliagePipeline::kCullFrameSlots];
        D3D12_CPU_DESCRIPTOR_HANDLE    depthCpu[FoliagePipeline::kCullFrameSlots]{};
        D3D12_GPU_DESCRIPTOR_HANDLE    depthGpu[FoliagePipeline::kCullFrameSlots]{};
        D3D12_CPU_DESCRIPTOR_HANDLE    depthCursorCpu[FoliagePipeline::kCullFrameSlots]{};
        D3D12_GPU_DESCRIPTOR_HANDLE    depthCursorGpu[FoliagePipeline::kCullFrameSlots]{};
        D3D12_RESOURCE_STATES          depthArgState[FoliagePipeline::kCullFrameSlots]    = { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
        D3D12_RESOURCE_STATES          depthIndexState[FoliagePipeline::kCullFrameSlots]  = { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
        D3D12_RESOURCE_STATES          depthPrefixState[FoliagePipeline::kCullFrameSlots] = { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
        bool                           ready = false;
    };

    FoliagePipeline::FoliagePipeline() = default;

    FoliagePipeline::~FoliagePipeline()
    {
        destroy();
    }

    bool FoliagePipeline::create(ID3D12Device* device)
    {
        destroy();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliagePipeline::create: null device");
            return false;
        }

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        // Four maps only. t4 is the world buffer, so the UV slot stays out of this table.
        srvRange.NumDescriptors                    = 4;
        srvRange.BaseShaderRegister                = 0;
        srvRange.RegisterSpace                     = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER gbParams[7]{};
        gbParams[kGbConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        gbParams[kGbConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        gbParams[kGbConstants].Constants.ShaderRegister = 0;
        gbParams[kGbConstants].Constants.RegisterSpace  = 0;
        gbParams[kGbConstants].Constants.Num32BitValues = static_cast<UINT>(sizeof(FoliageGBufferConstants) / 4);

        gbParams[kGbMaterial].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        gbParams[kGbMaterial].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        gbParams[kGbMaterial].DescriptorTable.NumDescriptorRanges = 1;
        gbParams[kGbMaterial].DescriptorTable.pDescriptorRanges   = &srvRange;

        gbParams[kGbWorlds].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        gbParams[kGbWorlds].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        gbParams[kGbWorlds].Descriptor.ShaderRegister = 4;
        gbParams[kGbWorlds].Descriptor.RegisterSpace  = 0;

        gbParams[kGbIndices].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        gbParams[kGbIndices].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        gbParams[kGbIndices].Descriptor.ShaderRegister = 5;
        gbParams[kGbIndices].Descriptor.RegisterSpace  = 0;

        gbParams[kGbView].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        gbParams[kGbView].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        gbParams[kGbView].Descriptor.ShaderRegister = 1;
        gbParams[kGbView].Descriptor.RegisterSpace  = 0;

        gbParams[kGbKinds].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        gbParams[kGbKinds].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        gbParams[kGbKinds].Descriptor.ShaderRegister = 6;
        gbParams[kGbKinds].Descriptor.RegisterSpace  = 0;

        gbParams[kGbPrefix].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        gbParams[kGbPrefix].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        gbParams[kGbPrefix].Descriptor.ShaderRegister = 7;
        gbParams[kGbPrefix].Descriptor.RegisterSpace  = 0;

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.MaxLOD           = D3D12_FLOAT32_MAX;
        samp.ShaderRegister   = 0;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC gbDesc{};
        gbDesc.NumParameters     = 7;
        gbDesc.pParameters       = gbParams;
        gbDesc.NumStaticSamplers = 1;
        gbDesc.pStaticSamplers   = &samp;
        gbDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&gbDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (foliage gbuffer)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Foliage gbuffer root signature: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_gbRoot)), "CreateRootSignature (foliage gbuffer)"))
            return false;

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent("shaders/FoliageGBuffer.hlsl", "VSMain", "vs_5_0", vs) || !compileShaderFromContent("shaders/FoliageGBuffer.hlsl", "PSMain", "ps_5_0", ps))
        {
            destroy();
            return false;
        }

        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature                                   = m_gbRoot.Get();
        psoDesc.VS                                               = { vs->GetBufferPointer(), vs->GetBufferSize() };
        psoDesc.PS                                               = { ps->GetBufferPointer(), ps->GetBufferSize() };
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.BlendState.RenderTarget[2].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.BlendState.RenderTarget[3].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.SampleMask                                       = UINT_MAX;
        psoDesc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode                         = D3D12_CULL_MODE_BACK;
        psoDesc.RasterizerState.FrontCounterClockwise            = TRUE;
        psoDesc.RasterizerState.DepthClipEnable                  = TRUE;
        psoDesc.DepthStencilState.DepthEnable                    = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc                      = sceneDepthFunc();
        psoDesc.DepthStencilState.StencilEnable                  = FALSE;
        psoDesc.InputLayout                                      = { inputLayout, _countof(inputLayout) };
        psoDesc.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets                                 = 4;
        psoDesc.RTVFormats[0]                                    = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        psoDesc.RTVFormats[1]                                    = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.RTVFormats[2]                                    = DXGI_FORMAT_R16G16_FLOAT;
        psoDesc.RTVFormats[3]                                    = DXGI_FORMAT_R8_UNORM;
        psoDesc.DSVFormat                                        = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc                                       = { 1, 0 };

        if (FailedHr(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_gbPso)), "CreateGraphicsPipelineState (foliage gbuffer)"))
        {
            destroy();
            return false;
        }
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        if (FailedHr(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_gbPsoTwoSided)), "CreateGraphicsPipelineState (foliage gbuffer two-sided)"))
        {
            destroy();
            return false;
        }
        {
            D3D_SHADER_MACRO thermalMacros[3];
            makeThermalMacros("0.40", thermalMacros);
            ComPtr<ID3DBlob> thermalPs;
            if (!compileShaderFromContent("shaders/FoliageGBuffer.hlsl", "PSMain", "ps_5_0", thermalPs, thermalMacros))
            {
                destroy();
                return false;
            }
            D3D12_GRAPHICS_PIPELINE_STATE_DESC thermalDesc = psoDesc;
            thermalDesc.PS = { thermalPs->GetBufferPointer(), thermalPs->GetBufferSize() };
            thermalDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
            if (FailedHr(device->CreateGraphicsPipelineState(&thermalDesc, IID_PPV_ARGS(&m_gbThermal)), "CreateGraphicsPipelineState (foliage thermal)"))
            {
                destroy();
                return false;
            }
            thermalDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            if (FailedHr(device->CreateGraphicsPipelineState(&thermalDesc, IID_PPV_ARGS(&m_gbThermalTwoSided)), "CreateGraphicsPipelineState (foliage thermal two-sided)"))
            {
                destroy();
                return false;
            }
            D3D_SHADER_MACRO uvMacros[3];
            makeUvMacros("0.08", uvMacros);
            ComPtr<ID3DBlob> uvPs;
            if (!compileShaderFromContent("shaders/FoliageGBuffer.hlsl", "PSMain", "ps_5_0", uvPs, uvMacros))
            {
                destroy();
                return false;
            }
            D3D12_GRAPHICS_PIPELINE_STATE_DESC uvDesc = psoDesc;
            uvDesc.PS = { uvPs->GetBufferPointer(), uvPs->GetBufferSize() };
            uvDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
            if (FailedHr(device->CreateGraphicsPipelineState(&uvDesc, IID_PPV_ARGS(&m_gbUv)), "CreateGraphicsPipelineState (foliage ultraviolet)"))
            {
                destroy();
                return false;
            }
            uvDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            if (FailedHr(device->CreateGraphicsPipelineState(&uvDesc, IID_PPV_ARGS(&m_gbUvTwoSided)), "CreateGraphicsPipelineState (foliage ultraviolet two-sided)"))
            {
                destroy();
                return false;
            }
        }

        D3D12_ROOT_PARAMETER depthParams[6]{};
        depthParams[kDepthConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        depthParams[kDepthConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        depthParams[kDepthConstants].Constants.ShaderRegister = 0;
        depthParams[kDepthConstants].Constants.RegisterSpace  = 0;
        depthParams[kDepthConstants].Constants.Num32BitValues = static_cast<UINT>(sizeof(FoliageDepthConstants) / 4);

        depthParams[kDepthWorlds].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        depthParams[kDepthWorlds].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        depthParams[kDepthWorlds].Descriptor.ShaderRegister = 4;
        depthParams[kDepthWorlds].Descriptor.RegisterSpace  = 0;

        depthParams[kDepthIndices].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        depthParams[kDepthIndices].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        depthParams[kDepthIndices].Descriptor.ShaderRegister = 5;
        depthParams[kDepthIndices].Descriptor.RegisterSpace  = 0;

        depthParams[kDepthMaterial].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        depthParams[kDepthMaterial].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        depthParams[kDepthMaterial].DescriptorTable.NumDescriptorRanges = 1;
        depthParams[kDepthMaterial].DescriptorTable.pDescriptorRanges   = &srvRange;

        depthParams[kDepthView].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        depthParams[kDepthView].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        depthParams[kDepthView].Descriptor.ShaderRegister = 1;
        depthParams[kDepthView].Descriptor.RegisterSpace  = 0;

        depthParams[kDepthPrefix].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        depthParams[kDepthPrefix].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        depthParams[kDepthPrefix].Descriptor.ShaderRegister = 7;
        depthParams[kDepthPrefix].Descriptor.RegisterSpace  = 0;

        D3D12_ROOT_SIGNATURE_DESC depthDesc{};
        depthDesc.NumParameters     = 6;
        depthDesc.pParameters       = depthParams;
        depthDesc.NumStaticSamplers = 1;
        depthDesc.pStaticSamplers   = &samp;
        depthDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        rsBlob.Reset();
        rsErr.Reset();
        if (FailedHr(D3D12SerializeRootSignature(&depthDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (foliage depth)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Foliage depth root signature: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            destroy();
            return false;
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_depthRoot)), "CreateRootSignature (foliage depth)"))
        {
            destroy();
            return false;
        }

        ComPtr<ID3DBlob> depthVs;
        ComPtr<ID3DBlob> depthPs;
        if (!compileShaderFromContent("shaders/FoliageDepth.hlsl", "VSMain", "vs_5_0", depthVs)
            || !compileShaderFromContent("shaders/FoliageDepth.hlsl", "PSMain", "ps_5_0", depthPs))
        {
            destroy();
            return false;
        }

        D3D12_INPUT_ELEMENT_DESC depthLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC depthPso{};
        depthPso.pRootSignature                        = m_depthRoot.Get();
        depthPso.VS                                    = { depthVs->GetBufferPointer(), depthVs->GetBufferSize() };
        depthPso.PS                                    = { depthPs->GetBufferPointer(), depthPs->GetBufferSize() };
        depthPso.SampleMask                            = UINT_MAX;
        depthPso.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
        depthPso.RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
        depthPso.RasterizerState.FrontCounterClockwise = TRUE;
        depthPso.RasterizerState.DepthClipEnable       = TRUE;
        depthPso.RasterizerState.DepthBias             = -4000;
        depthPso.RasterizerState.SlopeScaledDepthBias  = -2.5f;
        depthPso.RasterizerState.DepthBiasClamp        = 0.0f;
        depthPso.DepthStencilState.DepthEnable         = TRUE;
        depthPso.DepthStencilState.DepthWriteMask      = D3D12_DEPTH_WRITE_MASK_ALL;
        depthPso.DepthStencilState.DepthFunc           = shadowDepthFunc();
        depthPso.DepthStencilState.StencilEnable       = FALSE;
        depthPso.InputLayout                           = { depthLayout, _countof(depthLayout) };
        depthPso.PrimitiveTopologyType                 = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        depthPso.NumRenderTargets                      = 0;
        depthPso.DSVFormat                             = DXGI_FORMAT_D32_FLOAT;
        depthPso.SampleDesc                            = { 1, 0 };

        if (FailedHr(device->CreateGraphicsPipelineState(&depthPso, IID_PPV_ARGS(&m_depthPso)), "CreateGraphicsPipelineState (foliage depth)"))
        {
            destroy();
            return false;
        }
        depthPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        if (FailedHr(device->CreateGraphicsPipelineState(&depthPso, IID_PPV_ARGS(&m_depthPsoTwoSided)), "CreateGraphicsPipelineState (foliage depth two-sided)"))
        {
            destroy();
            return false;
        }

        const UINT64 viewBytes = static_cast<UINT64>(kFrameCount) * kViewsPerFrame * Terrain::kFoliageViewCbStride;
        const UINT64 prefixBytes = static_cast<UINT64>(kFrameCount) * kViewsPerFrame * kVariantInfoStride;
        m_worlds    = createUpload(device, kWorldStride * kFrameCount, "CreateCommittedResource foliage worlds");
        m_indices   = createUpload(device, kIndexViewStride * kViewsPerFrame * kFrameCount, "CreateCommittedResource foliage indices");
        m_viewCb    = createUpload(device, viewBytes, "CreateCommittedResource foliage view");
        m_kinds     = createUpload(device, kKindStride * kFrameCount, "CreateCommittedResource foliage kinds");
        m_cpuPrefix = createUpload(device, prefixBytes, "CreateCommittedResource foliage cpu prefix");
        if (!m_worlds || !m_indices || !m_viewCb || !m_kinds || !m_cpuPrefix)
        {
            destroy();
            return false;
        }
        if (FailedHr(m_worlds->Map(0, nullptr, reinterpret_cast<void**>(&m_worldMap)), "Map foliage worlds")
            || FailedHr(m_indices->Map(0, nullptr, reinterpret_cast<void**>(&m_indexMap)), "Map foliage indices")
            || FailedHr(m_viewCb->Map(0, nullptr, reinterpret_cast<void**>(&m_viewMap)), "Map foliage view")
            || FailedHr(m_kinds->Map(0, nullptr, reinterpret_cast<void**>(&m_kindMap)), "Map foliage kinds")
            || FailedHr(m_cpuPrefix->Map(0, nullptr, reinterpret_cast<void**>(&m_cpuPrefixMap)), "Map foliage cpu prefix"))
        {
            destroy();
            return false;
        }
        std::memset(m_worldMap, 0, static_cast<size_t>(kWorldStride * kFrameCount));
        std::memset(m_indexMap, 0, static_cast<size_t>(kIndexViewStride * kViewsPerFrame * kFrameCount));
        std::memset(m_viewMap, 0, static_cast<size_t>(viewBytes));
        std::memset(m_kindMap, 0, static_cast<size_t>(kKindStride * kFrameCount));
        std::memset(m_cpuPrefixMap, 0, static_cast<size_t>(prefixBytes));
        m_worldGpu     = m_worlds->GetGPUVirtualAddress();
        m_indexGpu     = m_indices->GetGPUVirtualAddress();
        m_viewGpu      = m_viewCb->GetGPUVirtualAddress();
        m_kindGpu      = m_kinds->GetGPUVirtualAddress();
        m_cpuPrefixGpu = m_cpuPrefix->GetGPUVirtualAddress();
        m_cpuGen   = 1;

        // Cull failure leaves the CPU lists. It does not turn foliage off.
        createCull(device);

        DE_LOG_INFO(LogCategory::Render, "FoliagePipeline: ready");
        return true;
    }

    void FoliagePipeline::destroy()
    {
        if (m_worlds && m_worldMap)
            m_worlds->Unmap(0, nullptr);
        if (m_indices && m_indexMap)
            m_indices->Unmap(0, nullptr);
        if (m_viewCb && m_viewMap)
            m_viewCb->Unmap(0, nullptr);
        if (m_kinds && m_kindMap)
            m_kinds->Unmap(0, nullptr);
        if (m_cpuPrefix && m_cpuPrefixMap)
            m_cpuPrefix->Unmap(0, nullptr);
        m_worldMap  = nullptr;
        m_indexMap  = nullptr;
        m_viewMap   = nullptr;
        m_kindMap   = nullptr;
        m_cpuPrefixMap = nullptr;
        m_worldGpu  = 0;
        m_indexGpu  = 0;
        m_viewGpu   = 0;
        m_kindGpu   = 0;
        m_cpuPrefixGpu = 0;
        m_worlds.Reset();
        m_indices.Reset();
        m_viewCb.Reset();
        m_kinds.Reset();
        m_cpuPrefix.Reset();
        m_gbPso.Reset();
        m_gbPsoTwoSided.Reset();
        m_gbThermal.Reset();
        m_gbThermalTwoSided.Reset();
        m_gbUv.Reset();
        m_gbUvTwoSided.Reset();
        m_thermal = false;
        m_ultraviolet = false;
        m_gbRoot.Reset();
        m_depthPso.Reset();
        m_depthPsoTwoSided.Reset();
        m_depthRoot.Reset();
        m_cpuWorlds.clear();
        m_cpuKinds.clear();
        for (int i = 0; i < kKindCount; ++i)
        {
            for (uint32_t variant = 0; variant < Terrain::kMaxFoliageVariants; ++variant)
                m_cpuIndex[i][variant].clear();
        }
        m_cpuGen       = 1;
        m_worldSlotGen[0] = 0;
        m_worldSlotGen[1] = 0;
        m_kindSlotGen[0]  = 0;
        m_kindSlotGen[1]  = 0;
        m_frameIndex   = ~0u;
        m_viewCursor   = 0;
        m_activeSlot   = 0;
        m_activeView   = 0;
        m_gpuCull      = false;
        m_depthCullPending = false;
        m_cpuUseSphere = false;
        for (int i = 0; i < kKindCount; ++i)
        {
            m_kindResident[i] = 0;
            m_kindValid[i]    = 0;
        }
        if (m_cull)
        {
            if (m_cull->cb && m_cull->cbMap)
                m_cull->cb->Unmap(0, nullptr);
            if (m_cull->variantInfo && m_cull->variantMap)
                m_cull->variantInfo->Unmap(0, nullptr);
            m_cull->cbMap      = nullptr;
            m_cull->variantMap = nullptr;
            if (m_renderer)
            {
                m_renderer->deferRelease(m_cull->root.Get());
                m_renderer->deferRelease(m_cull->compact.Get());
                m_renderer->deferRelease(m_cull->prefixPso.Get());
                m_renderer->deferRelease(m_cull->scatterPso.Get());
                m_renderer->deferRelease(m_cull->argsPso.Get());
                m_renderer->deferRelease(m_cull->signature.Get());
                m_renderer->deferRelease(m_cull->heap.Get());
                m_renderer->deferRelease(m_cull->cb.Get());
                m_renderer->deferRelease(m_cull->variantInfo.Get());
                m_renderer->deferRelease(m_cull->depthHeap.Get());
                for (uint32_t slot = 0; slot < kCullFrameSlots; ++slot)
                {
                    m_renderer->deferRelease(m_cull->indices[slot].Get());
                    m_renderer->deferRelease(m_cull->counters[slot].Get());
                    m_renderer->deferRelease(m_cull->args[slot].Get());
                    m_renderer->deferRelease(m_cull->prefix[slot].Get());
                    m_renderer->deferRelease(m_cull->cursor[slot].Get());
                    m_renderer->deferRelease(m_cull->depthIndices[slot].Get());
                    m_renderer->deferRelease(m_cull->depthCounters[slot].Get());
                    m_renderer->deferRelease(m_cull->depthArgs[slot].Get());
                    m_renderer->deferRelease(m_cull->depthPrefix[slot].Get());
                    m_renderer->deferRelease(m_cull->depthCursor[slot].Get());
                }
            }
        }
        m_cull.reset();
        m_retained.clear();
        m_renderer = nullptr;
        m_haveGather   = false;
        m_sourceStamp  = 0;
        m_loggedView   = false;
        m_editorTiles.clear();
        m_binnedData      = nullptr;
        m_binnedCount     = 0;
        m_binnedTilesX    = 0;
        m_binnedTilesZ    = 0;
        m_binnedTileWorld = 0.0f;
        m_binnedOriginX   = 0.0f;
        m_binnedOriginZ   = 0.0f;
        m_binnedFrontX    = 0;
        m_binnedFrontY    = 0;
        m_binnedBackZ     = 0;
        m_binnedFrontKind = 0;
    }

    bool FoliagePipeline::createCull(ID3D12Device* device)
    {
        m_cull.reset();
        m_kindSlotGen[0] = 0;
        m_kindSlotGen[1] = 0;
        if (!device)
            return logFoliageCullUnavailable();

        std::unique_ptr<FoliageCullGpu> cull = std::make_unique<FoliageCullGpu>();
        cull->device                          = device;

        D3D12_ROOT_PARAMETER params[9]{};
        params[kCullConstants].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[kCullConstants].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kCullConstants].Descriptor.ShaderRegister = 0;
        params[kCullConstants].Descriptor.RegisterSpace  = 0;

        params[kCullWorlds].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kCullWorlds].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kCullWorlds].Descriptor.ShaderRegister = 0;
        params[kCullWorlds].Descriptor.RegisterSpace  = 0;

        params[kCullKinds].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kCullKinds].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kCullKinds].Descriptor.ShaderRegister = 1;
        params[kCullKinds].Descriptor.RegisterSpace  = 0;

        params[kCullIndices].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kCullIndices].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kCullIndices].Descriptor.ShaderRegister = 0;
        params[kCullIndices].Descriptor.RegisterSpace  = 0;

        params[kCullCounters].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kCullCounters].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kCullCounters].Descriptor.ShaderRegister = 1;
        params[kCullCounters].Descriptor.RegisterSpace  = 0;

        params[kCullArgs].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kCullArgs].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kCullArgs].Descriptor.ShaderRegister = 2;
        params[kCullArgs].Descriptor.RegisterSpace  = 0;

        params[kCullPrefix].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kCullPrefix].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kCullPrefix].Descriptor.ShaderRegister = 3;
        params[kCullPrefix].Descriptor.RegisterSpace  = 0;

        params[kCullCursor].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kCullCursor].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kCullCursor].Descriptor.ShaderRegister = 4;
        params[kCullCursor].Descriptor.RegisterSpace  = 0;

        params[kCullVariant].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kCullVariant].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kCullVariant].Descriptor.ShaderRegister = 2;
        params[kCullVariant].Descriptor.RegisterSpace  = 0;

        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 9;
        rootDesc.pParameters   = params;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (foliage cull)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Foliage cull root signature: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return logFoliageCullUnavailable();
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&cull->root)), "CreateRootSignature (foliage cull)"))
            return logFoliageCullUnavailable();

        ComPtr<ID3DBlob> compact;
        ComPtr<ID3DBlob> prefix;
        ComPtr<ID3DBlob> scatter;
        ComPtr<ID3DBlob> args;
        if (!compileShaderFromContent("shaders/FoliageCull.hlsl", "CSCompact", "cs_5_0", compact)
            || !compileShaderFromContent("shaders/FoliageCull.hlsl", "CSPrefix", "cs_5_0", prefix)
            || !compileShaderFromContent("shaders/FoliageCull.hlsl", "CSScatter", "cs_5_0", scatter)
            || !compileShaderFromContent("shaders/FoliageCull.hlsl", "CSArgs", "cs_5_0", args))
            return logFoliageCullUnavailable();

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = cull->root.Get();
        pso.CS             = { compact->GetBufferPointer(), compact->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&cull->compact)), "CreateComputePipelineState (foliage cull compact)"))
            return logFoliageCullUnavailable();
        pso.CS = { prefix->GetBufferPointer(), prefix->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&cull->prefixPso)), "CreateComputePipelineState (foliage cull prefix)"))
            return logFoliageCullUnavailable();
        pso.CS = { scatter->GetBufferPointer(), scatter->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&cull->scatterPso)), "CreateComputePipelineState (foliage cull scatter)"))
            return logFoliageCullUnavailable();
        pso.CS = { args->GetBufferPointer(), args->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&cull->argsPso)), "CreateComputePipelineState (foliage cull args)"))
            return logFoliageCullUnavailable();
        cull->compact->SetName(L"DE.Foliage.CullCompact");
        cull->prefixPso->SetName(L"DE.Foliage.CullPrefix");
        cull->scatterPso->SetName(L"DE.Foliage.CullScatter");
        cull->argsPso->SetName(L"DE.Foliage.CullArgs");

        D3D12_INDIRECT_ARGUMENT_DESC argument{};
        argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        D3D12_COMMAND_SIGNATURE_DESC signature{};
        signature.ByteStride       = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        signature.NumArgumentDescs = 1;
        signature.pArgumentDescs   = &argument;
        if (FailedHr(device->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&cull->signature)), "CreateCommandSignature (foliage)"))
            return logFoliageCullUnavailable();
        cull->signature->SetName(L"DE.Foliage.CommandSignature");

        // ClearUnorderedAccessViewUint reads a CPU descriptor and clears through a GPU one.
        // The CPU handle must be non-shader-visible. The GPU handle must sit in the heap bound on this list.
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = kCullFrameSlots * 2;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&cull->heap)), "CreateDescriptorHeap (foliage cull)"))
            return logFoliageCullUnavailable();
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&cull->cpuHeap)), "CreateDescriptorHeap (foliage cull CPU)"))
            return logFoliageCullUnavailable();

        const UINT64 indexBytes   = static_cast<UINT64>(kCullViewsPerFrame) * kCullKindCount * Terrain::kMaxFoliageDraw * sizeof(uint32_t);
        const UINT64 counterBytes = static_cast<UINT64>(kVariantSlots) * sizeof(uint32_t);
        const UINT64 argBytes     = static_cast<UINT64>(kCullViewsPerFrame) * kCullKindCount * Terrain::kMaxFoliageVariants * kCullPartsPerKind * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        const UINT64 prefixBytes  = static_cast<UINT64>(kVariantSlots) * sizeof(uint32_t);
        const wchar_t* indexNames[]   = { L"DE.Foliage.CullIndices0", L"DE.Foliage.CullIndices1" };
        const wchar_t* counterNames[] = { L"DE.Foliage.CullCounters0", L"DE.Foliage.CullCounters1" };
        const wchar_t* argNames[]     = { L"DE.Foliage.CullArgs0", L"DE.Foliage.CullArgs1" };

        const UINT                  increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE gpuCpu    = cull->heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE cpu       = cull->cpuHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpu       = cull->heap->GetGPUDescriptorHandleForHeapStart();
        for (uint32_t slot = 0; slot < kCullFrameSlots; ++slot)
        {
            cull->indices[slot]  = createUav(device, indexBytes, "CreateCommittedResource (foliage cull indices)");
            cull->counters[slot] = createUav(device, counterBytes, "CreateCommittedResource (foliage cull counters)");
            cull->args[slot]     = createUav(device, argBytes, "CreateCommittedResource (foliage cull args)");
            cull->prefix[slot]   = createUav(device, prefixBytes, "CreateCommittedResource (foliage cull prefix)");
            cull->cursor[slot]   = createUav(device, prefixBytes, "CreateCommittedResource (foliage cull cursor)");
            if (!cull->indices[slot] || !cull->counters[slot] || !cull->args[slot] || !cull->prefix[slot] || !cull->cursor[slot])
                return logFoliageCullUnavailable();
            cull->indices[slot]->SetName(indexNames[slot]);
            cull->counters[slot]->SetName(counterNames[slot]);
            cull->args[slot]->SetName(argNames[slot]);

            cull->cpu[slot] = cpu;
            cull->gpu[slot] = gpu;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format              = DXGI_FORMAT_R32_UINT;
            uav.ViewDimension       = D3D12_UAV_DIMENSION_BUFFER;
            uav.Buffer.FirstElement = 0;
            uav.Buffer.NumElements  = kVariantSlots;
            device->CreateUnorderedAccessView(cull->counters[slot].Get(), nullptr, &uav, gpuCpu);
            device->CreateUnorderedAccessView(cull->counters[slot].Get(), nullptr, &uav, cpu);
            gpuCpu.ptr += increment;
            cpu.ptr += increment;
            gpu.ptr += increment;
        }
        for (uint32_t slot = 0; slot < kCullFrameSlots; ++slot)
        {
            cull->cursorCpu[slot] = cpu;
            cull->cursorGpu[slot] = gpu;
            D3D12_UNORDERED_ACCESS_VIEW_DESC cursorUav{};
            cursorUav.Format              = DXGI_FORMAT_R32_UINT;
            cursorUav.ViewDimension       = D3D12_UAV_DIMENSION_BUFFER;
            cursorUav.Buffer.FirstElement = 0;
            cursorUav.Buffer.NumElements  = kVariantSlots;
            device->CreateUnorderedAccessView(cull->cursor[slot].Get(), nullptr, &cursorUav, gpuCpu);
            device->CreateUnorderedAccessView(cull->cursor[slot].Get(), nullptr, &cursorUav, cpu);
            gpuCpu.ptr += increment;
            cpu.ptr += increment;
            gpu.ptr += increment;
        }

        const UINT64 variantBytes = kVariantInfoStride * kCullViewsPerFrame * kCullFrameSlots;
        cull->variantInfo = createUpload(device, variantBytes, "CreateCommittedResource (foliage cull variants)");
        cull->cb          = createUpload(device, kCullCbStride * kCullViewsPerFrame * kCullFrameSlots, "CreateCommittedResource (foliage cull constants)");
        if (!cull->variantInfo || !cull->cb)
            return logFoliageCullUnavailable();
        if (FailedHr(cull->variantInfo->Map(0, nullptr, reinterpret_cast<void**>(&cull->variantMap)), "Map foliage variants")
            || FailedHr(cull->cb->Map(0, nullptr, reinterpret_cast<void**>(&cull->cbMap)), "Map foliage cull constants"))
        {
            if (cull->variantMap)
                cull->variantInfo->Unmap(0, nullptr);
            if (cull->cbMap)
                cull->cb->Unmap(0, nullptr);
            cull->variantMap = nullptr;
            cull->cbMap      = nullptr;
            return logFoliageCullUnavailable();
        }
        std::memset(cull->variantMap, 0, static_cast<size_t>(variantBytes));
        std::memset(cull->cbMap, 0, static_cast<size_t>(kCullCbStride * kCullViewsPerFrame * kCullFrameSlots));
        cull->variantGpu = cull->variantInfo->GetGPUVirtualAddress();
        cull->cbGpu      = cull->cb->GetGPUVirtualAddress();
        cull->variantInfo->SetName(L"DE.Foliage.VariantInfo");
        cull->cb->SetName(L"DE.Foliage.CullConstants");

        // Depth set failure leaves the camera cull working. The shadow worker refuses until it exists.
        {
            D3D12_DESCRIPTOR_HEAP_DESC depthHeapDesc{};
            depthHeapDesc.NumDescriptors = kCullFrameSlots * 2;
            depthHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            depthHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            const bool heapOk = !FailedHr(device->CreateDescriptorHeap(&depthHeapDesc, IID_PPV_ARGS(&cull->depthHeap)), "CreateDescriptorHeap (foliage cull depth)");
            depthHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            const bool cpuOk = heapOk && !FailedHr(device->CreateDescriptorHeap(&depthHeapDesc, IID_PPV_ARGS(&cull->depthCpuHeap)), "CreateDescriptorHeap (foliage cull depth CPU)");
            bool buffersOk = cpuOk;
            if (buffersOk)
            {
                const UINT depthIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                D3D12_CPU_DESCRIPTOR_HANDLE depthGpuCpu = cull->depthHeap->GetCPUDescriptorHandleForHeapStart();
                D3D12_CPU_DESCRIPTOR_HANDLE depthCpu    = cull->depthCpuHeap->GetCPUDescriptorHandleForHeapStart();
                D3D12_GPU_DESCRIPTOR_HANDLE depthGpu    = cull->depthHeap->GetGPUDescriptorHandleForHeapStart();
                for (uint32_t slot = 0; slot < kCullFrameSlots; ++slot)
                {
                    cull->depthIndices[slot]  = createUav(device, indexBytes, "CreateCommittedResource (foliage cull depth indices)");
                    cull->depthCounters[slot] = createUav(device, counterBytes, "CreateCommittedResource (foliage cull depth counters)");
                    cull->depthArgs[slot]     = createUav(device, argBytes, "CreateCommittedResource (foliage cull depth args)");
                    cull->depthPrefix[slot]   = createUav(device, prefixBytes, "CreateCommittedResource (foliage cull depth prefix)");
                    cull->depthCursor[slot]   = createUav(device, prefixBytes, "CreateCommittedResource (foliage cull depth cursor)");
                    if (!cull->depthIndices[slot] || !cull->depthCounters[slot] || !cull->depthArgs[slot] || !cull->depthPrefix[slot] || !cull->depthCursor[slot]
                        || cull->depthIndices[slot].Get() == cull->indices[slot].Get()
                        || cull->depthArgs[slot].Get() == cull->args[slot].Get()
                        || cull->depthPrefix[slot].Get() == cull->prefix[slot].Get()
                        || cull->depthCursor[slot].Get() == cull->cursor[slot].Get())
                    {
                        buffersOk = false;
                        break;
                    }
                    cull->depthCpu[slot] = depthCpu;
                    cull->depthGpu[slot] = depthGpu;
                    D3D12_UNORDERED_ACCESS_VIEW_DESC depthUav{};
                    depthUav.Format              = DXGI_FORMAT_R32_UINT;
                    depthUav.ViewDimension       = D3D12_UAV_DIMENSION_BUFFER;
                    depthUav.Buffer.FirstElement = 0;
                    depthUav.Buffer.NumElements  = kVariantSlots;
                    device->CreateUnorderedAccessView(cull->depthCounters[slot].Get(), nullptr, &depthUav, depthGpuCpu);
                    device->CreateUnorderedAccessView(cull->depthCounters[slot].Get(), nullptr, &depthUav, depthCpu);
                    depthGpuCpu.ptr += depthIncrement;
                    depthCpu.ptr += depthIncrement;
                    depthGpu.ptr += depthIncrement;
                    cull->depthArgState[slot]    = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    cull->depthIndexState[slot]  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    cull->depthPrefixState[slot] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                }
                if (buffersOk)
                {
                    for (uint32_t slot = 0; slot < kCullFrameSlots; ++slot)
                    {
                        cull->depthCursorCpu[slot] = depthCpu;
                        cull->depthCursorGpu[slot] = depthGpu;
                        D3D12_UNORDERED_ACCESS_VIEW_DESC cursorUav{};
                        cursorUav.Format              = DXGI_FORMAT_R32_UINT;
                        cursorUav.ViewDimension       = D3D12_UAV_DIMENSION_BUFFER;
                        cursorUav.Buffer.FirstElement = 0;
                        cursorUav.Buffer.NumElements  = kVariantSlots;
                        device->CreateUnorderedAccessView(cull->depthCursor[slot].Get(), nullptr, &cursorUav, depthGpuCpu);
                        device->CreateUnorderedAccessView(cull->depthCursor[slot].Get(), nullptr, &cursorUav, depthCpu);
                        depthGpuCpu.ptr += depthIncrement;
                        depthCpu.ptr += depthIncrement;
                        depthGpu.ptr += depthIncrement;
                    }
                }
            }
            if (!buffersOk)
            {
                cull->depthHeap.Reset();
                cull->depthCpuHeap.Reset();
                for (uint32_t slot = 0; slot < kCullFrameSlots; ++slot)
                {
                    cull->depthIndices[slot].Reset();
                    cull->depthCounters[slot].Reset();
                    cull->depthArgs[slot].Reset();
                    cull->depthPrefix[slot].Reset();
                    cull->depthCursor[slot].Reset();
                }
                DE_LOG_WARN(LogCategory::Render, "FoliagePipeline: depth cull set unavailable; shadow worker stays on the camera set");
            }
            else
            {
                cull->depthIndices[0]->SetName(L"DE.Foliage.CullIndicesDepth0");
                cull->depthIndices[1]->SetName(L"DE.Foliage.CullIndicesDepth1");
                cull->depthCounters[0]->SetName(L"DE.Foliage.CullCountersDepth0");
                cull->depthCounters[1]->SetName(L"DE.Foliage.CullCountersDepth1");
                cull->depthArgs[0]->SetName(L"DE.Foliage.CullArgsDepth0");
                cull->depthArgs[1]->SetName(L"DE.Foliage.CullArgsDepth1");
                cull->depthPrefix[0]->SetName(L"DE.Foliage.CullPrefixDepth0");
                cull->depthPrefix[1]->SetName(L"DE.Foliage.CullPrefixDepth1");
                cull->depthCursor[0]->SetName(L"DE.Foliage.CullCursorDepth0");
                cull->depthCursor[1]->SetName(L"DE.Foliage.CullCursorDepth1");
            }
        }

        cull->ready = true;
        m_cull      = std::move(cull);
        return true;
    }

    bool FoliagePipeline::cullReady() const
    {
        return m_cull && m_cull->ready;
    }

    ID3D12Resource* FoliagePipeline::cullArgumentResource(uint32_t frameSlot) const
    {
        if (!m_cull || frameSlot >= kCullFrameSlots)
            return nullptr;
        return m_cull->args[frameSlot].Get();
    }

    ID3D12Resource* FoliagePipeline::cullIndexResource(uint32_t frameSlot) const
    {
        if (!m_cull || frameSlot >= kCullFrameSlots)
            return nullptr;
        return m_cull->indices[frameSlot].Get();
    }

    bool FoliagePipeline::depthResourceSetReady() const
    {
        if (!m_cull || !m_cull->ready || !m_cull->depthHeap || !m_cull->depthCpuHeap)
            return false;
        for (uint32_t slot = 0; slot < kCullFrameSlots; ++slot)
        {
            if (!m_cull->depthIndices[slot] || !m_cull->depthCounters[slot] || !m_cull->depthArgs[slot] || !m_cull->depthPrefix[slot] || !m_cull->depthCursor[slot])
                return false;
            if (m_cull->depthIndices[slot].Get() == m_cull->indices[slot].Get())
                return false;
            if (m_cull->depthArgs[slot].Get() == m_cull->args[slot].Get())
                return false;
            if (m_cull->depthCounters[slot].Get() == m_cull->counters[slot].Get())
                return false;
            if (m_cull->depthPrefix[slot].Get() == m_cull->prefix[slot].Get())
                return false;
            if (m_cull->depthCursor[slot].Get() == m_cull->cursor[slot].Get())
                return false;
        }
        return true;
    }

    bool FoliagePipeline::recordCull(ID3D12GraphicsCommandList* cmd, uint32_t frameSlot, const FoliageCullDesc& desc, CullResourceSet set)
    {
        if (!cmd || !cullReady() || frameSlot >= kCullFrameSlots || desc.worlds == 0 || desc.kinds == 0)
            return false;
        if (desc.viewIndex >= kCullViewsPerFrame || desc.instanceCount == 0)
            return false;
        if (set == CullResourceSet::Depth && !depthResourceSetReady())
            return false;
        for (uint32_t kind = 0; kind < kCullKindCount; ++kind)
        {
            if (desc.partCount[kind] > kCullPartsPerKind)
                return false;
            for (uint32_t variant = 0; variant < Terrain::kMaxFoliageVariants; ++variant)
            {
                if (desc.variantPartCount[kind][variant] > kCullPartsPerKind)
                    return false;
            }
        }

        FoliageCullGpu& cull = *m_cull;
        const bool depth = set == CullResourceSet::Depth;
        D3D12_RESOURCE_STATES& argState    = depth ? cull.depthArgState[frameSlot] : cull.argState[frameSlot];
        D3D12_RESOURCE_STATES& indexState  = depth ? cull.depthIndexState[frameSlot] : cull.indexState[frameSlot];
        D3D12_RESOURCE_STATES& prefixState = depth ? cull.depthPrefixState[frameSlot] : cull.prefixState[frameSlot];
        if (!foliageArgWritable(argState) || !foliageIndexWritable(indexState) || !foliageIndexWritable(prefixState))
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                DE_LOG_ERROR(LogCategory::Render, "FoliagePipeline: foliage cull skipped; buffer state is not a compute source");
            }
            return false;
        }

        uint32_t instanceCount = desc.instanceCount;
        if (instanceCount > Terrain::kMaxFoliageDraw)
            instanceCount = Terrain::kMaxFoliageDraw;

        FoliageCullConstants constants{};
        for (int plane = 0; plane < 6; ++plane)
        {
            constants.planes[plane][0] = desc.planes[plane][0];
            constants.planes[plane][1] = desc.planes[plane][1];
            constants.planes[plane][2] = desc.planes[plane][2];
            constants.planes[plane][3] = desc.planes[plane][3];
        }
        for (int axis = 0; axis < 4; ++axis)
            constants.sphere[axis] = desc.sphere[axis];
        constants.instanceCount = instanceCount;
        constants.useSphere     = desc.useSphere;
        constants.shadowView    = desc.shadowView;
        constants.viewIndex     = desc.viewIndex;
        for (uint32_t kind = 0; kind < kCullKindCount; ++kind)
        {
            uint32_t resident = desc.kindResident[kind];
            if (resident > instanceCount)
                resident = instanceCount;
            constants.kindResident[kind] = resident;
            constants.partCount[kind]    = desc.partCount[kind];
            constants.boundsMin[kind][0] = desc.boundsMin[kind][0];
            constants.boundsMin[kind][1] = desc.boundsMin[kind][1];
            constants.boundsMin[kind][2] = desc.boundsMin[kind][2];
            constants.boundsMin[kind][3] = desc.kindValid[kind] != 0 ? 1.0f : 0.0f;
            constants.boundsMax[kind][0] = desc.boundsMax[kind][0];
            constants.boundsMax[kind][1] = desc.boundsMax[kind][1];
            constants.boundsMax[kind][2] = desc.boundsMax[kind][2];
            constants.boundsMax[kind][3] = m_sway[kind];
            for (uint32_t part = 0; part < kCullPartsPerKind; ++part)
                constants.indexCount[kind * kCullPartsPerKind + part] = desc.indexCount[kind][part];
        }

        const UINT64 cbOff = (static_cast<UINT64>(frameSlot) * kCullViewsPerFrame + desc.viewIndex) * kCullCbStride;
        std::memcpy(cull.cbMap + cbOff, &constants, sizeof(constants));
        if (!cull.variantMap)
            return false;
        // Callers that only fill the variant-0 cbuffer fields still draw. A non-zero side table wins.
        const UINT64 varOff = (static_cast<UINT64>(frameSlot) * kCullViewsPerFrame + desc.viewIndex) * kVariantInfoStride;
        uint32_t* variantTable = reinterpret_cast<uint32_t*>(cull.variantMap + varOff);
        std::memset(variantTable, 0, static_cast<size_t>(kVariantSlots) * 9u * sizeof(uint32_t));
        for (uint32_t kind = 0; kind < kCullKindCount; ++kind)
        {
            bool authored = false;
            for (uint32_t variant = 0; variant < Terrain::kMaxFoliageVariants; ++variant)
            {
                if (desc.variantPartCount[kind][variant] != 0)
                    authored = true;
                for (uint32_t part = 0; part < kCullPartsPerKind; ++part)
                {
                    if (desc.variantIndexCount[kind][variant][part] != 0)
                        authored = true;
                }
            }
            for (uint32_t variant = 0; variant < Terrain::kMaxFoliageVariants; ++variant)
            {
                uint32_t parts = 0;
                if (!authored)
                {
                    if (variant != 0)
                        continue;
                    parts = desc.partCount[kind];
                }
                else
                    parts = desc.variantPartCount[kind][variant];
                if (parts > kCullPartsPerKind)
                    parts = kCullPartsPerKind;
                const uint32_t base = (kind * Terrain::kMaxFoliageVariants + variant) * 9u;
                variantTable[base] = parts;
                for (uint32_t part = 0; part < parts; ++part)
                    variantTable[base + 1u + part] = authored ? desc.variantIndexCount[kind][variant][part] : desc.indexCount[kind][part];
            }
        }

        ID3D12Resource* indices  = (depth ? cull.depthIndices[frameSlot] : cull.indices[frameSlot]).Get();
        ID3D12Resource* counters = (depth ? cull.depthCounters[frameSlot] : cull.counters[frameSlot]).Get();
        ID3D12Resource* args     = (depth ? cull.depthArgs[frameSlot] : cull.args[frameSlot]).Get();
        ID3D12Resource* prefix   = (depth ? cull.depthPrefix[frameSlot] : cull.prefix[frameSlot]).Get();
        ID3D12Resource* cursor   = (depth ? cull.depthCursor[frameSlot] : cull.cursor[frameSlot]).Get();
        if (!indices || !counters || !args || !prefix || !cursor)
            return false;
        if (argState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            transitionBuffer(cmd, args, argState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            argState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (indexState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            transitionBuffer(cmd, indices, indexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            indexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (prefixState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            transitionBuffer(cmd, prefix, prefixState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            prefixState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        ID3D12DescriptorHeap* heap = depth ? cull.depthHeap.Get() : cull.heap.Get();
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = depth ? cull.depthGpu[frameSlot] : cull.gpu[frameSlot];
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = depth ? cull.depthCpu[frameSlot] : cull.cpu[frameSlot];
        D3D12_GPU_DESCRIPTOR_HANDLE cursorGpu = depth ? cull.depthCursorGpu[frameSlot] : cull.cursorGpu[frameSlot];
        D3D12_CPU_DESCRIPTOR_HANDLE cursorCpu = depth ? cull.depthCursorCpu[frameSlot] : cull.cursorCpu[frameSlot];
        ID3D12DescriptorHeap* heaps[] = { heap };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetComputeRootSignature(cull.root.Get());
        cmd->SetComputeRootConstantBufferView(kCullConstants, cull.cbGpu + cbOff);
        cmd->SetComputeRootShaderResourceView(kCullWorlds, desc.worlds);
        cmd->SetComputeRootShaderResourceView(kCullKinds, desc.kinds);
        cmd->SetComputeRootUnorderedAccessView(kCullIndices, indices->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(kCullCounters, counters->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(kCullArgs, args->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(kCullPrefix, prefix->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(kCullCursor, cursor->GetGPUVirtualAddress());
        cmd->SetComputeRootShaderResourceView(kCullVariant, cull.variantGpu + varOff);

        const UINT zeros[4] = { 0, 0, 0, 0 };
        cmd->ClearUnorderedAccessViewUint(gpuHandle, cpuHandle, counters, zeros, 0, nullptr);
        uavBarrier(cmd, counters);

        cmd->SetPipelineState(cull.compact.Get());
        const UINT groups = (instanceCount + 63u) / 64u;
        cmd->Dispatch(groups, 1, 1);
        uavBarrier(cmd, counters);

        cmd->SetPipelineState(cull.prefixPso.Get());
        cmd->Dispatch(1, 1, 1);
        uavBarrier(cmd, prefix);

        cmd->ClearUnorderedAccessViewUint(cursorGpu, cursorCpu, cursor, zeros, 0, nullptr);
        uavBarrier(cmd, cursor);

        cmd->SetPipelineState(cull.scatterPso.Get());
        cmd->Dispatch(groups, 1, 1);
        uavBarrier(cmd, indices);
        uavBarrier(cmd, prefix);

        cmd->SetPipelineState(cull.argsPso.Get());
        cmd->Dispatch(1, 1, 1);

        uavBarrier(cmd, args);
        uavBarrier(cmd, indices);
        uavBarrier(cmd, prefix);
        transitionBuffer(cmd, args, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        transitionBuffer(cmd, indices, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        transitionBuffer(cmd, prefix, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        argState    = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        indexState  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        prefixState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        return true;
    }

    void FoliagePipeline::binEditorRecords(const Terrain::TerrainGrid& grid, const std::vector<Terrain::FoliageRecord>& records)
    {
        const int   tilesX    = static_cast<int>(grid.tilesX());
        const int   tilesZ    = static_cast<int>(grid.tilesZ());
        const float tileWorld = static_cast<float>(grid.tileCells()) * grid.cellSize();
        const float ox        = grid.origin().x;
        const float oz        = grid.origin().z;
        uint32_t    frontX    = 0;
        uint32_t    frontY    = 0;
        uint32_t    backZ     = 0;
        uint8_t     frontKind = 0;
        if (!records.empty())
        {
            const Terrain::FoliageRecord& a = records.front();
            const Terrain::FoliageRecord& b = records.back();
            std::memcpy(&frontX, &a.x, sizeof(frontX));
            std::memcpy(&frontY, &a.y, sizeof(frontY));
            std::memcpy(&backZ, &b.z, sizeof(backZ));
            frontKind = a.kind;
        }
        const bool endsSame = records.empty() || (m_binnedFrontX == frontX && m_binnedFrontY == frontY && m_binnedBackZ == backZ && m_binnedFrontKind == frontKind);
        if (m_binnedData == records.data() && m_binnedCount == records.size() && m_binnedTilesX == tilesX && m_binnedTilesZ == tilesZ && m_binnedTileWorld == tileWorld
            && m_binnedOriginX == ox && m_binnedOriginZ == oz && endsSame && static_cast<int>(m_editorTiles.size()) == tilesX * tilesZ)
            return;

        m_binnedData      = records.data();
        m_binnedCount     = records.size();
        m_binnedTilesX    = tilesX;
        m_binnedTilesZ    = tilesZ;
        m_binnedTileWorld = tileWorld;
        m_binnedOriginX   = ox;
        m_binnedOriginZ   = oz;
        m_binnedFrontX    = frontX;
        m_binnedFrontY    = frontY;
        m_binnedBackZ     = backZ;
        m_binnedFrontKind = frontKind;
        const size_t nTiles = (tilesX > 0 && tilesZ > 0) ? static_cast<size_t>(tilesX) * static_cast<size_t>(tilesZ) : 0u;
        m_editorTiles.assign(nTiles, {});
        for (size_t i = 0; i < records.size(); ++i)
        {
            int tx = 0;
            int tz = 0;
            if (!recordTile(grid, records[i].x, records[i].z, tx, tz))
                continue;
            m_editorTiles[static_cast<size_t>(tz) * static_cast<size_t>(tilesX) + static_cast<size_t>(tx)].push_back(static_cast<uint32_t>(i));
        }
    }

    void FoliagePipeline::ensureDrawSet(const Terrain::TerrainGrid& grid, const std::vector<Terrain::FoliageRecord>* editorRecords, const Camera3D& camera, const FoliagePrototypes& prototypes)
    {
        const Math::Vector3f cam   = camera.GetPosition();
        const uint64_t       stamp = sourceStamp(grid, editorRecords);
        bool                 need  = !m_haveGather || stamp != m_sourceStamp;
        if (!need)
        {
            const float dx = cam.x - m_gatherX;
            const float dz = cam.z - m_gatherZ;
            need            = dx * dx + dz * dz >= kGatherMoveM * kGatherMoveM;
        }
        if (!need)
            return;

        if (editorRecords)
            binEditorRecords(grid, *editorRecords);
        const std::vector<std::vector<uint32_t>>* editorTiles = editorRecords ? &m_editorTiles : nullptr;

        const float radius2 = kGatherRadiusM * kGatherRadiusM;
        auto inDisk = [&](const Terrain::FoliageRecord& rec)
        {
            if (rec.kind >= static_cast<uint8_t>(Terrain::FoliageKind::Count))
                return false;
            const float dx = rec.x - cam.x;
            const float dz = rec.z - cam.z;
            return dx * dx + dz * dz <= radius2;
        };

        uint64_t total = 0;
        visitResident(grid, editorRecords, editorTiles, [&](const Terrain::FoliageRecord& rec)
        {
            if (inDisk(rec))
                ++total;
        });

        m_cpuWorlds.clear();
        m_cpuKinds.clear();
        if (total > 0)
        {
            const uint64_t cap   = Terrain::kMaxFoliageDraw;
            const uint64_t keepN = total < cap ? total : cap;
            if (total > cap)
                DE_LOG_WARN(LogCategory::Render, "FoliagePipeline: gathered {} foliage, drawing {}", total, cap);
            m_cpuWorlds.reserve(static_cast<size_t>(keepN));
            m_cpuKinds.reserve(static_cast<size_t>(keepN));
            uint64_t acc = total / 2ull;
            visitResident(grid, editorRecords, editorTiles, [&](const Terrain::FoliageRecord& rec)
            {
                if (!inDisk(rec))
                    return;
                if (total > cap)
                {
                    acc += cap;
                    if (acc >= total)
                        acc -= total;
                    else
                        return;
                }
                // uploadKinds only copies the word. Variant and tint have to be packed while pad is still here.
                const auto kind = static_cast<Terrain::FoliageKind>(rec.kind);
                uint32_t variant = rec.pad[0];
                const uint32_t loaded = prototypes.loadedCount(kind);
                if (variant >= Terrain::kMaxFoliageVariants || variant >= loaded || (variant > 0 && !prototypes.variantReady(kind, static_cast<uint8_t>(variant))))
                    variant = 0;
                const uint16_t tint = static_cast<uint16_t>(rec.pad[1]) | (static_cast<uint16_t>(rec.pad[2]) << 8);
                m_cpuWorlds.push_back(instanceWorld(rec));
                m_cpuKinds.push_back(Terrain::packFoliageKind(rec.kind, static_cast<uint8_t>(variant), tint));
            });
        }

        m_haveGather  = true;
        m_sourceStamp = stamp;
        m_gatherX     = cam.x;
        m_gatherZ     = cam.z;
        ++m_cpuGen;
        if (m_cpuGen == 0)
            m_cpuGen = 1;
    }

    void FoliagePipeline::uploadWorlds(uint32_t frameSlot)
    {
        if (frameSlot >= kFrameCount)
            return;
        if (m_worldSlotGen[frameSlot] == m_cpuGen)
            return;
        if (!m_cpuWorlds.empty())
        {
            const size_t bytes = m_cpuWorlds.size() * sizeof(Math::Matrix4f);
            std::memcpy(m_worldMap + static_cast<size_t>(frameSlot) * static_cast<size_t>(kWorldStride), m_cpuWorlds.data(), bytes);
        }
        m_worldSlotGen[frameSlot] = m_cpuGen;
    }

    void FoliagePipeline::uploadKinds(uint32_t frameSlot)
    {
        if (!m_kindMap || frameSlot >= kFrameCount)
            return;
        if (m_kindSlotGen[frameSlot] == m_cpuGen)
            return;
        uint32_t* dst = reinterpret_cast<uint32_t*>(m_kindMap + static_cast<size_t>(frameSlot) * static_cast<size_t>(kKindStride));
        const size_t n = m_cpuKinds.size() < Terrain::kMaxFoliageDraw ? m_cpuKinds.size() : static_cast<size_t>(Terrain::kMaxFoliageDraw);
        if (n > 0)
            std::memcpy(dst, m_cpuKinds.data(), n * sizeof(uint32_t));
        m_kindSlotGen[frameSlot] = m_cpuGen;
    }

    void FoliagePipeline::snapshotKindBounds(FoliagePrototypes& prototypes)
    {
        for (int kind = 0; kind < kKindCount; ++kind)
        {
            m_kindResident[kind] = 0;
            m_kindValid[kind]    = 0;
            m_boundsMin[kind][0] = 0.0f;
            m_boundsMin[kind][1] = 0.0f;
            m_boundsMin[kind][2] = 0.0f;
            m_boundsMax[kind][0] = 0.0f;
            m_boundsMax[kind][1] = 0.0f;
            m_boundsMax[kind][2] = 0.0f;
            // One cull box per kind. Variants share it, so the box is their union.
            Math::AABox3f box = Math::AABox3f::Empty();
            bool any = false;
            uint32_t loaded = prototypes.loadedCount(kKindOrder[kind]);
            if (loaded == 0)
                loaded = 1;
            if (loaded > Terrain::kMaxFoliageVariants)
                loaded = Terrain::kMaxFoliageVariants;
            for (uint32_t variant = 0; variant < loaded; ++variant)
            {
                const AssetRef<Model>& model = prototypes.model(kKindOrder[kind], static_cast<uint8_t>(variant));
                if (!model || !model->bounds().IsValid())
                    continue;
                if (!any)
                    box = model->bounds();
                else
                    box.ExpandToInclude(model->bounds());
                any = true;
            }
            if (!any)
                continue;
            m_kindValid[kind]    = 1;
            m_boundsMin[kind][0] = box.Min.x;
            m_boundsMin[kind][1] = box.Min.y;
            m_boundsMin[kind][2] = box.Min.z;
            m_boundsMax[kind][0] = box.Max.x;
            m_boundsMax[kind][1] = box.Max.y;
            m_boundsMax[kind][2] = box.Max.z;
        }
        const size_t n = m_cpuWorlds.size() < Terrain::kMaxFoliageDraw ? m_cpuWorlds.size() : static_cast<size_t>(Terrain::kMaxFoliageDraw);
        for (size_t i = 0; i < n; ++i)
        {
            const int kind = static_cast<int>(Terrain::foliageKindIndex(m_cpuKinds[i]));
            if (kind < 0 || kind >= kKindCount || m_kindValid[kind] == 0)
                continue;
            ++m_kindResident[kind];
        }
    }

    bool FoliagePipeline::buildCpuIndices(FoliagePrototypes& prototypes)
    {
        for (int i = 0; i < kKindCount; ++i)
        {
            for (uint32_t variant = 0; variant < Terrain::kMaxFoliageVariants; ++variant)
                m_cpuIndex[i][variant].clear();
        }

        const size_t n = m_cpuWorlds.size();
        for (size_t i = 0; i < n; ++i)
        {
            const uint32_t packed = m_cpuKinds[i];
            const int kind = static_cast<int>(Terrain::foliageKindIndex(packed));
            if (kind < 0 || kind >= kKindCount)
                continue;
            uint32_t variant = (packed >> 8) & 0xFFu;
            if (variant >= Terrain::kMaxFoliageVariants)
                variant = 0;
            const AssetRef<Model>& model = prototypes.model(kKindOrder[kind], static_cast<uint8_t>(variant));
            if (!model || !model->bounds().IsValid())
                continue;
            Math::AABox3f box = model->bounds().Transformed(m_cpuWorlds[i]);
            const float sway = m_sway[kind];
            if (sway > 0.0f)
            {
                box.Min.x -= sway;
                box.Min.y -= sway;
                box.Min.z -= sway;
                box.Max.x += sway;
                box.Max.y += sway;
                box.Max.z += sway;
            }
            if (m_cpuUseSphere)
            {
                if (!box.Intersects(m_cpuSphere))
                    continue;
            }
            else if (!m_cpuFrustum.Intersects(box))
                continue;
            m_cpuIndex[kind][variant].push_back(static_cast<uint32_t>(i));
        }

        for (int i = 0; i < kKindCount; ++i)
        {
            for (uint32_t variant = 0; variant < Terrain::kMaxFoliageVariants; ++variant)
            {
                if (!m_cpuIndex[i][variant].empty())
                    return true;
            }
        }
        return false;
    }

    void FoliagePipeline::writeCpuIndexSpan()
    {
        if (!m_indexMap || !m_cpuPrefixMap)
            return;
        uint8_t* base = m_indexMap + (static_cast<size_t>(m_activeSlot) * kViewsPerFrame + m_activeView) * static_cast<size_t>(kIndexViewStride);
        uint32_t* prefix = reinterpret_cast<uint32_t*>(m_cpuPrefixMap + (static_cast<size_t>(m_activeSlot) * kViewsPerFrame + m_activeView) * static_cast<size_t>(kVariantInfoStride));
        std::memset(prefix, 0, static_cast<size_t>(kVariantSlots) * sizeof(uint32_t));
        for (int kind = 0; kind < kKindCount; ++kind)
        {
            uint32_t sum = 0;
            for (uint32_t variant = 0; variant < Terrain::kMaxFoliageVariants; ++variant)
            {
                prefix[static_cast<uint32_t>(kind) * Terrain::kMaxFoliageVariants + variant] = sum;
                const std::vector<uint32_t>& list = m_cpuIndex[kind][variant];
                if (list.empty() || sum >= Terrain::kMaxFoliageDraw)
                    continue;
                const uint32_t room = Terrain::kMaxFoliageDraw - sum;
                const uint32_t count = static_cast<uint32_t>(list.size()) < room ? static_cast<uint32_t>(list.size()) : room;
                std::memcpy(base + static_cast<size_t>(kind) * static_cast<size_t>(kIndexKindStride) + static_cast<size_t>(sum) * sizeof(uint32_t), list.data(), static_cast<size_t>(count) * sizeof(uint32_t));
                sum += count;
            }
        }
    }

    void FoliagePipeline::publishCpuIndices(FoliagePrototypes& prototypes)
    {
        buildCpuIndices(prototypes);
        writeCpuIndexSpan();
    }

    FoliageCullDesc FoliagePipeline::makeCullDesc(bool shadow) const
    {
        FoliageCullDesc desc{};
        desc.worlds = m_worldGpu + static_cast<UINT64>(m_activeSlot) * kWorldStride;
        desc.kinds  = m_kindGpu + static_cast<UINT64>(m_activeSlot) * kKindStride;
        uint32_t count = static_cast<uint32_t>(m_cpuWorlds.size());
        if (m_cpuWorlds.size() > Terrain::kMaxFoliageDraw)
            count = Terrain::kMaxFoliageDraw;
        desc.instanceCount = count;
        desc.useSphere     = m_cpuUseSphere ? 1u : 0u;
        desc.shadowView    = shadow ? 1u : 0u;
        desc.viewIndex     = m_activeView;
        if (m_cpuUseSphere)
        {
            desc.sphere[0] = m_cpuSphere.Center.x;
            desc.sphere[1] = m_cpuSphere.Center.y;
            desc.sphere[2] = m_cpuSphere.Center.z;
            desc.sphere[3] = m_cpuSphere.Radius;
        }
        for (int plane = 0; plane < 6; ++plane)
        {
            const Math::Plane4f& p = m_cpuFrustum.GetPlane(plane);
            desc.planes[plane][0]  = p.x;
            desc.planes[plane][1]  = p.y;
            desc.planes[plane][2]  = p.z;
            desc.planes[plane][3]  = p.w;
        }
        for (int kind = 0; kind < kKindCount; ++kind)
        {
            desc.kindResident[kind] = m_kindResident[kind];
            desc.kindValid[kind]    = m_kindValid[kind];
            desc.boundsMin[kind][0] = m_boundsMin[kind][0];
            desc.boundsMin[kind][1] = m_boundsMin[kind][1];
            desc.boundsMin[kind][2] = m_boundsMin[kind][2];
            desc.boundsMax[kind][0] = m_boundsMax[kind][0];
            desc.boundsMax[kind][1] = m_boundsMax[kind][1];
            desc.boundsMax[kind][2] = m_boundsMax[kind][2];
        }
        return desc;
    }

    bool FoliagePipeline::fillPartArgs(GpuResourceCache& gpu, FoliagePrototypes& prototypes, bool depth, FoliageCullDesc& desc)
    {
        for (int kind = 0; kind < kKindCount; ++kind)
        {
            desc.partCount[kind] = 0;
            for (uint32_t part = 0; part < kCullPartsPerKind; ++part)
                desc.indexCount[kind][part] = 0;
            for (uint32_t variant = 0; variant < Terrain::kMaxFoliageVariants; ++variant)
            {
                desc.variantPartCount[kind][variant] = 0;
                for (uint32_t part = 0; part < kCullPartsPerKind; ++part)
                    desc.variantIndexCount[kind][variant][part] = 0;
            }
            if (depth && kind == static_cast<int>(Terrain::FoliageKind::Grass))
                continue;
            if (m_kindResident[kind] == 0)
                continue;
            uint32_t loaded = prototypes.loadedCount(kKindOrder[kind]);
            if (loaded == 0)
                loaded = 1;
            if (loaded > Terrain::kMaxFoliageVariants)
                loaded = Terrain::kMaxFoliageVariants;
            for (uint32_t variant = 0; variant < loaded; ++variant)
            {
                const AssetRef<Model>& model = prototypes.model(kKindOrder[kind], static_cast<uint8_t>(variant));
                if (!model)
                    continue;
                uint32_t n = 0;
                const bool ok = forKindParts(gpu, model, depth, true, [&](const GpuModel::Part& part, const Model::Part*, uint32_t slot) {
                    desc.variantIndexCount[kind][variant][slot] = part.mesh.indexCount();
                    if (variant == 0)
                        desc.indexCount[kind][slot] = part.mesh.indexCount();
                    n = slot + 1;
                });
                if (!ok)
                    return logFoliagePartCap();
                desc.variantPartCount[kind][variant] = n;
                if (variant == 0)
                    desc.partCount[kind] = n;
            }
        }
        return true;
    }

    bool FoliagePipeline::tryGpuCull(ID3D12GraphicsCommandList* cmd, GpuResourceCache& gpu, FoliagePrototypes& prototypes, bool depth, bool shadow, bool profile)
    {
        if (!m_gpuCull || !m_cull)
            return false;
        FoliageCullDesc desc = makeCullDesc(shadow);
        if (!fillPartArgs(gpu, prototypes, depth, desc))
        {
            publishCpuIndices(prototypes);
            m_gpuCull = false;
            return false;
        }
        bool ok = false;
        if (profile)
        {
            const GpuScope cullScope(cmd, "Foliage Cull", ProfileColor::Terrain);
            ok = recordCull(cmd, m_activeSlot, desc);
        }
        else
            ok = recordCull(cmd, m_activeSlot, desc);
        if (!ok)
        {
            publishCpuIndices(prototypes);
            m_gpuCull = false;
            return false;
        }
        return true;
    }

    bool FoliagePipeline::allocView(uint32_t frameIndex)
    {
        const uint32_t slot = frameIndex % kFrameCount;
        if (m_frameIndex != frameIndex)
        {
            m_frameIndex = frameIndex;
            m_viewCursor = 0;
        }
        if (m_viewCursor >= kViewsPerFrame)
        {
            if (!m_loggedView)
            {
                DE_LOG_ERROR(LogCategory::Render, "FoliagePipeline: too many views in one frame");
                m_loggedView = true;
            }
            return false;
        }
        m_activeSlot = slot;
        m_activeView = m_viewCursor++;
        return true;
    }

    bool FoliagePipeline::prepare(
        Renderer& renderer,
        AssetManager& assets,
        FoliagePrototypes& prototypes,
        const Terrain::TerrainGrid& grid,
        const std::vector<Terrain::FoliageRecord>* editorRecords,
        const Terrain::FoliageDensity& density,
        const Camera3D& camera,
        const Frustum3f& frustum,
        const Math::Sphere3f* casterSphere)
    {
        // instancedIndirect replaces this view's box test with FoliageCull.hlsl. The 96 m gather stays on the CPU.
        m_gpuCull = false;
        if (!isValid() || !prototypes.ready() || !grid.valid())
            return false;

        prototypes.sync(renderer, assets, density);
        ensureDrawSet(grid, editorRecords, camera, prototypes);
        if (m_cpuWorlds.empty())
            return false;

        m_cpuFrustum   = frustum;
        m_cpuUseSphere = casterSphere != nullptr;
        if (casterSphere)
            m_cpuSphere = *casterSphere;

        for (int i = 0; i < kKindCount; ++i)
        {
            for (uint32_t variant = 0; variant < Terrain::kMaxFoliageVariants; ++variant)
                m_cpuIndex[i][variant].clear();
        }
        snapshotKindBounds(prototypes);

        const bool wantGpu = renderer.debugState().instancedIndirect && cullReady();
        if (wantGpu)
        {
            bool any = false;
            for (int i = 0; i < kKindCount; ++i)
                any = any || m_kindResident[i] > 0;
            if (!any)
                return false;
            if (!allocView(renderer.frameIndex()))
                return false;
            uploadWorlds(m_activeSlot);
            uploadKinds(m_activeSlot);
            m_gpuCull = true;
            return true;
        }

        if (!buildCpuIndices(prototypes))
            return false;
        if (!allocView(renderer.frameIndex()))
            return false;

        uploadWorlds(m_activeSlot);
        uploadKinds(m_activeSlot);
        writeCpuIndexSpan();
        return true;
    }

    void FoliagePipeline::drawGBuffer(
        ID3D12GraphicsCommandList* cmd,
        Renderer& renderer,
        AssetManager& assets,
        FoliagePrototypes& prototypes,
        const Terrain::TerrainGrid& grid,
        const std::vector<Terrain::FoliageRecord>* editorRecords,
        const Terrain::FoliageDensity& density,
        const Camera3D& camera,
        const Math::Matrix4f& viewProj,
        const Math::Matrix4f& prevViewProj,
        const Frustum3f& frustum)
    {
        m_renderer = &renderer;
        if (!cmd || !prepare(renderer, assets, prototypes, grid, editorRecords, density, camera, frustum, nullptr))
            return;
        if (!writeViewConstants(camera, &viewProj, &prevViewProj))
            return;

        GpuResourceCache& gpu = renderer.gpuResources();
        if (m_gpuCull)
            tryGpuCull(cmd, gpu, prototypes, false, false, true);

        cmd->SetGraphicsRootSignature(m_gbRoot.Get());
        cmd->SetGraphicsRootConstantBufferView(kGbView, viewConstantAddress());
        ID3D12PipelineState* gbPso = m_gbPso.Get();
        if (m_ultraviolet && m_gbUv)
            gbPso = m_gbUv.Get();
        else if (m_thermal && m_gbThermal)
            gbPso = m_gbThermal.Get();
        cmd->SetPipelineState(gbPso);

        const D3D12_GPU_VIRTUAL_ADDRESS worldVa   = m_worldGpu + static_cast<UINT64>(m_activeSlot) * kWorldStride;
        const D3D12_GPU_VIRTUAL_ADDRESS indexBase = m_indexGpu + (static_cast<UINT64>(m_activeSlot) * kViewsPerFrame + m_activeView) * kIndexViewStride;
        // SM5 SV_InstanceID ignores StartInstanceLocation, so the vertex shader adds this prefix.
        D3D12_GPU_VIRTUAL_ADDRESS prefixVa = m_cpuPrefixGpu + (static_cast<UINT64>(m_activeSlot) * kViewsPerFrame + m_activeView) * kVariantInfoStride;
        if (m_gpuCull && m_cull && m_cull->prefix[m_activeSlot])
            prefixVa = m_cull->prefix[m_activeSlot]->GetGPUVirtualAddress();
        cmd->SetGraphicsRootShaderResourceView(kGbKinds, m_kindGpu + static_cast<UINT64>(m_activeSlot) * kKindStride);
        cmd->SetGraphicsRootShaderResourceView(kGbPrefix, prefixVa);

        for (int i = 0; i < kKindCount; ++i)
        {
            const Terrain::FoliageKind kind = kKindOrder[i];
            const bool twoSided = kind == Terrain::FoliageKind::Grass || kind == Terrain::FoliageKind::Tree || kind == Terrain::FoliageKind::Flower;
            ID3D12PipelineState* pso = twoSided ? m_gbPsoTwoSided.Get() : m_gbPso.Get();
            if (m_ultraviolet)
                pso = twoSided ? m_gbUvTwoSided.Get() : m_gbUv.Get();
            else if (m_thermal)
                pso = twoSided ? m_gbThermalTwoSided.Get() : m_gbThermal.Get();
            if (pso)
                cmd->SetPipelineState(pso);
            D3D12_GPU_VIRTUAL_ADDRESS indexVa = indexBase + static_cast<UINT64>(i) * kIndexKindStride;
            if (m_gpuCull && m_cull)
                indexVa = m_cull->indices[m_activeSlot]->GetGPUVirtualAddress() + cullIndexByteOffset(m_activeView, static_cast<uint32_t>(i));
            for (uint32_t variant = 0; variant < Terrain::kMaxFoliageVariants; ++variant)
            {
                const uint32_t count = static_cast<uint32_t>(m_cpuIndex[i][variant].size());
                if (!m_gpuCull && count == 0)
                    continue;
                if (m_gpuCull && m_kindResident[i] == 0)
                    continue;
                const AssetRef<Model>& model = prototypes.model(kind, static_cast<uint8_t>(variant));
                if (!model)
                    continue;
                const bool worldScale = prototypes.worldScale(kind, static_cast<uint8_t>(variant));
                forKindParts(gpu, model, false, m_gpuCull, [&](const GpuModel::Part& part, const Model::Part*, uint32_t partSlot) {
                    gpu.bindMaterial(cmd, part.materialId, kGbMaterial);
                    FoliageGBufferConstants cb{};
                    copyMatrix(cb.localToRoot, partLocalToRoot(part.localToRoot, kind, worldScale));
                    cb.kindIndex = static_cast<float>(i);
                    cb.kindPad   = static_cast<float>(variant);
                    fillSurface(gpu, part.materialId, cb);
                    cmd->SetGraphicsRoot32BitConstants(kGbConstants, static_cast<UINT>(sizeof(cb) / 4), &cb, 0);
                    cmd->SetGraphicsRootShaderResourceView(kGbWorlds, worldVa);
                    cmd->SetGraphicsRootShaderResourceView(kGbIndices, indexVa);
                    if (m_gpuCull && m_cull)
                        part.mesh.drawIndirect(cmd, m_cull->signature.Get(), m_cull->args[m_activeSlot].Get(), cullArgumentOffset(m_activeView, static_cast<uint32_t>(i), variant, partSlot));
                    else
                        part.mesh.drawInstanced(cmd, count);
                });
            }
        }
    }

    bool FoliagePipeline::beginDepthView(
        Renderer& renderer,
        AssetManager& assets,
        FoliagePrototypes& prototypes,
        const Terrain::TerrainGrid& grid,
        const std::vector<Terrain::FoliageRecord>* editorRecords,
        const Terrain::FoliageDensity& density,
        const Camera3D& camera,
        const Frustum3f& casterFrustum,
        const Math::Sphere3f* casterSphere)
    {
        m_renderer        = &renderer;
        m_depthRenderer   = &renderer;
        m_depthPrototypes = &prototypes;
        m_depthReady = prepare(renderer, assets, prototypes, grid, editorRecords, density, camera, casterFrustum, casterSphere);
        if (m_depthReady)
            m_depthReady = writeViewConstants(camera, nullptr, nullptr);
        m_depthCullPending = m_depthReady && m_gpuCull;
        return m_depthReady;
    }

    void FoliagePipeline::drawPreparedDepth(ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& lightViewProj, bool profileCull)
    {
        if (!cmd || !m_depthReady || !m_depthRenderer || !m_depthPrototypes)
            return;

        Renderer& renderer = *m_depthRenderer;
        FoliagePrototypes& prototypes = *m_depthPrototypes;
        GpuResourceCache& gpu = renderer.gpuResources();
        if (m_depthCullPending)
        {
            m_depthCullPending = false;
            tryGpuCull(cmd, gpu, prototypes, true, true, profileCull);
        }
        cmd->SetGraphicsRootSignature(m_depthRoot.Get());
        cmd->SetGraphicsRootConstantBufferView(kDepthView, viewConstantAddress());
        cmd->SetPipelineState(m_depthPso.Get());

        const D3D12_GPU_VIRTUAL_ADDRESS worldVa   = m_worldGpu + static_cast<UINT64>(m_activeSlot) * kWorldStride;
        const D3D12_GPU_VIRTUAL_ADDRESS indexBase = m_indexGpu + (static_cast<UINT64>(m_activeSlot) * kViewsPerFrame + m_activeView) * kIndexViewStride;
        D3D12_GPU_VIRTUAL_ADDRESS prefixVa = m_cpuPrefixGpu + (static_cast<UINT64>(m_activeSlot) * kViewsPerFrame + m_activeView) * kVariantInfoStride;
        if (m_gpuCull && m_cull && m_cull->prefix[m_activeSlot])
            prefixVa = m_cull->prefix[m_activeSlot]->GetGPUVirtualAddress();
        cmd->SetGraphicsRootShaderResourceView(kDepthPrefix, prefixVa);

        for (int i = 0; i < kKindCount; ++i)
        {
            if (kKindOrder[i] == Terrain::FoliageKind::Grass)
                continue;
            const Terrain::FoliageKind kind = kKindOrder[i];
            const bool twoSided = kind == Terrain::FoliageKind::Tree || kind == Terrain::FoliageKind::Flower;
            cmd->SetPipelineState(twoSided ? m_depthPsoTwoSided.Get() : m_depthPso.Get());
            D3D12_GPU_VIRTUAL_ADDRESS indexVa = indexBase + static_cast<UINT64>(i) * kIndexKindStride;
            if (m_gpuCull && m_cull)
                indexVa = m_cull->indices[m_activeSlot]->GetGPUVirtualAddress() + cullIndexByteOffset(m_activeView, static_cast<uint32_t>(i));
            for (uint32_t variant = 0; variant < Terrain::kMaxFoliageVariants; ++variant)
            {
                const uint32_t count = static_cast<uint32_t>(m_cpuIndex[i][variant].size());
                if (!m_gpuCull && count == 0)
                    continue;
                if (m_gpuCull && m_kindResident[i] == 0)
                    continue;
                const AssetRef<Model>& model = prototypes.model(kind, static_cast<uint8_t>(variant));
                if (!model)
                    continue;
                const bool worldScale = prototypes.worldScale(kind, static_cast<uint8_t>(variant));
                forKindParts(gpu, model, true, m_gpuCull, [&](const GpuModel::Part& part, const Model::Part* cpuPart, uint32_t partSlot) {
                    gpu.bindMaterial(cmd, part.materialId, kDepthMaterial);
                    FoliageDepthConstants cb{};
                    copyMatrix(cb.localToRoot, partLocalToRoot(part.localToRoot, kind, worldScale));
                    copyMatrix(cb.lightWVP, lightViewProj);
                    cb.kindIndex     = static_cast<float>(i);
                    cb.pad           = static_cast<float>(variant);
                    cb.alphaCutoff   = 0.5f;
                    cb.alphaModeMask = 0.0f;
                    if (cpuPart && cpuPart->material)
                    {
                        cb.alphaCutoff   = cpuPart->material->alphaCutoff();
                        cb.alphaModeMask = cpuPart->material->alphaMode() == MaterialAlphaMode::Mask ? 1.0f : 0.0f;
                    }
                    cmd->SetGraphicsRoot32BitConstants(kDepthConstants, static_cast<UINT>(sizeof(cb) / 4), &cb, 0);
                    cmd->SetGraphicsRootShaderResourceView(kDepthWorlds, worldVa);
                    cmd->SetGraphicsRootShaderResourceView(kDepthIndices, indexVa);
                    if (m_gpuCull && m_cull)
                        part.mesh.drawIndirect(cmd, m_cull->signature.Get(), m_cull->args[m_activeSlot].Get(), cullArgumentOffset(m_activeView, static_cast<uint32_t>(i), variant, partSlot));
                    else
                        part.mesh.drawInstanced(cmd, count);
                });
            }
        }
    }

    void FoliagePipeline::clearRetainedDepth()
    {
        m_retained.clear();
    }

    uint32_t FoliagePipeline::sealDepthView()
    {
        if (!m_depthReady || !m_depthRenderer || !m_depthPrototypes)
            return ~0u;
        if (m_retained.size() >= kCullViewsPerFrame)
            return ~0u;

        RetainedFoliageDepth retained{};
        retained.desc     = makeCullDesc(true);
        retained.gpu      = m_depthCullPending && m_gpuCull && m_cull != nullptr;
        retained.slot     = m_activeSlot;
        retained.viewCb   = viewConstantAddress();
        retained.worldVa  = m_worldGpu + static_cast<UINT64>(m_activeSlot) * kWorldStride;
        retained.indexBase = m_indexGpu + (static_cast<UINT64>(m_activeSlot) * kViewsPerFrame + m_activeView) * kIndexViewStride;
        GpuResourceCache& gpu = m_depthRenderer->gpuResources();
        if (retained.gpu && !fillPartArgs(gpu, *m_depthPrototypes, true, retained.desc))
            retained.gpu = false;
        for (int kind = 0; kind < kKindCount; ++kind)
        {
            for (uint32_t variant = 0; variant < Terrain::kMaxFoliageVariants; ++variant)
                retained.cpuCount[kind][variant] = static_cast<uint32_t>(m_cpuIndex[kind][variant].size());
        }
        for (int kind = 0; kind < kKindCount; ++kind)
        {
            if (kKindOrder[kind] == Terrain::FoliageKind::Grass)
                continue;
            const bool twoSided = kKindOrder[kind] == Terrain::FoliageKind::Tree || kKindOrder[kind] == Terrain::FoliageKind::Flower;
            for (uint32_t variant = 0; variant < Terrain::kMaxFoliageVariants; ++variant)
            {
                const AssetRef<Model>& model = m_depthPrototypes->model(kKindOrder[kind], static_cast<uint8_t>(variant));
                if (!model)
                    continue;
                if (!retained.gpu && retained.cpuCount[kind][variant] == 0)
                    continue;
                const bool worldScale = m_depthPrototypes->worldScale(kKindOrder[kind], static_cast<uint8_t>(variant));
                forKindParts(gpu, model, true, retained.gpu, [&](const GpuModel::Part& part, const Model::Part* cpuPart, uint32_t partSlot) {
                    RetainedFoliagePart stored;
                    stored.mesh          = &part.mesh;
                    stored.material      = part.materialId;
                    stored.localToRoot   = partLocalToRoot(part.localToRoot, kKindOrder[kind], worldScale);
                    stored.kind          = static_cast<uint32_t>(kind);
                    stored.variant       = variant;
                    stored.partSlot      = partSlot;
                    stored.worldScale    = worldScale;
                    stored.twoSided      = twoSided;
                    if (cpuPart && cpuPart->material)
                    {
                        stored.alphaCutoff   = cpuPart->material->alphaCutoff();
                        stored.alphaModeMask = cpuPart->material->alphaMode() == MaterialAlphaMode::Mask ? 1.0f : 0.0f;
                    }
                    retained.parts.push_back(stored);
                });
            }
        }

        // The worker draws the snapshot. A later main-thread depth view must not cull this slot's camera set.
        m_depthCullPending = false;
        const uint32_t index = static_cast<uint32_t>(m_retained.size());
        m_retained.push_back(std::move(retained));
        return index;
    }

    void FoliagePipeline::drawRetainedDepth(ID3D12GraphicsCommandList* cmd, GpuResourceCache& gpu, const Math::Matrix4f& lightViewProj, uint32_t index)
    {
        if (!cmd || index >= m_retained.size() || !m_depthRoot || !m_depthPso)
            return;
        const RetainedFoliageDepth& retained = m_retained[index];
        bool gpuDraw = retained.gpu && m_cull && depthResourceSetReady();
        if (gpuDraw && !recordCull(cmd, retained.slot, retained.desc, CullResourceSet::Depth))
            gpuDraw = false;

        cmd->SetGraphicsRootSignature(m_depthRoot.Get());
        cmd->SetGraphicsRootConstantBufferView(kDepthView, retained.viewCb);
        cmd->SetPipelineState(m_depthPso.Get());
        D3D12_GPU_VIRTUAL_ADDRESS prefixVa = m_cpuPrefixGpu + (static_cast<UINT64>(retained.slot) * kViewsPerFrame + retained.desc.viewIndex) * kVariantInfoStride;
        if (gpuDraw && m_cull->depthPrefix[retained.slot])
            prefixVa = m_cull->depthPrefix[retained.slot]->GetGPUVirtualAddress();
        cmd->SetGraphicsRootShaderResourceView(kDepthPrefix, prefixVa);
        for (const RetainedFoliagePart& part : retained.parts)
        {
            if (!part.mesh || !part.mesh->valid() || part.kind >= 4 || part.variant >= Terrain::kMaxFoliageVariants)
                continue;
            const uint32_t count = gpuDraw ? retained.desc.kindResident[part.kind] : retained.cpuCount[part.kind][part.variant];
            if (!gpuDraw && count == 0)
                continue;
            if (gpuDraw && retained.desc.kindResident[part.kind] == 0)
                continue;
            cmd->SetPipelineState(part.twoSided && m_depthPsoTwoSided ? m_depthPsoTwoSided.Get() : m_depthPso.Get());
            gpu.bindMaterial(cmd, part.material, kDepthMaterial);
            FoliageDepthConstants cb{};
            copyMatrix(cb.localToRoot, part.localToRoot);
            copyMatrix(cb.lightWVP, lightViewProj);
            cb.kindIndex     = static_cast<float>(part.kind);
            cb.pad           = static_cast<float>(part.variant);
            cb.alphaCutoff   = part.alphaCutoff;
            cb.alphaModeMask = part.alphaModeMask;
            cmd->SetGraphicsRoot32BitConstants(kDepthConstants, static_cast<UINT>(sizeof(cb) / 4), &cb, 0);
            cmd->SetGraphicsRootShaderResourceView(kDepthWorlds, retained.worldVa);
            D3D12_GPU_VIRTUAL_ADDRESS indexVa = retained.indexBase + static_cast<UINT64>(part.kind) * kIndexKindStride;
            if (gpuDraw)
                indexVa = m_cull->depthIndices[retained.slot]->GetGPUVirtualAddress() + cullIndexByteOffset(retained.desc.viewIndex, part.kind);
            cmd->SetGraphicsRootShaderResourceView(kDepthIndices, indexVa);
            if (gpuDraw)
                part.mesh->drawIndirect(cmd, m_cull->signature.Get(), m_cull->depthArgs[retained.slot].Get(), cullArgumentOffset(retained.desc.viewIndex, part.kind, part.variant, part.partSlot));
            else
                part.mesh->drawInstanced(cmd, count);
        }
    }

    void FoliagePipeline::drawDepth(
        ID3D12GraphicsCommandList* cmd,
        Renderer& renderer,
        AssetManager& assets,
        FoliagePrototypes& prototypes,
        const Terrain::TerrainGrid& grid,
        const std::vector<Terrain::FoliageRecord>* editorRecords,
        const Terrain::FoliageDensity& density,
        const Camera3D& camera,
        const Math::Matrix4f& lightViewProj,
        const Frustum3f& casterFrustum)
    {
        if (!cmd)
            return;
        if (!beginDepthView(renderer, assets, prototypes, grid, editorRecords, density, camera, casterFrustum, nullptr))
            return;
        drawPreparedDepth(cmd, lightViewProj);
    }

    void FoliagePipeline::setWind(const Terrain::WindSettings& wind, double timeSec, double prevTimeSec)
    {
        m_wind = wind;
        Terrain::clampWindSettings(m_wind);
        m_windTime     = timeSec;
        m_windPrevTime = prevTimeSec;
        for (int kind = 0; kind < kKindCount; ++kind)
            m_sway[kind] = Terrain::kindSwayMetres(m_wind, kind);
    }

    D3D12_GPU_VIRTUAL_ADDRESS FoliagePipeline::viewConstantAddress() const
    {
        const UINT64 offset = (static_cast<UINT64>(m_activeSlot) * kViewsPerFrame + m_activeView) * Terrain::kFoliageViewCbStride;
        return m_viewGpu + offset;
    }

    bool FoliagePipeline::writeViewConstants(const Camera3D& camera, const Math::Matrix4f* viewProj, const Math::Matrix4f* prevViewProj)
    {
        if (!m_viewMap)
            return false;

        Terrain::FoliageViewConstants cb{};
        if (viewProj)
            copyMatrix(cb.viewProj, *viewProj);
        if (prevViewProj)
            copyMatrix(cb.prevViewProj, *prevViewProj);

        const auto packWind = [](float dst[4], const Terrain::WindSettings& wind, double timeSec, float gust) {
            dst[0] = std::sin(wind.headingRad);
            dst[1] = std::cos(wind.headingRad);
            dst[2] = wind.strength * gust;
            dst[3] = static_cast<float>(timeSec);
        };
        packWind(cb.windCur, m_wind, m_windTime, Terrain::gustAt(m_windTime, m_wind));
        packWind(cb.windPrev, m_wind, m_windPrevTime, Terrain::gustAt(m_windPrevTime, m_wind));

        cb.field[0] = m_wind.spatialFreq;
        cb.field[1] = m_wind.temporalFreq;
        cb.field[2] = m_wind.deflection;
        std::memcpy(&cb.field[3], &m_wind.seed, sizeof(float));

        for (int kind = 0; kind < kKindCount; ++kind)
        {
            const Terrain::TreeWindParams& params = m_wind.kind[kind];
            cb.kindA[kind][0] = params.stiffness;
            cb.kindA[kind][1] = params.mainBendMetres * params.influence;
            cb.kindA[kind][2] = params.branchAmp * params.influence;
            cb.kindA[kind][3] = params.edgeAmp * params.influence;
            cb.kindB[kind][0] = params.flutterFreq;
            cb.kindB[kind][1] = params.detailFadeStart;
            cb.kindB[kind][2] = params.detailFadeEnd;
            cb.kindB[kind][3] = params.bendFadeStart;
            cb.kindC[kind][0] = params.bendFadeEnd;
            cb.kindC[kind][1] = m_boundsMax[kind][1] * foliageDrawScaleY(kKindOrder[kind], false);
            cb.kindC[kind][2] = params.influence;
            cb.kindC[kind][3] = 0.0f;
        }

        const Math::Vector3f eye = camera.GetPosition();
        cb.eye[0] = eye.x;
        cb.eye[1] = eye.y;
        cb.eye[2] = eye.z;
        cb.eye[3] = 1.0f;

        const UINT64 offset = (static_cast<UINT64>(m_activeSlot) * kViewsPerFrame + m_activeView) * Terrain::kFoliageViewCbStride;
        std::memcpy(m_viewMap + offset, &cb, sizeof(cb));
        return true;
    }

} // namespace Dark
