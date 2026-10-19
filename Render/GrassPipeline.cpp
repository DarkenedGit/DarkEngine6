#include "Render/GrassPipeline.h"

#include "Core/Log.h"
#include "Render/DepthState.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"
#include "Terrain/GrassMesh.h"
#include "Terrain/TerrainLodFile.h"

#include <cstring>

namespace Dark
{
    namespace
    {
        static_assert(Renderer::kFrameCount == Terrain::kGrassFrameSlots, "grass upload slots");

        constexpr uint64_t kCbStride = Terrain::kGrassFrameCbStride;

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

    } // namespace

    GrassPipeline::GrassPipeline() = default;

    GrassPipeline::~GrassPipeline()
    {
        destroy();
    }

    bool GrassPipeline::fail(const char* why)
    {
        if (why && why[0])
            DE_LOG_ERROR(LogCategory::Render, "GrassPipeline::create: {}", why);
        destroy();
        return false;
    }

    bool GrassPipeline::create(Renderer& renderer)
    {
        destroy();
        ID3D12Device* device = renderer.device();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "GrassPipeline::create: null device");
            return false;
        }
        m_renderer = &renderer;

        for (int lod = 0; lod < 3; ++lod)
        {
            MeshData data;
            if (!Terrain::buildGrassBladeMesh(lod, data))
                return fail("blade mesh");
            if (!Mesh::tryCreate(renderer, data, m_mesh[lod]))
                return fail("Mesh::tryCreate");
        }

        D3D12_ROOT_PARAMETER params[5]{};
        params[kRootCb].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[kRootCb].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootCb].Descriptor.ShaderRegister = 0;
        params[kRootCb].Descriptor.RegisterSpace  = 0;

        params[kRootBlades].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kRootBlades].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        params[kRootBlades].Descriptor.ShaderRegister = 0;
        params[kRootBlades].Descriptor.RegisterSpace  = 0;

        params[kRootWind].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kRootWind].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        params[kRootWind].Descriptor.ShaderRegister = 1;
        params[kRootWind].Descriptor.RegisterSpace  = 0;

        params[kRootBase].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootBase].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;
        params[kRootBase].Constants.ShaderRegister = 1;
        params[kRootBase].Constants.RegisterSpace  = 0;
        params[kRootBase].Constants.Num32BitValues = kRootBaseDwords;

        params[kRootIndices].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kRootIndices].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        params[kRootIndices].Descriptor.ShaderRegister = 2;
        params[kRootIndices].Descriptor.RegisterSpace  = 0;

        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 5;
        rootDesc.pParameters   = params;
        rootDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (grass)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Grass root signature: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return fail("root signature");
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_root)), "CreateRootSignature (grass)"))
            return fail("CreateRootSignature");
        m_root->SetName(L"DE.Grass.Root");

        ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent("shaders/GrassGBuffer.hlsl", "PSMain", "ps_5_0", ps))
            return fail("pixel shader");
        D3D_SHADER_MACRO thermalMacros[3];
        makeThermalMacros("0.40", thermalMacros);
        ComPtr<ID3DBlob> thermalPs;
        if (!compileShaderFromContent("shaders/GrassGBuffer.hlsl", "PSMain", "ps_5_0", thermalPs, thermalMacros))
            return fail("thermal pixel shader");
        D3D_SHADER_MACRO uvMacros[3];
        makeUvMacros("0.08", uvMacros);
        ComPtr<ID3DBlob> uvPs;
        if (!compileShaderFromContent("shaders/GrassGBuffer.hlsl", "PSMain", "ps_5_0", uvPs, uvMacros))
            return fail("ultraviolet pixel shader");

        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        const wchar_t* psoNames[4] = {
            L"DE.Grass.PsoLod0",
            L"DE.Grass.PsoLod1",
            L"DE.Grass.PsoLod2",
            L"DE.Grass.PsoLod3",
        };
        const char* lodDefs[4] = { "0", "1", "2", "3" };

        for (int lod = 0; lod < 4; ++lod)
        {
            D3D_SHADER_MACRO macros[2]{};
            macros[0].Name       = "GRASS_LOD";
            macros[0].Definition = lodDefs[lod];

            ComPtr<ID3DBlob> vs;
            if (!compileShaderFromContent("shaders/GrassGBuffer.hlsl", "VSMain", "vs_5_0", vs, macros))
                return fail("vertex shader");

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature                                   = m_root.Get();
            psoDesc.VS                                               = { vs->GetBufferPointer(), vs->GetBufferSize() };
            psoDesc.PS                                               = { ps->GetBufferPointer(), ps->GetBufferSize() };
            psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.BlendState.RenderTarget[2].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.BlendState.RenderTarget[3].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.SampleMask                                       = UINT_MAX;
            psoDesc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode                         = D3D12_CULL_MODE_NONE;
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

            if (FailedHr(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso[lod])), "CreateGraphicsPipelineState (grass)"))
                return fail("graphics PSO");
            m_pso[lod]->SetName(psoNames[lod]);
            D3D12_GRAPHICS_PIPELINE_STATE_DESC thermalDesc = psoDesc;
            thermalDesc.PS = { thermalPs->GetBufferPointer(), thermalPs->GetBufferSize() };
            if (FailedHr(device->CreateGraphicsPipelineState(&thermalDesc, IID_PPV_ARGS(&m_psoThermal[lod])), "CreateGraphicsPipelineState (grass thermal)"))
                return fail("thermal graphics PSO");
            D3D12_GRAPHICS_PIPELINE_STATE_DESC uvDesc = psoDesc;
            uvDesc.PS = { uvPs->GetBufferPointer(), uvPs->GetBufferSize() };
            if (FailedHr(device->CreateGraphicsPipelineState(&uvDesc, IID_PPV_ARGS(&m_psoUv[lod])), "CreateGraphicsPipelineState (grass ultraviolet)"))
                return fail("ultraviolet graphics PSO");
        }

        const Terrain::GrassRegionBudget budget = Terrain::grassRegionBudget();
        m_bladeCap = budget.bladeCap;
        m_windSlots = budget.residentTiles;
        for (int lod = 0; lod < 4; ++lod)
            m_lodSlots[lod] = budget.slots[lod];
        m_bladeSlice = static_cast<uint64_t>(m_bladeCap) * sizeof(Terrain::GrassBlade);
        m_windSlice  = static_cast<uint64_t>(m_windSlots) * sizeof(Terrain::GrassTileWind);
        if (m_bladeCap == 0 || m_windSlots == 0)
            return fail("grass region budget is empty");

        m_blades = createUpload(device, m_bladeSlice * Renderer::kFrameCount, "CreateCommittedResource grass blades");
        m_wind   = createUpload(device, m_windSlice * Renderer::kFrameCount, "CreateCommittedResource grass wind");
        m_cb     = createUpload(device, kCbStride * Renderer::kFrameCount, "CreateCommittedResource grass constants");
        if (!m_blades || !m_wind || !m_cb)
            return fail("upload buffer");

        if (FailedHr(m_blades->Map(0, nullptr, reinterpret_cast<void**>(&m_bladeMap)), "Map grass blades") ||
            FailedHr(m_wind->Map(0, nullptr, reinterpret_cast<void**>(&m_windMap)), "Map grass wind") || FailedHr(m_cb->Map(0, nullptr, reinterpret_cast<void**>(&m_cbMap)), "Map grass constants"))
            return fail("map upload");

        std::memset(m_bladeMap, 0, static_cast<size_t>(m_bladeSlice * Renderer::kFrameCount));
        std::memset(m_windMap, 0, static_cast<size_t>(m_windSlice * Renderer::kFrameCount));
        std::memset(m_cbMap, 0, static_cast<size_t>(kCbStride * Renderer::kFrameCount));
        m_bladeGpu = m_blades->GetGPUVirtualAddress();
        m_windGpu  = m_wind->GetGPUVirtualAddress();
        m_cbGpu    = m_cb->GetGPUVirtualAddress();

        m_blades->SetName(L"DE.Grass.Blades");
        m_wind->SetName(L"DE.Grass.Wind");
        m_cb->SetName(L"DE.Grass.Constants");

        // LOD-local 0,1,2,... inside each span. The vertex shader adds baseIndex, so a global identity is wrong for LOD 1+.
        m_identity = createUpload(device, static_cast<UINT64>(m_bladeCap) * sizeof(uint32_t), "CreateCommittedResource grass identity");
        if (!m_identity)
            return fail("identity indices");
        uint32_t* identity = nullptr;
        if (FailedHr(m_identity->Map(0, nullptr, reinterpret_cast<void**>(&identity)), "Map grass identity"))
            return fail("identity indices");
        uint32_t cursor = 0;
        for (int lod = 0; lod < 4; ++lod)
        {
            const uint32_t slots = m_lodSlots[lod];
            for (uint32_t i = 0; i < slots; ++i)
                identity[cursor + i] = i;
            cursor += slots;
        }
        m_identity->Unmap(0, nullptr);
        DE_ASSERT(cursor == m_bladeCap);
        m_identityGpu = m_identity->GetGPUVirtualAddress();
        m_identity->SetName(L"DE.Grass.Identity");

        DE_ASSERT(m_mesh[0].indexCount() == 24u);
        DE_ASSERT(m_mesh[1].indexCount() == 12u);
        DE_ASSERT(m_mesh[2].indexCount() == 6u);

        m_valid = true;
        DE_LOG_INFO(
            LogCategory::Render,
            "GrassPipeline: ready ({} blades, tiles {}/{}/{}/{}, resident {}, {} slices)",
            m_bladeCap,
            budget.tiles[0],
            budget.tiles[1],
            budget.tiles[2],
            budget.tiles[3],
            m_windSlots,
            Renderer::kFrameCount);
        return true;
    }

    void GrassPipeline::destroy()
    {
        if (m_blades && m_bladeMap)
            m_blades->Unmap(0, nullptr);
        if (m_wind && m_windMap)
            m_wind->Unmap(0, nullptr);
        if (m_cb && m_cbMap)
            m_cb->Unmap(0, nullptr);
        m_bladeMap = nullptr;
        m_windMap  = nullptr;
        m_cbMap    = nullptr;

        if (m_renderer)
        {
            for (int i = 0; i < 3; ++i)
                m_mesh[i].deferRelease(*m_renderer);
            m_renderer->deferRelease(m_root.Get());
            for (int i = 0; i < 4; ++i)
            {
                m_renderer->deferRelease(m_pso[i].Get());
                m_renderer->deferRelease(m_psoThermal[i].Get());
                m_renderer->deferRelease(m_psoUv[i].Get());
            }
            m_renderer->deferRelease(m_blades.Get());
            m_renderer->deferRelease(m_wind.Get());
            m_renderer->deferRelease(m_cb.Get());
            m_renderer->deferRelease(m_identity.Get());
        }
        else
        {
            for (int i = 0; i < 3; ++i)
                m_mesh[i] = Mesh{};
        }

        m_root.Reset();
        for (int i = 0; i < 4; ++i)
        {
            m_pso[i].Reset();
            m_psoThermal[i].Reset();
            m_psoUv[i].Reset();
        }
        m_thermal = false;
        m_ultraviolet = false;
        m_blades.Reset();
        m_wind.Reset();
        m_cb.Reset();
        m_bladeGpu    = 0;
        m_windGpu     = 0;
        m_cbGpu       = 0;
        m_identity.Reset();
        m_identityGpu = 0;
        m_bladeSlice  = 0;
        m_windSlice   = 0;
        m_bladeCap    = 0;
        m_windSlots   = 0;
        for (int i = 0; i < 4; ++i)
            m_lodSlots[i] = 0;
        m_renderer = nullptr;
        m_valid    = false;
    }


    bool GrassPipeline::draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const GrassLodSpan lods[4], const Terrain::GrassTileWind* tileWind, uint32_t tileCount,
                             const Terrain::GrassFrameConstants& frame, const Terrain::GrassTileDraw* tiles, uint32_t tileDraws)
    {
        if (!cmd || !m_valid || !lods || !m_bladeMap || !m_windMap || !m_cbMap)
            return false;
        ID3D12Device* device = renderer.device();
        if (!device || FAILED(device->GetDeviceRemovedReason()))
            return false;

        // Whole-LOD upload: removeSpan or append marks both frame slots, and only this fenced slot is copied.
        // A warm frame copies no blades. Wind stays a full-slice copy.
        // Each tile draw is that tile's contiguous blades. SV_InstanceID starts at 0, so the span
        // begin is added to the identity base. StartInstanceLocation stays 0.
        const uint32_t slot = renderer.frameIndex() % Terrain::kGrassFrameSlots;

        uint32_t                  counts[4]{};
        D3D12_GPU_VIRTUAL_ADDRESS bladeVa[4]{};
        uint32_t                  bladeCursor = 0;
        for (int lod = 0; lod < 4; ++lod)
        {
            const uint32_t slots = m_lodSlots[lod];
            uint32_t       count = 0;
            if (lods[lod].blades)
                count = lods[lod].count;
            if (count > slots)
                count = slots;
            const uint64_t byteOff = static_cast<uint64_t>(bladeCursor) * sizeof(Terrain::GrassBlade);
            const bool     upload  = count > 0 && (lods[lod].staleMask & static_cast<uint8_t>(1u << slot)) != 0;
            if (upload)
                std::memcpy(m_bladeMap + slot * m_bladeSlice + byteOff, lods[lod].blades, sizeof(Terrain::GrassBlade) * count);
            counts[lod]  = count;
            bladeVa[lod] = m_bladeGpu + slot * m_bladeSlice + byteOff;
            bladeCursor += slots;
        }

        uint32_t windCount = 0;
        uint8_t* windDst   = m_windMap + slot * m_windSlice;
        std::memset(windDst, 0, static_cast<size_t>(m_windSlice));
        if (tileWind && tileCount > 0)
        {
            windCount = tileCount < m_windSlots ? tileCount : m_windSlots;
            std::memcpy(windDst, tileWind, sizeof(Terrain::GrassTileWind) * windCount);
        }

        std::memcpy(m_cbMap + slot * kCbStride, &frame, sizeof(frame));

        const GpuScope scope(cmd, "Grass", ProfileColor::Grass);
        cmd->SetGraphicsRootSignature(m_root.Get());
        cmd->SetGraphicsRootConstantBufferView(kRootCb, m_cbGpu + slot * kCbStride);
        cmd->SetGraphicsRootShaderResourceView(kRootWind, m_windGpu + slot * m_windSlice);
        cmd->SetGraphicsRootShaderResourceView(kRootIndices, m_identityGpu);

        if (!tiles || tileDraws == 0)
            return true;

        for (int lod = 0; lod < 4; ++lod)
        {
            if (counts[lod] == 0)
                continue;
            bool           bound      = false;
            const int      meshIndex  = lod < 2 ? lod : 2;
            uint32_t lodBase = 0;
            for (int prev = 0; prev < lod; ++prev)
                lodBase += m_lodSlots[prev];
            for (uint32_t i = 0; i < tileDraws; ++i)
            {
                const Terrain::GrassTileDraw& tile = tiles[i];
                if (tile.lod != lod || tile.count == 0)
                    continue;
                if (tile.begin >= counts[lod] || tile.count > counts[lod] - tile.begin)
                    continue;
                if (!bound)
                {
                    cmd->SetPipelineState((m_ultraviolet && m_psoUv[lod]) ? m_psoUv[lod].Get()
                        : (m_thermal && m_psoThermal[lod]) ? m_psoThermal[lod].Get()
                        : m_pso[lod].Get());
                    cmd->SetGraphicsRootShaderResourceView(kRootBlades, bladeVa[lod]);
                    bound = true;
                }
                const uint32_t baseWords[kRootBaseDwords] = { lodBase + tile.begin, 0u, 0u, 0u };
                cmd->SetGraphicsRoot32BitConstants(kRootBase, kRootBaseDwords, baseWords, 0);
                m_mesh[meshIndex].drawInstanced(cmd, tile.count);
            }
        }
        return true;
    }

} // namespace Dark
