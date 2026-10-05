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
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"
#include "Render/LocalShadowMath.h"
#include "Render/ShadowCascades.h"
#include "Terrain/TerrainGrid.h"
#include "Terrain/TerrainTileFile.h"

#include <cmath>
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
            float viewProj[16];
            float prevViewProj[16];
            float color[4];
            float roughness;
            float metallic;
            float ao;
            float normalScale;
            float alphaCutoff;
            float alphaModeMask;
        };

        struct FoliageDepthConstants
        {
            float localToRoot[16];
            float lightWVP[16];
            float alphaCutoff;
            float alphaModeMask;
            float pad[2];
        };

        static_assert(sizeof(FoliageGBufferConstants) == 58 * sizeof(float), "foliage gbuffer constants");
        // 58 constants + material table + two root SRVs = 63. A third matrix does not fit.
        static_assert(sizeof(FoliageGBufferConstants) / sizeof(float) + 1 + 2 + 2 <= 64, "foliage gbuffer root signature");
        static_assert(sizeof(FoliageDepthConstants) == 36 * sizeof(float), "foliage depth constants");

        constexpr UINT64 kWorldStride     = static_cast<UINT64>(Terrain::kMaxFoliageDraw) * sizeof(Math::Matrix4f);
        constexpr UINT64 kIndexKindStride = static_cast<UINT64>(Terrain::kMaxFoliageDraw) * sizeof(uint32_t);
        constexpr UINT64 kIndexViewStride = kIndexKindStride * static_cast<UINT64>(kKindCount);

        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

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

        void copyMatrix(float dst[16], const Math::Matrix4f& m)
        {
            std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
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
        srvRange.NumDescriptors                    = GpuMaterial::kMapSrvCount;
        srvRange.BaseShaderRegister                = 0;
        srvRange.RegisterSpace                     = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER gbParams[4]{};
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

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.MaxLOD           = D3D12_FLOAT32_MAX;
        samp.ShaderRegister   = 0;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC gbDesc{};
        gbDesc.NumParameters     = 4;
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

        D3D12_ROOT_PARAMETER depthParams[4]{};
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

        D3D12_ROOT_SIGNATURE_DESC depthDesc{};
        depthDesc.NumParameters     = 4;
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
        if (!m_worlds || !m_indices)
        {
            destroy();
            return false;
        }
        if (FailedHr(m_worlds->Map(0, nullptr, reinterpret_cast<void**>(&m_worldMap)), "Map foliage worlds")
            || FailedHr(m_indices->Map(0, nullptr, reinterpret_cast<void**>(&m_indexMap)), "Map foliage indices"))
        {
            destroy();
            return false;
        }
        std::memset(m_worldMap, 0, static_cast<size_t>(kWorldStride * kFrameCount));
        std::memset(m_indexMap, 0, static_cast<size_t>(kIndexViewStride * kViewsPerFrame * kFrameCount));
        m_worldGpu = m_worlds->GetGPUVirtualAddress();
        m_indexGpu = m_indices->GetGPUVirtualAddress();
        m_cpuGen   = 1;

        DE_LOG_INFO(LogCategory::Render, "FoliagePipeline: ready");
        return true;
    }

    void FoliagePipeline::destroy()
    {
        if (m_worlds && m_worldMap)
            m_worlds->Unmap(0, nullptr);
        if (m_indices && m_indexMap)
            m_indices->Unmap(0, nullptr);
        m_worldMap  = nullptr;
        m_indexMap  = nullptr;
        m_worldGpu  = 0;
        m_indexGpu  = 0;
        m_worlds.Reset();
        m_indices.Reset();
        m_gbPso.Reset();
        m_gbPsoTwoSided.Reset();
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
        m_frameIndex   = ~0u;
        m_viewCursor   = 0;
        m_activeSlot   = 0;
        m_activeView   = 0;
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
        if (!isValid() || !prototypes.ready() || !grid.valid())
            return false;

        prototypes.sync(renderer, assets, density);
        ensureDrawSet(grid, editorRecords, camera);
        if (m_cpuWorlds.empty())
            return false;

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
            if (casterSphere)
            {
                if (!box.Intersects(*casterSphere))
                    continue;
            }
            else if (!frustum.Intersects(box))
                continue;
            m_cpuIndex[kind].push_back(static_cast<uint32_t>(i));
        }

        bool any = false;
        for (int i = 0; i < kKindCount; ++i)
        {
            if (!m_cpuIndex[i].empty())
                any = true;
        }
        if (!any)
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
        if (!cmd || !prepare(renderer, assets, prototypes, grid, editorRecords, density, camera, frustum, nullptr))
            return;

        GpuResourceCache& gpu = renderer.gpuResources();
        cmd->SetGraphicsRootSignature(m_gbRoot.Get());
        cmd->SetPipelineState(m_gbPso.Get());

        const D3D12_GPU_VIRTUAL_ADDRESS worldVa = m_worldGpu + static_cast<UINT64>(m_activeSlot) * kWorldStride;
        const D3D12_GPU_VIRTUAL_ADDRESS indexBase = m_indexGpu + (static_cast<UINT64>(m_activeSlot) * kViewsPerFrame + m_activeView) * kIndexViewStride;

        for (int i = 0; i < kKindCount; ++i)
        {
            const uint32_t count = static_cast<uint32_t>(m_cpuIndex[i].size());
            if (count == 0)
                continue;
            const AssetRef<Model>& model = prototypes.model(kKindOrder[i]);
            if (!model || !gpu.ensureModel(model))
                continue;
            GpuModel* gm = gpu.model(model->id);
            if (!gm)
                continue;
            const Terrain::FoliageKind kind = kKindOrder[i];
            const bool twoSided = kind == Terrain::FoliageKind::Grass || kind == Terrain::FoliageKind::Tree || kind == Terrain::FoliageKind::Flower;
            cmd->SetPipelineState(twoSided ? m_gbPsoTwoSided.Get() : m_gbPso.Get());
            const D3D12_GPU_VIRTUAL_ADDRESS indexVa = indexBase + static_cast<UINT64>(i) * kIndexKindStride;
            for (const GpuModel::Part& part : gm->opaque())
            {
                if (part.skinned || !part.mesh.valid())
                    continue;
                gpu.bindMaterial(cmd, part.materialId, kGbMaterial);
                FoliageGBufferConstants cb{};
                copyMatrix(cb.localToRoot, partLocalToRoot(part.localToRoot, kind));
                copyMatrix(cb.viewProj, viewProj);
                copyMatrix(cb.prevViewProj, prevViewProj);
                fillSurface(gpu, part.materialId, cb);
                cmd->SetGraphicsRoot32BitConstants(kGbConstants, static_cast<UINT>(sizeof(cb) / 4), &cb, 0);
                cmd->SetGraphicsRootShaderResourceView(kGbWorlds, worldVa);
                cmd->SetGraphicsRootShaderResourceView(kGbIndices, indexVa);
                part.mesh.drawInstanced(cmd, count);
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
        m_depthRenderer   = &renderer;
        m_depthPrototypes = &prototypes;
        m_depthReady = prepare(renderer, assets, prototypes, grid, editorRecords, density, camera, casterFrustum, casterSphere);
        return m_depthReady;
    }

    void FoliagePipeline::drawPreparedDepth(ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& lightViewProj)
    {
        if (!cmd || !m_depthReady || !m_depthRenderer || !m_depthPrototypes)
            return;

        Renderer& renderer = *m_depthRenderer;
        FoliagePrototypes& prototypes = *m_depthPrototypes;
        GpuResourceCache& gpu = renderer.gpuResources();
        cmd->SetGraphicsRootSignature(m_depthRoot.Get());
        cmd->SetPipelineState(m_depthPso.Get());

        const D3D12_GPU_VIRTUAL_ADDRESS worldVa = m_worldGpu + static_cast<UINT64>(m_activeSlot) * kWorldStride;
        const D3D12_GPU_VIRTUAL_ADDRESS indexBase = m_indexGpu + (static_cast<UINT64>(m_activeSlot) * kViewsPerFrame + m_activeView) * kIndexViewStride;

        for (int i = 0; i < kKindCount; ++i)
        {
            if (kKindOrder[i] == Terrain::FoliageKind::Grass)
                continue;
            const uint32_t count = static_cast<uint32_t>(m_cpuIndex[i].size());
            if (count == 0)
                continue;
            const AssetRef<Model>& model = prototypes.model(kKindOrder[i]);
            if (!model || !gpu.ensureModel(model))
                continue;
            GpuModel* gm = gpu.model(model->id);
            if (!gm)
                continue;
            const Terrain::FoliageKind kind = kKindOrder[i];
            const bool twoSided = kind == Terrain::FoliageKind::Tree || kind == Terrain::FoliageKind::Flower;
            cmd->SetPipelineState(twoSided ? m_depthPsoTwoSided.Get() : m_depthPso.Get());
            const D3D12_GPU_VIRTUAL_ADDRESS indexVa = indexBase + static_cast<UINT64>(i) * kIndexKindStride;
            const std::vector<Model::Part>& cpuParts = model->opaque();
            const std::vector<GpuModel::Part>& gpuParts = gm->opaque();
            const size_t partCount = cpuParts.size() < gpuParts.size() ? cpuParts.size() : gpuParts.size();
            for (size_t p = 0; p < partCount; ++p)
            {
                const GpuModel::Part& part = gpuParts[p];
                if (part.skinned || !part.mesh.valid())
                    continue;
                gpu.bindMaterial(cmd, part.materialId, kDepthMaterial);
                FoliageDepthConstants cb{};
                copyMatrix(cb.localToRoot, partLocalToRoot(part.localToRoot, kind));
                copyMatrix(cb.lightWVP, lightViewProj);
                cb.alphaCutoff   = 0.5f;
                cb.alphaModeMask = 0.0f;
                if (cpuParts[p].material)
                {
                    cb.alphaCutoff   = cpuParts[p].material->alphaCutoff();
                    cb.alphaModeMask = cpuParts[p].material->alphaMode() == MaterialAlphaMode::Mask ? 1.0f : 0.0f;
                }
                cmd->SetGraphicsRoot32BitConstants(kDepthConstants, static_cast<UINT>(sizeof(cb) / 4), &cb, 0);
                cmd->SetGraphicsRootShaderResourceView(kDepthWorlds, worldVa);
                cmd->SetGraphicsRootShaderResourceView(kDepthIndices, indexVa);
                part.mesh.drawInstanced(cmd, count);
            }
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

} // namespace Dark
