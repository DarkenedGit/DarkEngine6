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
            float pad[3];
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
        // 28 constants + material table + two root SRVs + view CBV = 28 + 1 + 2 + 2 + 2 = 35.
        static_assert(sizeof(FoliageGBufferConstants) / sizeof(float) + 1 + 2 + 2 + 2 <= 64, "foliage gbuffer root signature");
        static_assert(sizeof(FoliageDepthConstants) == 36 * sizeof(float), "foliage depth constants");

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
        Math::Matrix4f partLocalToRoot(const Math::Matrix4f& local, Terrain::FoliageKind kind)
        {
            if (kind == Terrain::FoliageKind::Grass || kind == Terrain::FoliageKind::Flower)
                return local * Math::Matrix4f::ScaleMatrix(10.0f);
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
        ComPtr<ID3D12PipelineState>    argsPso;
        ComPtr<ID3D12CommandSignature> signature;
        ComPtr<ID3D12DescriptorHeap>   heap;
        ComPtr<ID3D12DescriptorHeap>   cpuHeap;
        ComPtr<ID3D12Resource>         indices[FoliagePipeline::kCullFrameSlots];
        ComPtr<ID3D12Resource>         counters[FoliagePipeline::kCullFrameSlots];
        ComPtr<ID3D12Resource>         args[FoliagePipeline::kCullFrameSlots];
        ComPtr<ID3D12Resource>         kinds;
        ComPtr<ID3D12Resource>         cb;
        uint8_t*                       kindMap = nullptr;
        uint8_t*                       cbMap   = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS      kindGpu = 0;
        D3D12_GPU_VIRTUAL_ADDRESS      cbGpu   = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE    cpu[FoliagePipeline::kCullFrameSlots]{};
        D3D12_GPU_DESCRIPTOR_HANDLE    gpu[FoliagePipeline::kCullFrameSlots]{};
        D3D12_RESOURCE_STATES          argState[FoliagePipeline::kCullFrameSlots]   = { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
        D3D12_RESOURCE_STATES          indexState[FoliagePipeline::kCullFrameSlots] = { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
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

        D3D12_ROOT_PARAMETER gbParams[5]{};
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

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.MaxLOD           = D3D12_FLOAT32_MAX;
        samp.ShaderRegister   = 0;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC gbDesc{};
        gbDesc.NumParameters     = 5;
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

        D3D12_ROOT_PARAMETER depthParams[5]{};
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

        D3D12_ROOT_SIGNATURE_DESC depthDesc{};
        depthDesc.NumParameters     = 5;
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

        m_worlds  = createUpload(device, kWorldStride * kFrameCount, "CreateCommittedResource foliage worlds");
        m_indices = createUpload(device, kIndexViewStride * kViewsPerFrame * kFrameCount, "CreateCommittedResource foliage indices");
        constexpr UINT64 kViewCbSize = static_cast<UINT64>(kFrameCount) * static_cast<UINT64>(kViewsPerFrame) * Terrain::kFoliageViewCbStride;
        m_viewCb  = createUpload(device, kViewCbSize, "CreateCommittedResource foliage view CB");
        if (!m_worlds || !m_indices || !m_viewCb)
        {
            destroy();
            return false;
        }
        if (FailedHr(m_worlds->Map(0, nullptr, reinterpret_cast<void**>(&m_worldMap)), "Map foliage worlds")
            || FailedHr(m_indices->Map(0, nullptr, reinterpret_cast<void**>(&m_indexMap)), "Map foliage indices")
            || FailedHr(m_viewCb->Map(0, nullptr, reinterpret_cast<void**>(&m_viewCbMap)), "Map foliage view CB"))
        {
            destroy();
            return false;
        }
        std::memset(m_worldMap, 0, static_cast<size_t>(kWorldStride * kFrameCount));
        std::memset(m_indexMap, 0, static_cast<size_t>(kIndexViewStride * kViewsPerFrame * kFrameCount));
        m_worldGpu = m_worlds->GetGPUVirtualAddress();
        m_indexGpu = m_indices->GetGPUVirtualAddress();
        m_viewCbGpu = m_viewCb->GetGPUVirtualAddress();
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
        if (m_viewCb && m_viewCbMap)
            m_viewCb->Unmap(0, nullptr);
        m_worldMap  = nullptr;
        m_indexMap  = nullptr;
        m_viewCbMap = nullptr;
        m_worldGpu  = 0;
        m_indexGpu  = 0;
        m_viewCbGpu = 0;
        m_worlds.Reset();
        m_indices.Reset();
        m_viewCb.Reset();
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
            m_cpuIndex[i].clear();
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
            if (m_cull->kinds && m_cull->kindMap)
                m_cull->kinds->Unmap(0, nullptr);
            if (m_cull->cb && m_cull->cbMap)
                m_cull->cb->Unmap(0, nullptr);
            m_cull->kindMap = nullptr;
            m_cull->cbMap   = nullptr;
            if (m_renderer)
            {
                m_renderer->deferRelease(m_cull->root.Get());
                m_renderer->deferRelease(m_cull->compact.Get());
                m_renderer->deferRelease(m_cull->argsPso.Get());
                m_renderer->deferRelease(m_cull->signature.Get());
                m_renderer->deferRelease(m_cull->heap.Get());
                m_renderer->deferRelease(m_cull->kinds.Get());
                m_renderer->deferRelease(m_cull->cb.Get());
                for (uint32_t slot = 0; slot < kCullFrameSlots; ++slot)
                {
                    m_renderer->deferRelease(m_cull->indices[slot].Get());
                    m_renderer->deferRelease(m_cull->counters[slot].Get());
                    m_renderer->deferRelease(m_cull->args[slot].Get());
                }
            }
        }
        m_cull.reset();
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

        D3D12_ROOT_PARAMETER params[6]{};
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

        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 6;
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
        ComPtr<ID3DBlob> args;
        if (!compileShaderFromContent("shaders/FoliageCull.hlsl", "CSCompact", "cs_5_0", compact) || !compileShaderFromContent("shaders/FoliageCull.hlsl", "CSArgs", "cs_5_0", args))
            return logFoliageCullUnavailable();

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = cull->root.Get();
        pso.CS             = { compact->GetBufferPointer(), compact->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&cull->compact)), "CreateComputePipelineState (foliage cull compact)"))
            return logFoliageCullUnavailable();
        pso.CS = { args->GetBufferPointer(), args->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&cull->argsPso)), "CreateComputePipelineState (foliage cull args)"))
            return logFoliageCullUnavailable();
        cull->compact->SetName(L"DE.Foliage.CullCompact");
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
        heapDesc.NumDescriptors = kCullFrameSlots;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&cull->heap)), "CreateDescriptorHeap (foliage cull)"))
            return logFoliageCullUnavailable();
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&cull->cpuHeap)), "CreateDescriptorHeap (foliage cull CPU)"))
            return logFoliageCullUnavailable();

        const UINT64 indexBytes   = static_cast<UINT64>(kCullViewsPerFrame) * kCullKindCount * Terrain::kMaxFoliageDraw * sizeof(uint32_t);
        const UINT64 counterBytes = static_cast<UINT64>(kCullKindCount) * sizeof(uint32_t);
        const UINT64 argBytes     = static_cast<UINT64>(kCullViewsPerFrame) * kCullKindCount * kCullPartsPerKind * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
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
            if (!cull->indices[slot] || !cull->counters[slot] || !cull->args[slot])
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
            uav.Buffer.NumElements  = kCullKindCount;
            device->CreateUnorderedAccessView(cull->counters[slot].Get(), nullptr, &uav, gpuCpu);
            device->CreateUnorderedAccessView(cull->counters[slot].Get(), nullptr, &uav, cpu);
            gpuCpu.ptr += increment;
            cpu.ptr += increment;
            gpu.ptr += increment;
        }

        cull->kinds = createUpload(device, kKindStride * kCullFrameSlots, "CreateCommittedResource (foliage kinds)");
        cull->cb    = createUpload(device, kCullCbStride * kCullViewsPerFrame * kCullFrameSlots, "CreateCommittedResource (foliage cull constants)");
        if (!cull->kinds || !cull->cb)
            return logFoliageCullUnavailable();
        if (FailedHr(cull->kinds->Map(0, nullptr, reinterpret_cast<void**>(&cull->kindMap)), "Map foliage kinds")
            || FailedHr(cull->cb->Map(0, nullptr, reinterpret_cast<void**>(&cull->cbMap)), "Map foliage cull constants"))
        {
            if (cull->kindMap)
                cull->kinds->Unmap(0, nullptr);
            if (cull->cbMap)
                cull->cb->Unmap(0, nullptr);
            cull->kindMap = nullptr;
            cull->cbMap   = nullptr;
            return logFoliageCullUnavailable();
        }
        std::memset(cull->kindMap, 0, static_cast<size_t>(kKindStride * kCullFrameSlots));
        std::memset(cull->cbMap, 0, static_cast<size_t>(kCullCbStride * kCullViewsPerFrame * kCullFrameSlots));
        cull->kindGpu = cull->kinds->GetGPUVirtualAddress();
        cull->cbGpu   = cull->cb->GetGPUVirtualAddress();
        cull->kinds->SetName(L"DE.Foliage.Kinds");
        cull->cb->SetName(L"DE.Foliage.CullConstants");

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

    bool FoliagePipeline::recordCull(ID3D12GraphicsCommandList* cmd, uint32_t frameSlot, const FoliageCullDesc& desc)
    {
        if (!cmd || !cullReady() || frameSlot >= kCullFrameSlots || desc.worlds == 0 || desc.kinds == 0)
            return false;
        if (desc.viewIndex >= kCullViewsPerFrame || desc.instanceCount == 0)
            return false;
        for (uint32_t kind = 0; kind < kCullKindCount; ++kind)
        {
            if (desc.partCount[kind] > kCullPartsPerKind)
                return false;
        }

        FoliageCullGpu& cull = *m_cull;
        if (!foliageArgWritable(cull.argState[frameSlot]) || !foliageIndexWritable(cull.indexState[frameSlot]))
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
            constants.boundsMax[kind][3] = 0.0f;
            for (uint32_t part = 0; part < kCullPartsPerKind; ++part)
                constants.indexCount[kind * kCullPartsPerKind + part] = desc.indexCount[kind][part];
        }

        const UINT64 cbOff = (static_cast<UINT64>(frameSlot) * kCullViewsPerFrame + desc.viewIndex) * kCullCbStride;
        std::memcpy(cull.cbMap + cbOff, &constants, sizeof(constants));

        ID3D12Resource* indices  = cull.indices[frameSlot].Get();
        ID3D12Resource* counters = cull.counters[frameSlot].Get();
        ID3D12Resource* args     = cull.args[frameSlot].Get();
        if (cull.argState[frameSlot] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            transitionBuffer(cmd, args, cull.argState[frameSlot], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cull.argState[frameSlot] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (cull.indexState[frameSlot] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            transitionBuffer(cmd, indices, cull.indexState[frameSlot], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cull.indexState[frameSlot] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        ID3D12DescriptorHeap* heaps[] = { cull.heap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetComputeRootSignature(cull.root.Get());
        cmd->SetComputeRootConstantBufferView(kCullConstants, cull.cbGpu + cbOff);
        cmd->SetComputeRootShaderResourceView(kCullWorlds, desc.worlds);
        cmd->SetComputeRootShaderResourceView(kCullKinds, desc.kinds);
        cmd->SetComputeRootUnorderedAccessView(kCullIndices, indices->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(kCullCounters, counters->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(kCullArgs, args->GetGPUVirtualAddress());

        const UINT zeros[4] = { 0, 0, 0, 0 };
        cmd->ClearUnorderedAccessViewUint(cull.gpu[frameSlot], cull.cpu[frameSlot], counters, zeros, 0, nullptr);
        uavBarrier(cmd, counters);

        cmd->SetPipelineState(cull.compact.Get());
        const UINT groups = (instanceCount + 63u) / 64u;
        cmd->Dispatch(groups, 1, 1);
        uavBarrier(cmd, counters);

        cmd->SetPipelineState(cull.argsPso.Get());
        cmd->Dispatch(1, 1, 1);

        D3D12_RESOURCE_BARRIER done[2]{};
        done[0].Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        done[0].UAV.pResource = args;
        done[1].Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        done[1].UAV.pResource = indices;
        cmd->ResourceBarrier(2, done);
        transitionBuffer(cmd, args, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        transitionBuffer(cmd, indices, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cull.argState[frameSlot]   = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        cull.indexState[frameSlot] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
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

    void FoliagePipeline::ensureDrawSet(const Terrain::TerrainGrid& grid, const std::vector<Terrain::FoliageRecord>* editorRecords, const Camera3D& camera)
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
                m_cpuWorlds.push_back(instanceWorld(rec));
                m_cpuKinds.push_back(rec.kind);
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
        if (!m_cull || !m_cull->kindMap || frameSlot >= kFrameCount)
            return;
        if (m_kindSlotGen[frameSlot] == m_cpuGen)
            return;
        uint32_t* dst = reinterpret_cast<uint32_t*>(m_cull->kindMap + static_cast<size_t>(frameSlot) * static_cast<size_t>(kKindStride));
        const size_t n = m_cpuKinds.size() < Terrain::kMaxFoliageDraw ? m_cpuKinds.size() : static_cast<size_t>(Terrain::kMaxFoliageDraw);
        for (size_t i = 0; i < n; ++i)
            dst[i] = m_cpuKinds[i];
        m_kindSlotGen[frameSlot] = m_cpuGen;
    }

    void FoliagePipeline::snapshotKindBounds(FoliagePrototypes& prototypes)
    {
        const AssetRef<Model> models[kKindCount] = {
            prototypes.model(Terrain::FoliageKind::Tree),
            prototypes.model(Terrain::FoliageKind::Flower),
            prototypes.model(Terrain::FoliageKind::Rock),
            prototypes.model(Terrain::FoliageKind::Grass),
        };
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
            if (!models[kind] || !models[kind]->bounds().IsValid())
                continue;
            const Math::AABox3f& box = models[kind]->bounds();
            m_kindValid[kind]        = 1;
            m_boundsMin[kind][0]     = box.Min.x;
            m_boundsMin[kind][1]     = box.Min.y;
            m_boundsMin[kind][2]     = box.Min.z;
            m_boundsMax[kind][0]     = box.Max.x;
            m_boundsMax[kind][1]     = box.Max.y;
            m_boundsMax[kind][2]     = box.Max.z;

            // Inflate bounds for wind sway
            const Terrain::TreeWindParams& windParams = m_wind.kind[kind];
            if (windParams.influence > 0.0f && m_kindValid[kind] != 0)
            {
                const float m = windParams.maxSwayMetres * std::min(windParams.influence, 1.0f) * std::max(m_wind.strength, 1.0f);
                m_boundsMin[kind][0] -= m;
                m_boundsMin[kind][2] -= m;
                m_boundsMax[kind][0] += m;
                m_boundsMax[kind][2] += m;
                m_boundsMax[kind][1] += 0.1f * m;
            }
        }
        const size_t n = m_cpuWorlds.size() < Terrain::kMaxFoliageDraw ? m_cpuWorlds.size() : static_cast<size_t>(Terrain::kMaxFoliageDraw);
        for (size_t i = 0; i < n; ++i)
        {
            const int kind = m_cpuKinds[i];
            if (kind < 0 || kind >= kKindCount || m_kindValid[kind] == 0)
                continue;
            ++m_kindResident[kind];
        }
    }

    bool FoliagePipeline::buildCpuIndices(FoliagePrototypes& prototypes)
    {
        for (int i = 0; i < kKindCount; ++i)
            m_cpuIndex[i].clear();

        const AssetRef<Model> models[kKindCount] = {
            prototypes.model(Terrain::FoliageKind::Tree),
            prototypes.model(Terrain::FoliageKind::Flower),
            prototypes.model(Terrain::FoliageKind::Rock),
            prototypes.model(Terrain::FoliageKind::Grass),
        };

        const size_t n = m_cpuWorlds.size();
        for (size_t i = 0; i < n; ++i)
        {
            const int kind = m_cpuKinds[i];
            if (kind < 0 || kind >= kKindCount || !models[kind] || !models[kind]->bounds().IsValid())
                continue;
            const Math::AABox3f box = models[kind]->bounds().Transformed(m_cpuWorlds[i]);
            if (m_cpuUseSphere)
            {
                if (!box.Intersects(m_cpuSphere))
                    continue;
            }
            else if (!m_cpuFrustum.Intersects(box))
                continue;
            m_cpuIndex[kind].push_back(static_cast<uint32_t>(i));
        }

        for (int i = 0; i < kKindCount; ++i)
        {
            if (!m_cpuIndex[i].empty())
                return true;
        }
        return false;
    }

    void FoliagePipeline::publishCpuIndices(FoliagePrototypes& prototypes)
    {
        buildCpuIndices(prototypes);
        if (!m_indexMap)
            return;
        uint8_t* base = m_indexMap + (static_cast<size_t>(m_activeSlot) * kViewsPerFrame + m_activeView) * static_cast<size_t>(kIndexViewStride);
        for (int i = 0; i < kKindCount; ++i)
        {
            if (m_cpuIndex[i].empty())
                continue;
            std::memcpy(base + static_cast<size_t>(i) * static_cast<size_t>(kIndexKindStride), m_cpuIndex[i].data(), m_cpuIndex[i].size() * sizeof(uint32_t));
        }
    }

    FoliageCullDesc FoliagePipeline::makeCullDesc(bool shadow) const
    {
        FoliageCullDesc desc{};
        desc.worlds = m_worldGpu + static_cast<UINT64>(m_activeSlot) * kWorldStride;
        desc.kinds  = m_cull->kindGpu + static_cast<UINT64>(m_activeSlot) * kKindStride;
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
            if (depth && kind == static_cast<int>(Terrain::FoliageKind::Grass))
                continue;
            if (m_kindResident[kind] == 0)
                continue;
            uint32_t n = 0;
            const bool ok = forKindParts(gpu, prototypes.model(kKindOrder[kind]), depth, true, [&](const GpuModel::Part& part, const Model::Part*, uint32_t slot) {
                desc.indexCount[kind][slot] = part.mesh.indexCount();
                n                           = slot + 1;
            });
            if (!ok)
                return logFoliagePartCap();
            desc.partCount[kind] = n;
        }
        return true;
    }

    bool FoliagePipeline::tryGpuCull(ID3D12GraphicsCommandList* cmd, GpuResourceCache& gpu, FoliagePrototypes& prototypes, bool depth, bool shadow)
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
        {
            const GpuScope cullScope(cmd, "Foliage Cull", ProfileColor::Terrain);
            ok = recordCull(cmd, m_activeSlot, desc);
        }
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
        ensureDrawSet(grid, editorRecords, camera);
        if (m_cpuWorlds.empty())
            return false;

        m_cpuFrustum   = frustum;
        m_cpuUseSphere = casterSphere != nullptr;
        if (casterSphere)
            m_cpuSphere = *casterSphere;

        for (int i = 0; i < kKindCount; ++i)
            m_cpuIndex[i].clear();
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
        uint8_t* base = m_indexMap + (static_cast<size_t>(m_activeSlot) * kViewsPerFrame + m_activeView) * static_cast<size_t>(kIndexViewStride);
        for (int i = 0; i < kKindCount; ++i)
        {
            if (m_cpuIndex[i].empty())
                continue;
            std::memcpy(base + static_cast<size_t>(i) * static_cast<size_t>(kIndexKindStride), m_cpuIndex[i].data(), m_cpuIndex[i].size() * sizeof(uint32_t));
        }
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

        writeViewConstants(camera, &viewProj, &prevViewProj);

        GpuResourceCache& gpu = renderer.gpuResources();
        if (m_gpuCull)
            tryGpuCull(cmd, gpu, prototypes, false, false);

        cmd->SetGraphicsRootSignature(m_gbRoot.Get());
        ID3D12PipelineState* gbPso = m_gbPso.Get();
        if (m_ultraviolet && m_gbUv)
            gbPso = m_gbUv.Get();
        else if (m_thermal && m_gbThermal)
            gbPso = m_gbThermal.Get();
        cmd->SetPipelineState(gbPso);

        const D3D12_GPU_VIRTUAL_ADDRESS worldVa   = m_worldGpu + static_cast<UINT64>(m_activeSlot) * kWorldStride;
        const D3D12_GPU_VIRTUAL_ADDRESS indexBase = m_indexGpu + (static_cast<UINT64>(m_activeSlot) * kViewsPerFrame + m_activeView) * kIndexViewStride;
        const D3D12_GPU_VIRTUAL_ADDRESS viewVa    = m_viewCbGpu + (static_cast<UINT64>(m_activeSlot) * kViewsPerFrame + m_activeView) * Terrain::kFoliageViewCbStride;

        for (int i = 0; i < kKindCount; ++i)
        {
            const uint32_t count = m_gpuCull ? m_kindResident[i] : static_cast<uint32_t>(m_cpuIndex[i].size());
            if (count == 0)
                continue;
            const AssetRef<Model>& model = prototypes.model(kKindOrder[i]);
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
            forKindParts(gpu, model, false, m_gpuCull, [&](const GpuModel::Part& part, const Model::Part*, uint32_t partSlot) {
                gpu.bindMaterial(cmd, part.materialId, kGbMaterial);
                FoliageGBufferConstants cb{};
                copyMatrix(cb.localToRoot, partLocalToRoot(part.localToRoot, kind));
                fillSurface(gpu, part.materialId, cb);
                cb.kindIndex = static_cast<float>(i);
                cmd->SetGraphicsRoot32BitConstants(kGbConstants, static_cast<UINT>(sizeof(cb) / 4), &cb, 0);
                cmd->SetGraphicsRootShaderResourceView(kGbWorlds, worldVa);
                cmd->SetGraphicsRootShaderResourceView(kGbIndices, indexVa);
                cmd->SetGraphicsRootConstantBufferView(kGbView, viewVa);
                if (m_gpuCull && m_cull)
                    part.mesh.drawIndirect(cmd, m_cull->signature.Get(), m_cull->args[m_activeSlot].Get(), cullArgumentOffset(m_activeView, static_cast<uint32_t>(i), partSlot));
                else
                    part.mesh.drawInstanced(cmd, count);
            });
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
        m_depthReady       = prepare(renderer, assets, prototypes, grid, editorRecords, density, camera, casterFrustum, casterSphere);
        m_depthCullPending = m_depthReady && m_gpuCull;
        return m_depthReady;
    }

    void FoliagePipeline::drawPreparedDepth(ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& lightViewProj)
    {
        if (!cmd || !m_depthReady || !m_depthRenderer || !m_depthPrototypes)
            return;

        writeViewConstants(m_depthRenderer->camera(), nullptr, nullptr);

        Renderer& renderer = *m_depthRenderer;
        FoliagePrototypes& prototypes = *m_depthPrototypes;
        GpuResourceCache& gpu = renderer.gpuResources();
        if (m_depthCullPending)
        {
            m_depthCullPending = false;
            tryGpuCull(cmd, gpu, prototypes, true, true);
        }
        cmd->SetGraphicsRootSignature(m_depthRoot.Get());
        cmd->SetPipelineState(m_depthPso.Get());

        const D3D12_GPU_VIRTUAL_ADDRESS worldVa   = m_worldGpu + static_cast<UINT64>(m_activeSlot) * kWorldStride;
        const D3D12_GPU_VIRTUAL_ADDRESS indexBase = m_indexGpu + (static_cast<UINT64>(m_activeSlot) * kViewsPerFrame + m_activeView) * kIndexViewStride;
        const D3D12_GPU_VIRTUAL_ADDRESS viewVa    = m_viewCbGpu + (static_cast<UINT64>(m_activeSlot) * kViewsPerFrame + m_activeView) * Terrain::kFoliageViewCbStride;

        for (int i = 0; i < kKindCount; ++i)
        {
            if (kKindOrder[i] == Terrain::FoliageKind::Grass)
                continue;
            const uint32_t count = m_gpuCull ? m_kindResident[i] : static_cast<uint32_t>(m_cpuIndex[i].size());
            if (count == 0)
                continue;
            const AssetRef<Model>& model = prototypes.model(kKindOrder[i]);
            const Terrain::FoliageKind kind = kKindOrder[i];
            const bool twoSided = kind == Terrain::FoliageKind::Tree || kind == Terrain::FoliageKind::Flower;
            cmd->SetPipelineState(twoSided ? m_depthPsoTwoSided.Get() : m_depthPso.Get());
            D3D12_GPU_VIRTUAL_ADDRESS indexVa = indexBase + static_cast<UINT64>(i) * kIndexKindStride;
            if (m_gpuCull && m_cull)
                indexVa = m_cull->indices[m_activeSlot]->GetGPUVirtualAddress() + cullIndexByteOffset(m_activeView, static_cast<uint32_t>(i));
            forKindParts(gpu, model, true, m_gpuCull, [&](const GpuModel::Part& part, const Model::Part* cpuPart, uint32_t partSlot) {
                gpu.bindMaterial(cmd, part.materialId, kDepthMaterial);
                FoliageDepthConstants cb{};
                copyMatrix(cb.localToRoot, partLocalToRoot(part.localToRoot, kind));
                copyMatrix(cb.lightWVP, lightViewProj);
                cb.alphaCutoff   = 0.5f;
                cb.alphaModeMask = 0.0f;
                cb.kindIndex     = static_cast<float>(i);
                if (cpuPart && cpuPart->material)
                {
                    cb.alphaCutoff   = cpuPart->material->alphaCutoff();
                    cb.alphaModeMask = cpuPart->material->alphaMode() == MaterialAlphaMode::Mask ? 1.0f : 0.0f;
                }
                cmd->SetGraphicsRoot32BitConstants(kDepthConstants, static_cast<UINT>(sizeof(cb) / 4), &cb, 0);
                cmd->SetGraphicsRootShaderResourceView(kDepthWorlds, worldVa);
                cmd->SetGraphicsRootShaderResourceView(kDepthIndices, indexVa);
                cmd->SetGraphicsRootConstantBufferView(kDepthView, viewVa);
                if (m_gpuCull && m_cull)
                    part.mesh.drawIndirect(cmd, m_cull->signature.Get(), m_cull->args[m_activeSlot].Get(), cullArgumentOffset(m_activeView, static_cast<uint32_t>(i), partSlot));
                else
                    part.mesh.drawInstanced(cmd, count);
            });
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

    void FoliagePipeline::setWind(const Terrain::WindSettings& settings, double time, double prevTime)
    {
        m_wind         = settings;
        m_windTime     = time;
        m_windPrevTime = prevTime;
    }

    void FoliagePipeline::writeViewConstants(const Camera3D& camera, const Math::Matrix4f* viewProj, const Math::Matrix4f* prevViewProj)
    {
        if (!m_viewCbMap)
            return;

        const UINT64 offset = (static_cast<UINT64>(m_activeSlot) * kViewsPerFrame + m_activeView) * Terrain::kFoliageViewCbStride;
        Terrain::FoliageViewConstants cb{};

        if (viewProj)
            copyMatrix(cb.viewProj, *viewProj);
        if (prevViewProj)
            copyMatrix(cb.prevViewProj, *prevViewProj);

        const float heading = m_wind.headingRad;
        const float gustCur = Terrain::gustAt(m_windTime, m_wind);
        const float gustPrv = Terrain::gustAt(m_windPrevTime, m_wind);
        const float strengthCur = m_wind.strength * gustCur;
        const float strengthPrv = m_wind.strength * gustPrv;

        const double wrap = 3600.0;
        double tCur  = std::fmod(m_windTime, wrap);
        double tPrv  = std::fmod(m_windPrevTime, wrap);
        if (std::floor(m_windTime / wrap) != std::floor(m_windPrevTime / wrap))
            tPrv = tCur;

        cb.windCur[0]  = std::sin(heading);
        cb.windCur[1]  = std::cos(heading);
        cb.windCur[2]  = strengthCur;
        cb.windCur[3]  = static_cast<float>(tCur);
        cb.windPrev[0] = std::sin(heading);
        cb.windPrev[1] = std::cos(heading);
        cb.windPrev[2] = strengthPrv;
        cb.windPrev[3] = static_cast<float>(tPrv);

        cb.field[0] = m_wind.spatialFreq;
        cb.field[1] = m_wind.temporalFreq;
        cb.field[2] = m_wind.deflection;
        std::memcpy(&cb.field[3], &m_wind.seed, sizeof(uint32_t));

        for (int i = 0; i < 4; ++i)
        {
            const Terrain::TreeWindParams& k = m_wind.kind[i];
            const float kindHeight = m_boundsMax[i][1];
            const float infl = k.influence;

            cb.kindA[i][0] = k.stiffness;
            cb.kindA[i][1] = k.mainBendMetres * infl;
            cb.kindA[i][2] = k.branchAmp * infl;
            cb.kindA[i][3] = k.edgeAmp * infl;

            cb.kindB[i][0] = k.flutterFreq;
            cb.kindB[i][1] = k.detailFadeStart;
            cb.kindB[i][2] = k.detailFadeEnd;
            cb.kindB[i][3] = k.bendFadeStart;

            cb.kindC[i][0] = k.bendFadeEnd;
            cb.kindC[i][1] = kindHeight;
            cb.kindC[i][2] = infl;
            cb.kindC[i][3] = 0.0f;
        }

        const Math::Vector3f pos = camera.GetPosition();
        cb.eye[0] = pos.x;
        cb.eye[1] = pos.y;
        cb.eye[2] = pos.z;
        cb.eye[3] = m_windEnabled ? 1.0f : 0.0f;

        std::memcpy(m_viewCbMap + offset, &cb, sizeof(cb));
    }

} // namespace Dark
